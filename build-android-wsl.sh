#!/usr/bin/env bash
#
# Krita for Android — one-command build, for WSL2 (Ubuntu 22.04+) or plain Linux.
#
# This mirrors what KDE's CI actually does (build-tools/ci-scripts/android.yml),
# because that is the only path that works: Krita's Android dependencies are
# prebuilt bundles downloaded from KDE's package registry, not something you can
# assemble locally out of a stock Qt install.
#
# First run does everything: toolchain, SDK, NDK, helper repos, dependency
# download, full native build, APK. Expect 60-120 minutes and ~25 GB.
# Every run after that reuses all of it and just rebuilds what you changed,
# which is usually a couple of minutes.
#
#   ./build-android-wsl.sh              # build + package
#   ./build-android-wsl.sh --install    # ...and adb install it
#   ./build-android-wsl.sh --clean      # wipe the build dir, keep deps + SDK
#   ./build-android-wsl.sh --refresh-deps
#   ./build-android-wsl.sh --setup-only # just get the toolchain in place
#   ./build-android-wsl.sh --env        # dump the environment and exit
#   ./build-android-wsl.sh --gmic       # also build the G'MIC-Qt filters plugin
#                                       # (first time ~20-40 min; then only on change)
#
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration. Override any of these from the environment:
#   KRITA_SRC=/home/me/krita ./build-android-wsl.sh
# ---------------------------------------------------------------------------

# Your Krita checkout. Defaults to the Windows-side tree via the WSL mount, so
# you keep editing in Windows and building here. Only the source is read across
# the mount; all build output lands on the fast Linux filesystem.
KRITA_SRC="${KRITA_SRC:-/mnt/d/Users/Darren/Documents/krita}"

# Everything this script downloads or generates lives here. Delete it to start over.
WORK="${WORK:-$HOME/krita-android}"

ABI="${ABI:-arm64-v8a}"                 # arm64-v8a | armeabi-v7a | x86_64
BUILD_TYPE="${BUILD_TYPE:-Release}"     # Release | Debug  (Debug is huge and slow)
DEPS_BRANCH="${DEPS_BRANCH:-master}"    # must match your Krita branch

# Android SDK/NDK versions. Taken from packaging/android/apk/build.gradle in
# your own checkout — see check_gradle_versions() below, which will shout if
# they drift apart.
SDK_PLATFORM="${SDK_PLATFORM:-35}"
BUILD_TOOLS="${BUILD_TOOLS:-35.0.0}"
NDK_VERSION="${NDK_VERSION:-27.3.13750724}"

JOBS="${JOBS:-$(nproc)}"

# Where the finished APK gets copied so Windows Explorer can see it. Defaults to
# an "apk" folder next to the source, which on a /mnt/... checkout means a normal
# Windows path rather than something buried inside the WSL virtual disk.
OUT_DIR="${OUT_DIR:-$KRITA_SRC/apk}"

# ---------------------------------------------------------------------------

TOOLS="$WORK/tools"
SDK="$TOOLS/sdk"
HELPERS="$WORK/helpers"
DEPS_REPO="$HELPERS/krita-deps-management"
VENV="$WORK/venv"
CACHE="$WORK/package-cache/android-$ABI-qt5.15"
WD="$WORK/wd"
STAMPS="$WORK/.stamps"

DO_INSTALL=0
DO_CLEAN=0
REFRESH_DEPS=0
SETUP_ONLY=0
DUMP_ENV=0
BUILD_GMIC="${BUILD_GMIC:-0}"
# gmic.cpp alone wants several GB of RAM to compile; keep this low
GMIC_JOBS="${GMIC_JOBS:-6}"

for arg in "$@"; do
    case "$arg" in
        --install)       DO_INSTALL=1 ;;
        --clean)         DO_CLEAN=1 ;;
        --refresh-deps)  REFRESH_DEPS=1 ;;
        --setup-only)    SETUP_ONLY=1 ;;
        --env)           DUMP_ENV=1 ;;
        --gmic)          BUILD_GMIC=1 ;;
        -h|--help)       sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "Unknown option: $arg (try --help)" >&2; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------

if [ -t 1 ]; then
    C_STEP=$'\e[1;36m'; C_OK=$'\e[1;32m'; C_WARN=$'\e[1;33m'; C_ERR=$'\e[1;31m'; C_OFF=$'\e[0m'
else
    C_STEP=""; C_OK=""; C_WARN=""; C_ERR=""; C_OFF=""
fi

step() { echo; echo "${C_STEP}==> $*${C_OFF}"; }
ok()   { echo "${C_OK}    ok: $*${C_OFF}"; }
warn() { echo "${C_WARN}    warning: $*${C_OFF}" >&2; }
die()  { echo "${C_ERR}error: $*${C_OFF}" >&2; exit 1; }

# A step that only runs once. Delete $WORK/.stamps/<name> to force a redo.
stamped() {
    local name="$1"; shift
    if [ -f "$STAMPS/$name" ]; then
        ok "$name (already done)"
        return 0
    fi
    "$@"
    mkdir -p "$STAMPS"
    touch "$STAMPS/$name"
}

# ---------------------------------------------------------------------------
# Sanity
# ---------------------------------------------------------------------------

preflight() {
    step "Checking where we are"

    [ -d "$KRITA_SRC" ] || die "Krita source not found at: $KRITA_SRC
    Set KRITA_SRC=/path/to/krita and try again."
    [ -f "$KRITA_SRC/CMakeLists.txt" ] || die "$KRITA_SRC does not look like a Krita checkout."
    [ -f "$KRITA_SRC/build-tools/ci-scripts/build-android-package.py" ] || \
        die "$KRITA_SRC is missing build-tools/ci-scripts/build-android-package.py — is this Krita master?"

    if ! grep -q microsoft /proc/version 2>/dev/null; then
        warn "This does not look like WSL. That is fine on real Linux; ignore this."
    fi

    case "$KRITA_SRC" in
        /mnt/*)
            warn "Source lives on a Windows drive. That works, but compiling reads
    thousands of files across the mount and is noticeably slower. If the wait
    annoys you, clone into the Linux filesystem instead and set KRITA_SRC."
            ;;
    esac

    local free_gb
    free_gb=$(df -BG --output=avail "$HOME" | tail -1 | tr -dc '0-9')
    if [ "${free_gb:-999}" -lt 30 ]; then
        warn "Only ${free_gb}G free in \$HOME. A first build wants about 25G."
    fi

    ok "source: $KRITA_SRC"
    ok "work:   $WORK"
    ok "abi:    $ABI ($BUILD_TYPE)"
}

# Krita's own build.gradle is the authority on SDK/NDK versions for your
# checkout. If it has moved on, say so loudly instead of building against the
# wrong NDK and failing forty minutes later with a linker error.
check_gradle_versions() {
    local gradle="$KRITA_SRC/packaging/android/apk/build.gradle"
    [ -f "$gradle" ] || return 0

    local want_ndk want_bt
    want_ndk=$(grep -oP 'ndkVersion\s+"\K[^"]+' "$gradle" | head -1 || true)
    want_bt=$(grep -oP 'buildToolsVersion\s+"\K[^"]+' "$gradle" | head -1 || true)

    if [ -n "$want_ndk" ] && [ "$want_ndk" != "$NDK_VERSION" ]; then
        warn "build.gradle wants NDK $want_ndk, this script is set to $NDK_VERSION.
    Using build.gradle's value. Override with NDK_VERSION=... if that is wrong."
        NDK_VERSION="$want_ndk"
    fi
    if [ -n "$want_bt" ] && [ "$want_bt" != "$BUILD_TOOLS" ]; then
        warn "build.gradle wants build-tools $want_bt; using that."
        BUILD_TOOLS="$want_bt"
    fi
}

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------

install_packages() {
    step "Installing Linux build prerequisites (sudo)"
    sudo apt-get update -qq
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl unzip zip rsync ccache pkg-config \
        python3 python3-venv python3-pip python3-dev \
        openjdk-17-jdk-headless \
        gettext libtool autoconf automake bison flex \
        > /dev/null
    ok "packages installed"
}

install_sdk() {
    step "Installing the Android SDK command-line tools"
    mkdir -p "$SDK/cmdline-tools"
    local zip="$WORK/cmdline-tools.zip"
    # Google publishes these under a rolling filename; this one is the current
    # linux build. If it 404s, grab the URL from developer.android.com/studio.
    curl -fL --retry 3 -o "$zip" \
        "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip"
    rm -rf "$SDK/cmdline-tools/latest" "$SDK/cmdline-tools/cmdline-tools"
    unzip -q "$zip" -d "$SDK/cmdline-tools"
    mv "$SDK/cmdline-tools/cmdline-tools" "$SDK/cmdline-tools/latest"
    rm -f "$zip"
    ok "cmdline-tools installed"
}

install_sdk_packages() {
    step "Installing SDK platform $SDK_PLATFORM, build-tools $BUILD_TOOLS, NDK $NDK_VERSION"
    local sdkmanager="$SDK/cmdline-tools/latest/bin/sdkmanager"
    [ -x "$sdkmanager" ] || die "sdkmanager missing — delete $STAMPS/sdk-tools and re-run."

    yes | "$sdkmanager" --sdk_root="$SDK" --licenses > /dev/null 2>&1 || true
    "$sdkmanager" --sdk_root="$SDK" \
        "platform-tools" \
        "platforms;android-$SDK_PLATFORM" \
        "build-tools;$BUILD_TOOLS" \
        "ndk;$NDK_VERSION"

    [ -d "$SDK/ndk/$NDK_VERSION" ] || die "NDK $NDK_VERSION did not install."
    ok "SDK and NDK in place"
}

clone_or_update() {
    local url="$1" dir="$2" branch="$3"
    if [ -d "$dir/.git" ]; then
        git -C "$dir" fetch --depth=1 origin "$branch" --quiet
        git -C "$dir" reset --hard "origin/$branch" --quiet
    else
        git clone --depth=1 -b "$branch" "$url" "$dir" --quiet
    fi
}

install_helpers() {
    step "Fetching KDE's dependency-management and CI helper repos"
    mkdir -p "$HELPERS"
    clone_or_update https://invent.kde.org/packaging/krita-deps-management.git \
                    "$DEPS_REPO" "$DEPS_BRANCH"
    clone_or_update https://invent.kde.org/packaging/krita-ci-utilities.git \
                    "$DEPS_REPO/ci-utilities" master
    ok "helpers ready"
}

install_venv() {
    step "Creating the Python environment"
    python3 -m venv "$VENV" --upgrade-deps > /dev/null
    # shellcheck disable=SC1091
    source "$VENV/bin/activate"
    pip install --quiet -r "$DEPS_REPO/requirements.txt"
    deactivate
    ok "venv ready at $VENV"
}

# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------

# The package registry and toolchain filename have both changed name over the
# years, so read them out of the repo we just cloned instead of hardcoding.
detect_package_project() {
    local found=""
    # Search every pipeline file, not just seed-deps.yml, and match on the ABI
    # alone -- the "-qt5.15" suffix has come and gone over the years.
    if [ -d "$DEPS_REPO/pipelines" ]; then
        found=$(grep -rhoP "KDECI_PACKAGE_PROJECT:\s*\K\S+" "$DEPS_REPO/pipelines" 2>/dev/null \
                | grep -F -- "android" | grep -F -- "$ABI" | head -1 || true)
    fi

    if [ -n "$found" ]; then
        echo "$found"
        return 0
    fi

    # No silent fallback. A wrong registry name shows up as an opaque
    # "404 Project Not Found" twenty seconds into the build, which tells you
    # nothing. Show what actually exists and stop.
    echo >&2
    echo "${C_ERR}error: could not work out which package registry holds the${C_OFF}" >&2
    echo "${C_ERR}       prebuilt $ABI dependencies.${C_OFF}" >&2
    echo >&2
    echo "  KDECI_PACKAGE_PROJECT values found in $DEPS_REPO/pipelines:" >&2
    grep -rhoP "KDECI_PACKAGE_PROJECT:\s*\K\S+" "$DEPS_REPO/pipelines" 2>/dev/null \
        | sort -u | sed 's/^/    /' >&2 || echo "    (none at all — did the clone work?)" >&2
    echo >&2
    echo "  Pick the one matching $ABI and re-run with:" >&2
    echo "    KDECI_PACKAGE_PROJECT=<value> bash build-android-wsl.sh" >&2
    echo >&2
    exit 1
}

detect_toolchain_file() {
    for candidate in android-toolchain-krita.cmake android-toolchain.cmake; do
        if [ -f "$DEPS_REPO/tools/$candidate" ]; then
            echo "$DEPS_REPO/tools/$candidate"
            return 0
        fi
    done
    die "No Android toolchain file in $DEPS_REPO/tools — did the clone succeed?"
}

setup_env() {
    export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}"
    [ -d "$JAVA_HOME" ] || die "JDK 17 not found at $JAVA_HOME. Krita's Gradle build needs exactly 17."

    export ANDROID_HOME="$SDK"
    export ANDROID_SDK_ROOT="$SDK"
    export KDECI_ANDROID_SDK_ROOT="$SDK"
    export KDECI_ANDROID_NDK_ROOT="$SDK/ndk/$NDK_VERSION"
    export ANDROID_NDK_ROOT="$KDECI_ANDROID_NDK_ROOT"

    export PATH="$JAVA_HOME/bin:$SDK/platform-tools:$SDK/cmdline-tools/latest/bin:$PATH"

    export KDECI_ANDROID_ABI="$ABI"
    export ANDROID_ABI="$ABI"
    export KDECI_BUILD_TYPE="$BUILD_TYPE"

    export KDECI_GITLAB_SERVER="https://invent.kde.org/"
    export KDECI_PACKAGE_PROJECT="${KDECI_PACKAGE_PROJECT:-$(detect_package_project)}"
    export KDECI_CACHE_PATH="$CACHE"
    export KDECI_REPO_METADATA_PATH="$DEPS_REPO/repo-metadata"
    export KDECI_COMPRESS_PACKAGES_ON_DOWNLOAD=False

    # Krita ships its own Android toolchain; ECM's would fight with it.
    export KDECI_SKIP_ECM_ANDROID_TOOLCHAIN=True
    export CMAKE_TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN_FILE:-$(detect_toolchain_file)}"

    # androiddeployqt refuses to see Qt's QML modules if _install sits beside the
    # source root, so the build and install trees go somewhere else entirely.
    export KDECI_WORKDIR_PATH="$WD"

    export KDECI_EXTRA_CMAKE_ARGS="-DHIDE_SAFE_ASSERTS=OFF -DBUILD_TESTING=OFF"
    export KRITACI_ANDROID_PACKAGE_TYPE=debug

    # build-android-package.py wants a short sha for the package name.
    export CI_COMMIT_SHORT_SHA="${CI_COMMIT_SHORT_SHA:-$(git -C "$KRITA_SRC" log --pretty=%h -n 1 2>/dev/null || echo local)}"

    mkdir -p "$CACHE" "$WD"
}

print_env() {
    echo
    echo "  KRITA_SRC                 $KRITA_SRC"
    echo "  JAVA_HOME                 $JAVA_HOME"
    echo "  ANDROID_HOME              $ANDROID_HOME"
    echo "  KDECI_ANDROID_NDK_ROOT    $KDECI_ANDROID_NDK_ROOT"
    echo "  CMAKE_TOOLCHAIN_FILE      $CMAKE_TOOLCHAIN_FILE"
    echo "  KDECI_PACKAGE_PROJECT     $KDECI_PACKAGE_PROJECT"
    echo "  KDECI_CACHE_PATH          $KDECI_CACHE_PATH"
    echo "  KDECI_WORKDIR_PATH        $KDECI_WORKDIR_PATH"
    echo "  build dir                 $WD/krita/_build"
    echo "  deps install dir          $WD/krita/_install"
    echo
}

# ---------------------------------------------------------------------------
# The build itself
# ---------------------------------------------------------------------------

# run-ci-build.py reads the project's dependency list out of .kde-ci.yml in the
# source root, and the generator overwrites it. That file is tracked in git, so
# keep a pristine copy the first time and tell the user how to put it back.
generate_deps_file() {
    step "Generating the dependency list"
    # shellcheck disable=SC1091
    source "$VENV/bin/activate"

    local pristine="$WORK/kde-ci.yml.pristine"
    if [ ! -f "$pristine" ] && [ -f "$KRITA_SRC/.kde-ci.yml" ]; then
        cp "$KRITA_SRC/.kde-ci.yml" "$pristine"
        warn "Krita's tracked .kde-ci.yml is about to be overwritten with the
    generated dependency list — this is what CI does too. The original is saved
    at $pristine, and 'git checkout .kde-ci.yml' restores it."
    fi

    local seed="$DEPS_REPO/latest/krita-deps.yml"
    local corrected="$WORK/branch-corrected-deps.yml"

    if [ -f "$DEPS_REPO/tools/replace-branch-in-seed-file.py" ]; then
        python3 -u "$DEPS_REPO/tools/replace-branch-in-seed-file.py" \
            "$seed" -p -o "$corrected" -d "$DEPS_BRANCH"
    else
        cp "$seed" "$corrected"
    fi

    python3 -u "$DEPS_REPO/tools/generate-deps-file.py" \
        -s "$corrected" -o "$KRITA_SRC/.kde-ci.yml"

    deactivate
    ok "dependency list written"
}

# CMake bakes the source path into its cache, so pointing KRITA_SRC at a
# different tree makes every later run die with "does not match the source used
# to generate cache". The build dir is disposable and the deps are not, so just
# clear the stale one instead of making the user work that out.
check_stale_build_dir() {
    local cache="$WD/krita/_build/CMakeCache.txt"
    [ -f "$cache" ] || return 0

    local cached
    cached=$(grep -m1 '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$cache" | cut -d= -f2- || true)
    [ -n "$cached" ] || return 0

    if [ "$(readlink -f "$cached")" != "$(readlink -f "$KRITA_SRC")" ]; then
        warn "Build dir was configured for a different source tree:
      was: $cached
      now: $KRITA_SRC
    Clearing the build dir. Dependencies and the SDK are untouched, so this
    costs a recompile, not another download."
        rm -rf "$WD/krita/_build" "$WD/krita/_packaging"
    fi
}

run_build() {
    step "Downloading dependencies and building Krita ($JOBS jobs)"
    echo "    First time through this pulls Qt, KF5 and ~60 libraries as prebuilt"
    echo "    bundles, then compiles Krita. Go and do something else for a while."
    # shellcheck disable=SC1091
    source "$VENV/bin/activate"
    cd "$KRITA_SRC"
    python3 -u "$DEPS_REPO/ci-utilities/run-ci-build.py" \
        --project krita \
        --branch "$DEPS_BRANCH" \
        --platform "Android/$ABI/Qt5/Shared" \
        --only-build \
        --skip-publishing \
        2>&1 | tee "$WORK/build-krita.log"
    deactivate
    ok "native build finished"
}

# Git on Windows checks the tree out with CRLF by default (core.autocrlf=true).
# Compilers do not care, so a full build sails through and then androiddeployqt
# dies with "/usr/bin/env: 'sh\r': No such file or directory" because Linux
# cannot exec a script whose shebang ends in a carriage return. Cheap to check,
# miserable to diagnose, so we just fix it every run.
fix_line_endings() {
    local changed=0
    local f
    for f in "$KRITA_SRC/packaging/android/apk/gradlew" \
             "$KRITA_SRC/packaging/android/apk/gradlew.bat"; do
        [ -f "$f" ] || continue
        case "$f" in *.bat) continue ;; esac   # .bat must stay CRLF
        if grep -qU $'\r' "$f" 2>/dev/null; then
            sed -i 's/\r$//' "$f"
            chmod +x "$f"
            changed=1
        fi
    done
    if [ "$changed" = 1 ]; then
        warn "Stripped Windows line endings from gradlew. If this keeps coming
    back, run 'git config core.autocrlf false' in the source tree."
    fi
}

run_package() {
    step "Packaging the APK"
    # shellcheck disable=SC1091
    source "$VENV/bin/activate"
    cd "$KRITA_SRC"
    python3 -u build-tools/ci-scripts/build-android-package.py \
        2>&1 | tee "$WORK/build-apk.log"
    deactivate
}

find_apk() {
    find "$WD/krita/_packaging" -name '*.apk' -print -quit 2>/dev/null
}

do_install() {
    local apk="$1"
    step "Installing on the connected device"
    if ! adb get-state > /dev/null 2>&1; then
        warn "No device visible to adb.

    In WSL, adb cannot see USB devices directly. Either:
      * run 'adb install -r' from Windows against the path below, or
      * use wireless debugging: enable it on the tablet, then
          adb connect <tablet-ip>:<port>
        from inside WSL and re-run with --install."
        return 0
    fi
    adb install -r "$apk"
    ok "installed"
}


# ---------------------------------------------------------------------------
# G'MIC-Qt (DH-GMIC)
#
# Krita's own G'MIC host (the Filter > Start G'MIC-Qt menu and
# libkritaqmicinterface) is already part of the Android build. What KDE's CI
# skips on Android is the gmic-qt plugin itself (build-plugins.py exits early).
# This builds it from Krita's own 3rdparty_plugins/ext_gmic recipe against the
# Krita we just installed, and drops krita_gmic_qt.so into _install/lib, where
# Gradle picks it up and renames it lib_krita_gmic_qt.so - the name Krita's
# plugin loader looks for on Android.
# ---------------------------------------------------------------------------
build_gmic() {
    step "Building the G'MIC-Qt plugin for Android ($GMIC_JOBS jobs)"

    local install="$WD/krita/_install"
    local cache="$WD/krita/_build/CMakeCache.txt"
    [ -f "$cache" ] || die "Krita has not been configured yet ($cache missing). Run a normal build first."
    # Krita's G'MIC host library. On Android it may carry an ABI suffix
    # (libkritaqmicinterface_arm64-v8a.so), so look rather than assume.
    local qmiclib qmicinc
    qmiclib="$(find "$install/lib" "$WD/krita/_build" -maxdepth 4 -name 'libkritaqmicinterface*.so' 2>/dev/null | head -1)"
    qmicinc="$(find "$install/include" "$WD/krita/_build" -maxdepth 4 -name 'kis_qmic_plugin_interface.h' -printf '%h\n' 2>/dev/null | head -1)"
    if [ -z "$qmiclib" ] || [ -z "$qmicinc" ]; then
        echo "  library: ${qmiclib:-NOT FOUND}"
        echo "  header:  ${qmicinc:-NOT FOUND}"
        echo "  anything qmic-related that did get built:"
        find "$install" "$WD/krita/_build" -iname '*qmic*' 2>/dev/null | head -20 | sed 's/^/    /'
        die "Krita's G'MIC host (kritaqmicinterface) is missing - send Claude the list above."
    fi
    ok "host library: $qmiclib"
    ok "host headers: $qmicinc"

    getc() { grep -m1 "^$1:" "$cache" | cut -d= -f2- || true; }

    # Build the plugin with exactly the settings Krita was built with.
    local platform stl findroot
    platform="$(getc ANDROID_PLATFORM)"
    if [ -z "$platform" ]; then
        local api; api="$(getc ANDROID_NATIVE_API_LEVEL)"
        [ -z "$api" ] && api="$(getc ANDROID_API_LEVEL)"
        platform="android-${api:-23}"
    fi
    stl="$(getc ANDROID_STL)"; stl="${stl:-c++_shared}"
    findroot="$(getc CMAKE_FIND_ROOT_PATH)"
    findroot="${findroot:+$findroot;}$install"
    ok "platform $platform, stl $stl"

    # gmic-qt insists on Qt's translation tool. Use the Android Qt's if it has
    # one, otherwise the host's (it only runs at build time).
    local linguist=""
    if [ ! -d "$install/lib/cmake/Qt5LinguistTools" ]; then
        if [ ! -f /usr/lib/x86_64-linux-gnu/cmake/Qt5LinguistTools/Qt5LinguistToolsConfig.cmake ]; then
            step "Installing Qt's translation tools for the host (sudo)"
            sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
                qttools5-dev qttools5-dev-tools > /dev/null
        fi
        linguist="-DQt5LinguistTools_DIR=/usr/lib/x86_64-linux-gnu/cmake/Qt5LinguistTools"
        ok "using host lrelease"
    fi

    # FFTW speeds up some filters; only use it if the deps bundle ships it.
    local fftw=OFF
    if ls "$install"/lib/libfftw3*.so "$install"/lib/libfftw3*.a > /dev/null 2>&1; then fftw=ON; fi
    ok "FFTW3: $fftw"

    # Extra arguments handed straight to gmic-qt's own CMake.
    local extra="-DCMAKE_BUILD_TYPE=Release;-DENABLE_CURL=OFF;-DENABLE_ASAN=OFF;-DENABLE_FFTW3=$fftw"
    extra="$extra;-DANDROID_STL=$stl;-DCMAKE_PREFIX_PATH=$install"
    extra="$extra;-DKIS_IMAGE_INTERFACE_LIBRARY=$qmiclib;-DKIS_IMAGE_INTERFACE_DIR=$qmicinc"
    # Android's pthread.h has no _PTHREAD_H include guard (bionic uses #pragma
    # once), so CImg concludes pthreads are absent: it then compiles its mutexes
    # out and calls syscall(SYS_gettid), which bionic doesn't declare. Tell it
    # the truth - pthreads are there and already included by gmic.h.
    extra="$extra;-DCMAKE_CXX_FLAGS=-D_PTHREAD_H"
    if [ -n "$linguist" ]; then extra="$extra;$linguist"; fi

    local build="$WORK/gmic-build"
    mkdir -p "$build" "$WORK/downloads"

    # gmic-qt's configure step makes its translation .qm files by calling
    # `lrelease` straight off PATH (not the Qt tool CMake found). Ubuntu keeps
    # it in /usr/lib/qt5/bin, which is not on PATH, so put it there for this step.
    local lrel=""
    for c in /usr/lib/qt5/bin/lrelease /usr/lib/x86_64-linux-gnu/qt5/bin/lrelease "$(command -v lrelease-qt5 || true)" "$(command -v lrelease || true)"; do
        if [ -n "$c" ] && [ -x "$c" ]; then lrel="$c"; break; fi
    done
    if [ -z "$lrel" ]; then
        step "Installing Qt's lrelease for the host (sudo)"
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends qttools5-dev-tools > /dev/null
        for c in /usr/lib/qt5/bin/lrelease /usr/lib/x86_64-linux-gnu/qt5/bin/lrelease; do
            if [ -x "$c" ]; then lrel="$c"; break; fi
        done
    fi
    [ -n "$lrel" ] || die "Could not find or install lrelease (package qttools5-dev-tools)."
    export PATH="$(dirname "$lrel"):$PATH"
    ok "lrelease: $lrel"

    # A configure that ran without lrelease leaves no .qm files behind and is
    # never re-run by itself - force it.
    local gsrc="$build/ext_gmic/ext_gmic-prefix/src/ext_gmic/gmic-qt"
    if [ -d "$gsrc" ] && [ ! -f "$gsrc/translations/filters/ru.qm" ]; then
        rm -f "$build/ext_gmic/ext_gmic-prefix/src/ext_gmic-stamp/ext_gmic-configure"
        ok "re-running G'MIC configure (translations were missing)"
    fi

    # -static-openmp: the NDK would otherwise link libomp.so, which is not in
    # the APK, and the plugin would silently fail to load on the tablet.
    ( cd "$build" && cmake -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
        -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM="$platform" \
        -DANDROID_STL="$stl" \
        -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
        -DCMAKE_FIND_ROOT_PATH="$findroot" \
        -DCMAKE_PREFIX_PATH="$install" \
        -DCMAKE_SHARED_LINKER_FLAGS="-static-openmp" \
        -DCMAKE_MODULE_LINKER_FLAGS="-static-openmp" \
        -DINSTALL_ROOT="$install" \
        -DEXTERNALS_DOWNLOAD_DIR="$WORK/downloads" \
        -DEXT_GMIC_EXTRA_ARGS="$extra" \
        "$KRITA_SRC/3rdparty_plugins" ) 2>&1 | tee "$WORK/build-gmic.log"
    [ "${PIPESTATUS[0]}" = 0 ] || die "G'MIC configure failed - see $WORK/build-gmic.log"

    ( cd "$build" && CMAKE_BUILD_PARALLEL_LEVEL="$GMIC_JOBS" \
        cmake --build . --target ext_gmic --parallel "$GMIC_JOBS" ) 2>&1 | tee -a "$WORK/build-gmic.log"
    [ "${PIPESTATUS[0]}" = 0 ] || die "G'MIC build failed - see $WORK/build-gmic.log
    (inner logs: $build/ext_gmic-prefix/src/ext_gmic-stamp/*.log)"

    # Qt5 on Android names modules with an ABI suffix (krita_gmic_qt_arm64-v8a.so),
    # same as Krita's own plugins; Gradle renames both to lib_krita*.so.
    local gso
    gso="$(ls "$install"/lib/krita_gmic_qt*.so 2>/dev/null | head -1)"
    [ -n "$gso" ] || \
        die "G'MIC built but no krita_gmic_qt*.so in $install/lib - see $WORK/build-gmic.log"

    if readelf -d "$gso" | grep -q 'libomp.so'; then
        die "$(basename "$gso") still needs libomp.so, so it would not load on Android.
    -static-openmp did not take effect - send Claude $WORK/build-gmic.log"
    fi
    ok "$(basename "$gso") ($(du -h "$gso" | cut -f1)) is in place, no libomp.so dependency"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

mkdir -p "$WORK" "$STAMPS"

preflight
check_gradle_versions

if [ "$DO_CLEAN" = 1 ]; then
    step "Cleaning the build tree (dependencies and SDK are kept)"
    rm -rf "$WD/krita/_build" "$WD/krita/_packaging"
    ok "cleaned"
fi

stamped packages     install_packages
stamped sdk-tools    install_sdk
stamped sdk-packages install_sdk_packages
install_helpers                       # always refreshed; it is a shallow clone
stamped venv         install_venv

setup_env

if [ "$DUMP_ENV" = 1 ]; then
    print_env
    exit 0
fi

if [ "$REFRESH_DEPS" = 1 ]; then
    rm -f "$STAMPS/deps-file"
    rm -rf "$WD/krita/_install"
fi

stamped deps-file generate_deps_file

if [ "$SETUP_ONLY" = 1 ]; then
    print_env
    step "Setup complete"
    echo "    Re-run without --setup-only to build."
    exit 0
fi

print_env
check_stale_build_dir
run_build
if [ "$BUILD_GMIC" = 1 ]; then build_gmic; fi
fix_line_endings
run_package

APK="$(find_apk || true)"
if [ -z "$APK" ]; then
    die "The build finished but no APK turned up in $WD/krita/_packaging.
    Check $WORK/build-apk.log."
fi

# Drop a copy somewhere reachable from Windows. Everything the build produces
# otherwise lives inside the WSL virtual disk, which is a pain to find from
# Explorer and worse to explain over a chat window.
mkdir -p "$OUT_DIR"
cp -f "$APK" "$OUT_DIR/"
COPIED="$OUT_DIR/$(basename "$APK")"

# Translate /mnt/d/foo into D:\foo so the path can be pasted into Explorer.
windows_path() {
    local p="$1"
    if [[ "$p" =~ ^/mnt/([a-z])/(.*)$ ]]; then
        local drive="${BASH_REMATCH[1]}"
        local rest="${BASH_REMATCH[2]}"
        echo "${drive^^}:\\${rest//\//\\}"
    else
        echo "\\\\wsl.localhost\\$(grep -oP '^NAME="?\K[^"]+' /etc/os-release 2>/dev/null | head -1 | tr ' ' '-')${p//\//\\}"
    fi
}

echo
echo "${C_OK}=====================================================${C_OFF}"
echo "${C_OK} Build complete${C_OFF}"
echo "${C_OK}=====================================================${C_OFF}"
echo
echo "  APK:       $(basename "$APK")"
echo "  Size:      $(du -h "$APK" | cut -f1)"
echo
echo "  In Windows Explorer, paste this into the address bar:"
echo "    $(windows_path "$OUT_DIR")"
echo
echo "  Or install straight over adb:"
echo "    adb install -r \"$COPIED\""
echo
echo "  Installs as org.krita.debug, alongside any Krita you already have."
echo "  Logs: $WORK/build-krita.log, $WORK/build-apk.log"
echo

[ "$DO_INSTALL" = 1 ] && do_install "$COPIED"

exit 0
