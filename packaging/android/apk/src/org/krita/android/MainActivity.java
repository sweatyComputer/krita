/*
 * This file is part of the KDE project
 * SPDX-FileCopyrightText: 2019 Sharaf Zaman <sharafzaz121@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

package org.krita.android;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.app.ApplicationExitInfo;
import android.app.ForegroundServiceStartNotAllowedException;
import android.app.ServiceStartNotAllowedException;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewConfiguration;
import android.widget.Toast;

import androidx.annotation.RequiresApi;

import java.util.List;

import org.krita.R;
import org.libsdl.app.SDLAudioManager;
import org.qtproject.qt5.android.QtNative;
import org.qtproject.qt5.android.bindings.QtActivity;

import java.util.function.BiConsumer;
import java.util.function.Consumer;

public class MainActivity extends QtActivity {

    private static final String TAG = "krita.MainActivity";
    private static boolean applicationLoaded = false;
    private static String applicationLoadingText = "";
    private boolean haveLibsLoaded = false;
    private boolean serviceStarted = false;
    // --- three-finger gesture interception ---------------------------------
    // Mirrors KisAndroidGestureBridge::Phase on the native side.
    private static final int GESTURE_BEGIN  = 0;
    private static final int GESTURE_UPDATE = 1;
    private static final int GESTURE_END    = 2;
    private static final int GESTURE_CANCEL = 3;

    private static final int GESTURE_FINGERS = 3;

    // How far three fingers must travel before we decide this is a drag and
    // not a tap, in dp. Krita's own input system already binds three-finger
    // TAPS to actions, so taking over at touch-down would quietly break them.
    private static final float GESTURE_TAKEOVER_SLOP_DP = 8f;

    // "tracking" means the native side accepted the gesture and wants deltas.
    // "swallowing" means we keep eating touch events until the hand leaves the
    // glass, so Qt never sees a half-finished pointer sequence. If the native
    // side declines the gesture (disabled in settings, no main window yet) we
    // set neither and stay out of the way completely.
    private boolean gestureWatching = false;
    private boolean gestureTracking = false;
    private boolean gestureSwallowing = false;
    private float gestureStartX = 0f;
    private float gestureStartY = 0f;
    private float displayDensity = 1f;
    private boolean inFullScreen = false;
    private DonationDialog mDonationDialog = null;

    @Override
    @SuppressLint("MissingSuperCall")
    public void onCreate(Bundle savedInstanceState) {
        super.QT_ANDROID_DEFAULT_THEME = "DefaultTheme";

        // we have to do this before loading main()
        Intent i = getIntent();
        String uri = getUri(i);
        if (uri != null) {
            // this will be passed as a command line argument to main()
            i.putExtra("applicationArguments", uri);
        }

        SDLAudioManager.initialize();
        SDLAudioManager.setContext(this);
        SDLAudioManager.nativeSetupJNI();

        super.onCreate(savedInstanceState);
        Log.i(TAG, "TouchSlop: " + ViewConfiguration.get(this).getScaledTouchSlop());
        Log.i(TAG, "LibsLoaded");
        haveLibsLoaded = true;
        displayDensity = getResources().getDisplayMetrics().density;
        if (displayDensity <= 0f) {
            displayDensity = 1f;
        }

        DonationHelper.getInstance();
        DonationProduct.initAllProducts(this);
    }

    @Override
    public void onStart() {
        super.onStart();

        // unlike onCreate where we did this before, this method is called several times throughout the
        // lifecycle of our app, but we intend to run this method only once (and in "Foreground").
        if (!serviceStarted) {
            serviceStarted  = true;
            // Full-screening the application here instead of after the main window is shown avoids
            // some ugly flicker as the Qt UI resizes itself.
            trySetFullScreen(true);
            // Keep the service started so in an unfortunate case where we're not allowed to start a
            // foreground service, we can try to continue without it.
            Intent docSaverServiceIntent = new Intent(this, DocumentSaverService.class);
            startService(docSaverServiceIntent);
        }
    }

    @Override
    protected void onNewIntent (Intent intent) {
        String uri = getUri(intent);
        if (uri != null) {
            JNIWrappers.openFileFromIntent(uri);
        }

        super.onNewIntent(intent);
    }

    private String getUri(Intent intent) {
        if (intent != null) {
            Uri fileUri = intent.getData();
            if (fileUri != null) {
                return fileUri.toString();
            }
        }
        return null;
    }

    @Override
    public void onPause() {
        super.onPause();
        // onPause() _is_ called when the app starts. If the native lib
        // isn't loaded, it crashes.
        if (haveLibsLoaded) {
            synchronized(this) {
                startServiceGeneric(DocumentSaverService.START_SAVING);
            }
        }
    }

    void startServiceGeneric(final String action) {
        Intent intent = new Intent(this, DocumentSaverService.class);
        intent.putExtra(action, true);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            startForegroundServiceS(intent);
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
    }

    @RequiresApi(api = Build.VERSION_CODES.S)
    void startForegroundServiceS(Intent intent) {
        try {
            try {
                startForegroundService(intent);
            } catch (ForegroundServiceStartNotAllowedException e) {
                Log.w(TAG, "ForegroundServiceStartNotAllowedException: " + e);

                // The service is already running, so maybe try saving without trying to put it in
                // foreground. According to docs we should have a couple of minutes of runtime.
                startService(intent);
            }
        } catch (ServiceStartNotAllowedException e) {
            // We may not be allowed to start a background service either,
            // probably because onPause is called on an already-paused
            // application that is beyond the "couple of minutes" cutoff.
            Log.w(TAG, "ServiceStartNotAllowedException: " + e);
        }
    }

    @Override
    public void onDestroy() {
        // Docs say: this method will not be called if the activity's hosting process
        // is killed. This means, for us that the service has been stopped.

        Log.i(TAG, "[onDestroy]");
        startServiceGeneric(DocumentSaverService.KILL_PROCESS);

        super.onDestroy();
    }

    @Override
    public boolean onKeyUp(final int keyCode, final KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            if (!JNIWrappers.hasMainWindowLoaded()) {
                // back button was pressed during splash screen, letting this
                // propagate leaves native side in an undefined state. So, it's
                // best we finish the activity here.
                finish();
            }
        }

        return super.onKeyUp(keyCode, event);
    }

    /**
     * Three-finger DRAGS are an Android-shell concern, so we grab them here,
     * before Qt sees them. Qt has no three-finger drag recognizer of its own
     * and would just try to paint with the extra pointers.
     *
     * Three-finger TAPS are left alone: Krita's own KisTouchGestureAction
     * already binds those, and stealing them here would silently break
     * whatever the user has mapped in Canvas Input Settings. We only take the
     * gesture over once the fingers have actually travelled.
     *
     * Everything past "three fingers moved this far" is decided natively in
     * KisAndroidGestureBridge, which reads kritarc, so the mapping and the
     * sensitivity live in Krita's own settings rather than in Java.
     */
    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (haveLibsLoaded) {
            try {
                if (updateGestureState(event)) {
                    return true;
                }
            } catch (Exception | UnsatisfiedLinkError e) {
                Log.e(TAG, "Three-finger gesture handling failed", e);
                gestureWatching = false;
                gestureTracking = false;
                gestureSwallowing = false;
            }
        }
        return super.dispatchTouchEvent(event);
    }

    /**
     * @return true when the event was consumed by the gesture handler.
     */
    private boolean updateGestureState(MotionEvent event) {
        final int action = event.getActionMasked();
        final int pointerCount = event.getPointerCount();

        if (gestureSwallowing) {
            switch (action) {
            case MotionEvent.ACTION_MOVE:
                if (gestureTracking) {
                    final float dx = (centroidX(event) - gestureStartX) / displayDensity;
                    final float dy = (centroidY(event) - gestureStartY) / displayDensity;
                    JNIWrappers.threeFingerGesture(GESTURE_UPDATE, dx, dy);
                }
                return true;

            case MotionEvent.ACTION_POINTER_UP:
                // pointerCount still counts the finger that is on its way up.
                if (pointerCount - 1 < GESTURE_FINGERS) {
                    endGesture(GESTURE_END);
                }
                return true;

            case MotionEvent.ACTION_UP:
                endGesture(GESTURE_END);
                gestureSwallowing = false;
                return true;

            case MotionEvent.ACTION_CANCEL:
                endGesture(GESTURE_CANCEL);
                gestureSwallowing = false;
                return true;

            default:
                // Keep eating everything, including a fourth finger landing,
                // until the hand actually leaves the glass. Handing Qt a
                // half-finished pointer sequence is how you get phantom dabs.
                return true;
            }
        }

        // Watch from the frame where the third finger lands, but do not claim
        // anything yet -- a three-finger tap belongs to Krita's own input
        // system, and we only take over once it is clearly a drag.
        if (action == MotionEvent.ACTION_POINTER_DOWN && pointerCount == GESTURE_FINGERS) {
            gestureWatching = true;
            gestureStartX = centroidX(event);
            gestureStartY = centroidY(event);
            return false;
        }

        if (!gestureWatching) {
            return false;
        }

        if (action == MotionEvent.ACTION_MOVE && pointerCount >= GESTURE_FINGERS) {
            final float dx = (centroidX(event) - gestureStartX) / displayDensity;
            final float dy = (centroidY(event) - gestureStartY) / displayDensity;

            if (Math.max(Math.abs(dx), Math.abs(dy)) < GESTURE_TAKEOVER_SLOP_DP) {
                return false;
            }

            if (!JNIWrappers.threeFingerGesture(GESTURE_BEGIN, 0f, 0f)) {
                // Native side is not interested; stop asking for this gesture.
                gestureWatching = false;
                return false;
            }

            gestureTracking = true;
            gestureSwallowing = true;

            // Qt has a stroke in flight from the first finger. Cancel it so we
            // do not leave a stray dab where the takeover happened.
            MotionEvent cancel = MotionEvent.obtain(event);
            cancel.setAction(MotionEvent.ACTION_CANCEL);
            super.dispatchTouchEvent(cancel);
            cancel.recycle();

            JNIWrappers.threeFingerGesture(GESTURE_UPDATE, dx, dy);
            return true;
        }

        if (action == MotionEvent.ACTION_UP
            || action == MotionEvent.ACTION_CANCEL
            || (action == MotionEvent.ACTION_POINTER_UP && pointerCount - 1 < GESTURE_FINGERS)) {
            // Fingers left without travelling: it was a tap, and it is not ours.
            gestureWatching = false;
        }

        return false;
    }

    private void endGesture(int phase) {
        gestureWatching = false;
        if (!gestureTracking) {
            return;
        }
        gestureTracking = false;
        JNIWrappers.threeFingerGesture(phase, 0f, 0f);
    }

    private float centroidX(MotionEvent event) {
        float sum = 0f;
        final int count = event.getPointerCount();
        for (int i = 0; i < count; i++) {
            sum += event.getX(i);
        }
        return count > 0 ? sum / count : 0f;
    }

    private float centroidY(MotionEvent event) {
        float sum = 0f;
        final int count = event.getPointerCount();
        for (int i = 0; i < count; i++) {
            sum += event.getY(i);
        }
        return count > 0 ? sum / count : 0f;
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        // We manually pass these events to the QPA Android because,
        // android doesn't send events of type other than SOURCE_CLASS_POINTER
        // to the view which was just tapped. So, this view will never get to
        // QtSurface, because it doesn't claim focus.
        if (event.isFromSource(InputDevice.SOURCE_TOUCHPAD)) {
            return QtNative.getInputEventDispatcher().sendGenericMotionEvent(event, event.getDeviceId());
        }
        return super.onGenericMotionEvent(event);
    }

    public void onUserInteraction() {
    }

    public void copyAssets() {
        new ConfigsManager(this).handleAssets();
    }

    public boolean isInFullScreen() {
        return inFullScreen;
    }

    public void setFullScreenOnUiThread(boolean fullScreen) {
        if (fullScreen != inFullScreen) {
            runOnUiThread(() -> trySetFullScreen(fullScreen));
        }
    }

    private void trySetFullScreen(boolean fullScreen) {
        try {
            setFullScreen(fullScreen);
            inFullScreen = fullScreen;
        } catch (Exception | UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to set fullscreen " + fullScreen, e);
        }
    }

    public static int getLongPressTimeout() {
        try {
            return ViewConfiguration.get(QtNative.activity()).getLongPressTimeout();
        } catch (Exception|UnsatisfiedLinkError e) {
            Log.e(TAG, "Exception getting long press timeout", e);
            return 500;
        }
    }

    public static boolean looksLikeXiaomiDevice() {
        return containsXiaomi(Build.BRAND) || containsXiaomi(Build.MANUFACTURER);
    }

    private static boolean containsXiaomi(String s) {
        return s != null && s.toLowerCase().contains("xiaomi");
    }

    public ApplicationExitInfo getLastApplicationExitInfo() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                ActivityManager activityManager = getSystemService(ActivityManager.class);
                if (activityManager != null) {
                    List<ApplicationExitInfo> exitReasons =
                        activityManager.getHistoricalProcessExitReasons(null, 0, 1);
                    if (exitReasons != null && !exitReasons.isEmpty()) {
                        return exitReasons.get(0);
                    }
                }
            } catch (Exception e) {
                Log.e(TAG, "Exception getting last application exit info", e);
            }
        }
        return null;
    }

    public static boolean isLowMemoryKillReportSupported() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                return ActivityManager.isLowMemoryKillReportSupported();
            } catch (Exception e) {
                Log.e(TAG, "Exception getting low memory kill report support", e);
            }
        }
        return false;
    }

    @SuppressWarnings("unused")
    public static void manageSubscriptions() {
        manageSubscription(null);
    }

    @SuppressWarnings("unused")
    public static void manageSubscription(String productId) {
        doWithMainActivity((MainActivity activity) -> {
            activity.runOnUiThread(() -> {
                try {
                    String packageName = activity.getApplicationContext().getPackageName();
                    String uri = "https://play.google.com/store/account/subscriptions?package=" + Uri.encode(packageName)
                            + (productId == null ? "" : "&sku=" + Uri.encode(productId));
                    activity.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(uri)));
                } catch (Exception e) {
                    Log.e(TAG, "Failed to open subscription management", e);
                    Toast.makeText(QtNative.getContext(), R.string.something_wrong, Toast.LENGTH_LONG).show();
                }
            });
        });
    }

    @SuppressWarnings("unused")
    public static void showDonationDialog(boolean splash, byte[] splashBytes, String splashArtist, String splashVersion) {
        Log.d(TAG, "showDonationDialog");
        try {
            Activity activity = QtNative.activity();
            if (activity instanceof MainActivity) {
                ((MainActivity) activity).showDonationDialogInternal(splash, splashBytes, splashArtist, splashVersion);
            } else {
                Log.e(TAG, "showDonationDialog: QtNative.activity() is not a Krita MainActivity");
            }
        } catch (Exception e) {
            Log.e(TAG, "Exception dispatching donation dialog", e);
        }
    }

    private void showDonationDialogInternal(boolean splash, byte[] splashBytes, String splashArtist, String splashVersion) {
        QtNative.activity().runOnUiThread(() -> {
            if (mDonationDialog == null) {
                try {
                    mDonationDialog = new DonationDialog(MainActivity.this, splash);
                    updateDonationDialog();
                    AlertDialog alertDialog = mDonationDialog.getAlertDialog();
                    alertDialog.setOnDismissListener(dialogInterface -> {
                        if (splash) {
                            JNIWrappers.onSplashDialogDismissed();
                        }
                        if (mDonationDialog != null) {
                            if (dialogInterface == mDonationDialog.getAlertDialog()) {
                                Log.d(TAG, "Donation dialog dismissed, clearing");
                                mDonationDialog = null;
                            } else {
                                Log.w(TAG, "Unknown donation dialog dismissed, not clearing it");
                            }
                        }
                    });
                    alertDialog.show();

                    if (splash) {
                        Bitmap bitmap = loadSplashImageBitmap(splashBytes);
                        if (bitmap != null) {
                            mDonationDialog.setSplashContents(bitmap, combineSplashText(splashArtist, splashVersion));
                        }
                    }

                    updateDonationDialogProductsInternal(mDonationDialog);
                } catch (Exception e) {
                    Log.e(TAG, "Exception showing donation dialog", e);
                }
            } else {
                Log.w(TAG, "Donation dialog requested while already shown");
            }
        });
    }

    private static Bitmap loadSplashImageBitmap(byte[] splashBytes) {
        if (splashBytes == null || splashBytes.length == 0) {
            Log.d(TAG, "No splash image data given");
            return null;
        }

        Bitmap bitmap;
        try {
            Log.d(TAG, "Loading splash image bitmap");
            bitmap = BitmapFactory.decodeByteArray(splashBytes, 0, splashBytes.length);
        } catch (Exception e) {
            Log.d(TAG, "Exception loading splash image bitmap", e);
            return null;
        }

        if (bitmap == null) {
            Log.w(TAG, "Null splash image bitmap loaded");
            return null;
        }

        Log.d(TAG, "Loaded splash image bitmap");
        return bitmap;
    }

    private static String combineSplashText(String splashArtist, String splashVersion) {
        boolean haveArtist = splashArtist != null && !splashArtist.isEmpty();
        boolean haveVersion = splashVersion != null && !splashVersion.isEmpty();
        if (haveArtist) {
            if (haveVersion) {
                return splashVersion + "\n" + splashArtist;
            } else {
                return splashArtist;
            }
        } else if (haveVersion) {
            return splashVersion;
        } else {
            return "";
        }
    }

    @SuppressWarnings("unused")
    public static void setLoaded(boolean loaded) {
        Log.d(TAG, "setLoaded " + loaded);
        applicationLoaded = loaded;
        updateDonationDialog();
    }

    @SuppressWarnings("unused")
    public static void setLoadingText(String loadingText) {
        Log.d(TAG, "setLoadingText " + loadingText);
        applicationLoadingText = loadingText;
        updateDonationDialog();
    }

    private static void updateDonationDialog() {
        doWithDonationDialog((MainActivity activity, DonationDialog dlg) -> {
            if (dlg.isSplash()) {
                dlg.setLoading(!applicationLoaded);
                dlg.setLoadingText(activity.getLoadingText());
            } else {
                dlg.showProductListPage();
                dlg.setLoading(false);
            }
        });
    }

    private String getLoadingText() {
        if (applicationLoaded) {
            return getString(R.string.donation_dialog_loaded);
        } else if (applicationLoadingText == null || applicationLoadingText.isEmpty()) {
            return getString(R.string.donation_dialog_loading);
        } else {
            return applicationLoadingText;
        }
    }

    public static void showPurchaseConfirmation() {
        JNIWrappers.showDonationManagementDialog();
        doWithDonationDialog((MainActivity activity, DonationDialog dlg) -> {
            dlg.showPurchaseConfirmationPage();
        });
    }

    public static void doWithDonationDialog(BiConsumer<MainActivity, DonationDialog> consumer) {
        doWithMainActivity((MainActivity activity) -> {
            activity.doWithDonationDialogInternal(consumer);
        });
    }

    private void doWithDonationDialogInternal(BiConsumer<MainActivity, DonationDialog> consumer) {
        if (mDonationDialog == null) {
            Log.d(TAG, "Donation dialog not set, not updating it");
        } else {
            QtNative.activity().runOnUiThread(() -> {
                if (mDonationDialog == null) {
                    Log.d(TAG, "Donation dialog not set on UI thread, not updating it");
                } else {
                    consumer.accept(this, mDonationDialog);
                }
            });
        }
    }

    public static void doWithMainActivity(Consumer<MainActivity> consumer) {
        try {
            Activity activity = QtNative.activity();
            if (activity instanceof MainActivity) {
                consumer.accept(((MainActivity) activity));
            } else {
                Log.e(TAG, "doWithMainActivity: QtNative.activity() is not a Krita MainActivity");
            }
        } catch (Exception e) {
            Log.e(TAG, "Exception in doWithMainActivity", e);
        }
    }

    public static void updateDonationDialogProducts() {
        doWithDonationDialog(MainActivity::updateDonationDialogProductsInternal);
    }

    private void updateDonationDialogProductsInternal(DonationDialog dlg) {
        DonationHelper donationHelper = DonationHelper.getInstance();
        dlg.setProductDetails(
                donationHelper.isReady() ? donationHelper.getProductDetails() : null,
                donationHelper.isAnyProductsOwned());
    }

    public void showScalingDialog(double currentScale, double defaultScale, boolean showOnStartup, boolean canShowOnStartup) {
        QtNative.activity().runOnUiThread(() -> {
            ScalingDialog scalingDialog = new ScalingDialog(this, currentScale, defaultScale, showOnStartup, canShowOnStartup);
            scalingDialog.show();
        });
    }
}
