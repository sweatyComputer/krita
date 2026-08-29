 /*
  * This file is part of the KDE project
  * SPDX-FileCopyrightText: 2019 Sharaf Zaman <sharafzaz121@gmail.com>
  *
  * SPDX-License-Identifier: GPL-2.0-or-later
  */

package org.krita.android;

class JNIWrappers {
    public static native void saveState();
    public static native boolean hasMainWindowLoaded();
    public static native void donationStateUpdated(int state, long ownedProductFlags);
    public static native void openFileFromIntent(String uri);

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
    public static native void showDonationManagementDialog();
    public static native void setPrimaryScreenScale(double scale);
    public static native void savePrimaryScreenScale(boolean save);
    public static native void onSplashDialogDismissed();
    public static native void onScalingDialogShown();
    public static native void onScalingDialogDismissed();
}

