#!/usr/bin/env python3
"""
Re-apply Darren's Krita customisations to a fresh Krita checkout.

The self-contained parts (the dhtools plugin, the touchgestures plugin, the
Android gesture bridge) are copied wholesale. The handful of edits to files
Krita already owns are re-applied by anchoring on surrounding text rather than
by line number, so this keeps working as upstream moves underneath it.

Everything is idempotent: run it twice and the second run reports "already
applied" rather than doubling anything up.

    python3 apply-dh-changes.py --from /mnt/d/Users/Darren/Documents/krita \
                                --to   ~/krita

Add --check to see what would happen without writing anything.
"""

import argparse
import os
import re
import shutil
import sys

# Directories and files copied verbatim from the old tree.
COPY_TREES = [
    "plugins/extensions/dhtools",
    "plugins/extensions/touchgestures",
]

COPY_FILES = [
    "libs/ui/input/KisAndroidGestureBridge.h",
    "libs/ui/input/KisAndroidGestureBridge.cpp",
    "build-android-wsl.sh",
]

# Text extensions safe to strip carriage returns from on the way across.
TEXT_SUFFIXES = {
    ".c", ".cc", ".cpp", ".h", ".hpp", ".java", ".py", ".sh", ".txt",
    ".json", ".xml", ".ui", ".qrc", ".xmlgui", ".action", ".md", ".yml",
    ".yaml", ".cmake", ".gradle", ".properties",
}


class Patch:
    """One anchored edit: find `anchor`, put `insert` before or after it.

    Set `regex=True` when the surrounding code is likely to be reformatted
    upstream. An exact-text anchor is easier to read but breaks the moment
    someone wraps a one-line list onto several lines, which is exactly what
    happened to the kritaui source list.
    """

    def __init__(self, path, anchor, insert, where="after", marker=None, regex=False):
        self.path = path
        self.anchor = anchor
        self.insert = insert
        self.where = where
        self.regex = regex
        # If this string is already present, the edit is considered done.
        self.marker = marker or insert.strip().splitlines()[0]


PATCHES = [
    # --- build wiring for the gesture bridge --------------------------------
    # Anchored on the library definition rather than on any particular source
    # line: upstream reshuffles that list, the library declaration is forever.
    Patch(
        "libs/ui/CMakeLists.txt",
        anchor=r"^\s*kis_add_library\s*\(\s*kritaui\b",
        insert=("if(ANDROID)\n"
                "    list(APPEND kritaui_LIB_SRCS input/KisAndroidGestureBridge.cpp)\n"
                "endif()\n\n"),
        where="before",
        marker="input/KisAndroidGestureBridge.cpp",
        regex=True,
    ),

    # --- JNI entry point for the three-finger gesture -----------------------
    # Anchored on the opening of the Android include block, not on any single
    # header inside it -- upstream renamed KisAndroidLogHandler.h to
    # KisAndroidUtils.h, which is exactly the kind of churn that breaks an
    # exact-text anchor. The #ifdef itself is stable.
    Patch(
        "krita/main.cc",
        anchor=r"^#ifdef Q_OS_ANDROID\s*\n#include <QtAndroid>\s*\n",
        insert="#include <input/KisAndroidGestureBridge.h>\n",
        where="after",
        marker="input/KisAndroidGestureBridge.h",
        regex=True,
    ),
    Patch(
        "krita/main.cc",
        anchor='extern "C" JNIEXPORT void JNICALL\nJava_org_krita_android_JNIWrappers_openFileFromIntent',
        insert='''/**
 * Three-finger drag, forwarded raw from MainActivity.dispatchTouchEvent().
 *
 * \\p phase is KisAndroidGestureBridge::Phase, \\p dx / \\p dy are the centroid
 * offset from where the gesture started, in density-independent pixels.
 * Returns true when Krita has taken the gesture, which tells the Activity to
 * swallow the events instead of forwarding them to Qt.
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_org_krita_android_JNIWrappers_threeFingerGesture(JNIEnv* /*env*/,
                                                      jobject /*obj*/,
                                                      jint phase,
                                                      jfloat dx,
                                                      jfloat dy)
{
    return static_cast<jboolean>(KisAndroidGestureBridge::postGesture(phase, dx, dy));
}

''',
        where="before",
        marker="Java_org_krita_android_JNIWrappers_threeFingerGesture",
    ),

    # --- build wiring -------------------------------------------------------
    Patch(
        "plugins/extensions/CMakeLists.txt",
        anchor="    add_subdirectory( spensettings )\n",
        insert="    add_subdirectory( touchgestures )\n",
        where="after",
        marker="add_subdirectory( touchgestures )",
    ),
    Patch(
        "plugins/extensions/CMakeLists.txt",
        anchor="add_subdirectory( dbexplorer )\n",
        insert="\nadd_subdirectory( dhtools )\n",
        where="after",
        marker="add_subdirectory( dhtools )",
    ),

    # --- Java side ----------------------------------------------------------
    Patch(
        "packaging/android/apk/src/org/krita/android/JNIWrappers.java",
        anchor="    public static native void openFileFromIntent(String uri);\n",
        insert='''
    /**
     * Forwards a multi-touch drag to KisAndroidGestureBridge on the native side.
     *
     * @param phase 0 = begin, 1 = update, 2 = end, 3 = cancel
     * @param dx    centroid offset from the gesture start, in dp
     * @param dy    ditto, screen-down positive
     * @return true when Krita consumed the gesture, so the Activity should not
     *         hand these events on to Qt.
     */
    public static native boolean threeFingerGesture(int phase, float dx, float dy);
''',
        where="after",
        marker="threeFingerGesture",
    ),
]

# MainActivity gets a whole block of new members and methods, so it is handled
# separately rather than as a one-line anchor.
MAIN_ACTIVITY = "packaging/android/apk/src/org/krita/android/MainActivity.java"


def strip_cr(path):
    """Convert CRLF to LF in place, for text files only."""
    if os.path.splitext(path)[1].lower() not in TEXT_SUFFIXES:
        return
    with open(path, "rb") as fh:
        data = fh.read()
    if b"\r\n" not in data:
        return
    with open(path, "wb") as fh:
        fh.write(data.replace(b"\r\n", b"\n"))


def copy_tree(src, dst, check):
    if not os.path.isdir(src):
        return f"MISSING in source: {src}"
    if check:
        return f"would copy tree {src} -> {dst}"
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    for root, _dirs, files in os.walk(dst):
        for name in files:
            strip_cr(os.path.join(root, name))
    return f"copied tree {dst}"


def copy_file(src, dst, check):
    if not os.path.isfile(src):
        return f"MISSING in source: {src}"
    if check:
        return f"would copy {src} -> {dst}"
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    strip_cr(dst)
    return f"copied {dst}"


def apply_patch(root, patch, check):
    path = os.path.join(root, patch.path)
    if not os.path.isfile(path):
        return f"MISSING target: {patch.path}"

    with open(path, encoding="utf-8") as fh:
        text = fh.read()

    if patch.marker in text:
        return f"already applied: {patch.path} ({patch.marker[:48]})"

    if patch.regex:
        matches = list(re.finditer(patch.anchor, text, re.M))
        if not matches:
            return (f"ANCHOR NOT FOUND in {patch.path}\n"
                    f"      pattern: {patch.anchor[:70]}\n"
                    f"      upstream probably moved this; needs a human")
        if len(matches) != 1:
            return f"AMBIGUOUS anchor in {patch.path} ({len(matches)} matches)"
        if check:
            return f"would patch {patch.path}"
        at = matches[0].start() if patch.where == "before" else matches[0].end()
        text = text[:at] + patch.insert + text[at:]
    else:
        if patch.anchor not in text:
            return (f"ANCHOR NOT FOUND in {patch.path}\n"
                    f"      looking for: {patch.anchor.strip().splitlines()[0][:70]}\n"
                    f"      upstream probably moved this; needs a human")

        if text.count(patch.anchor) != 1:
            return f"AMBIGUOUS anchor in {patch.path} ({text.count(patch.anchor)} matches)"

        if check:
            return f"would patch {patch.path}"

        if patch.where == "after":
            text = text.replace(patch.anchor, patch.anchor + patch.insert)
        else:
            text = text.replace(patch.anchor, patch.insert + patch.anchor)

    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    return f"patched {patch.path}"


def port_main_activity(src_root, dst_root, check):
    """MainActivity's gesture handling is a big self-contained block. Lift it
    out of the old file rather than restating it here, so there is exactly one
    copy of that code in existence."""
    src = os.path.join(src_root, MAIN_ACTIVITY)
    dst = os.path.join(dst_root, MAIN_ACTIVITY)

    if not os.path.isfile(src):
        return f"MISSING in source: {MAIN_ACTIVITY}"
    if not os.path.isfile(dst):
        return f"MISSING target: {MAIN_ACTIVITY}"

    with open(dst, encoding="utf-8") as fh:
        new_text = fh.read()
    if "threeFingerGesture" in new_text:
        return f"already applied: {MAIN_ACTIVITY}"

    with open(src, encoding="utf-8") as fh:
        old_text = fh.read().replace("\r\n", "\n")

    fields_start = old_text.find("    // --- three-finger gesture interception")
    fields_end = old_text.find("    private float displayDensity = 1f;")
    block_start = old_text.find("    /**\n     * Three-finger DRAGS are an Android-shell concern")
    block_end = old_text.find("    @Override\n    public boolean onGenericMotionEvent(MotionEvent event) {")

    if -1 in (fields_start, fields_end, block_start, block_end):
        return ("COULD NOT LOCATE the gesture block in the old MainActivity.java\n"
                "      port it by hand, or ask Claude to regenerate it")

    fields = old_text[fields_start:fields_end] + "    private float displayDensity = 1f;\n"
    methods = old_text[block_start:block_end]

    anchors = [
        ("    private boolean serviceStarted = false;\n", fields, "after"),
        ("        haveLibsLoaded = true;\n",
         "        displayDensity = getResources().getDisplayMetrics().density;\n"
         "        if (displayDensity <= 0f) {\n"
         "            displayDensity = 1f;\n"
         "        }\n", "after"),
        ("    @Override\n    public boolean onGenericMotionEvent(MotionEvent event) {",
         methods, "before"),
    ]

    for anchor, insert, where in anchors:
        if anchor not in new_text:
            return f"ANCHOR NOT FOUND in {MAIN_ACTIVITY}: {anchor.strip()[:60]}"
        if check:
            continue
        if where == "after":
            new_text = new_text.replace(anchor, anchor + insert, 1)
        else:
            new_text = new_text.replace(anchor, insert + anchor, 1)

    if check:
        return f"would patch {MAIN_ACTIVITY}"

    with open(dst, "w", encoding="utf-8") as fh:
        fh.write(new_text)
    return f"patched {MAIN_ACTIVITY}"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--from", dest="src", required=True,
                        help="the old Krita tree holding your changes")
    parser.add_argument("--to", dest="dst", required=True,
                        help="the fresh Krita checkout to apply them to")
    parser.add_argument("--check", action="store_true",
                        help="report what would happen, write nothing")
    args = parser.parse_args()

    src = os.path.abspath(os.path.expanduser(args.src))
    dst = os.path.abspath(os.path.expanduser(args.dst))

    for root, label in ((src, "source"), (dst, "target")):
        if not os.path.isfile(os.path.join(root, "CMakeLists.txt")):
            sys.exit(f"error: {label} does not look like a Krita checkout: {root}")
    if src == dst:
        sys.exit("error: --from and --to are the same tree")

    results = []
    for tree in COPY_TREES:
        results.append(copy_tree(os.path.join(src, tree), os.path.join(dst, tree), args.check))
    for path in COPY_FILES:
        results.append(copy_file(os.path.join(src, path), os.path.join(dst, path), args.check))
    for patch in PATCHES:
        results.append(apply_patch(dst, patch, args.check))
    results.append(port_main_activity(src, dst, args.check))

    print()
    problems = 0
    for line in results:
        bad = any(w in line for w in ("MISSING", "NOT FOUND", "AMBIGUOUS", "COULD NOT"))
        problems += bad
        print(("  !!  " if bad else "  ok  ") + line)

    print()
    if problems:
        print()
        print("=" * 68)
        print(f"  {problems} ITEM(S) FAILED -- the build will not link until these are fixed.")
        print("  Paste the !! lines above to Claude.")
        print("=" * 68)
        sys.exit(1)

    print("All changes applied." if not args.check else "Dry run clean; re-run without --check.")
    print(f"Now build with:  KRITA_SRC={dst} JOBS=8 bash {dst}/build-android-wsl.sh")


if __name__ == "__main__":
    main()
