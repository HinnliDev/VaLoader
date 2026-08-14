package com.hinnli.valoader;

import android.app.Activity;
import android.app.Application;
import android.os.Bundle;
import android.util.Log;

import com.hinnli.valoader.game.GamePackage;
import com.hinnli.valoader.game.GamePackageLocator;
import com.hinnli.valoader.loader.GameResourceBridge;
import com.hinnli.valoader.loader.GameActivityContextBridge;
import com.hinnli.valoader.overlay.OverlayHost;

public final class ValoaderApplication extends Application implements Application.ActivityLifecycleCallbacks {
    private static final String TAG = "Valoader";
    private static final String GAME_ACTIVITY = "com.epicgames.ue4.GameActivity";
    private GamePackage gamePackage;

    @Override
    public void onCreate() {
        super.onCreate();
        try {
            gamePackage = GamePackageLocator.require(this);
            GameResourceBridge.install(getResources(), gamePackage.sourceApk());
        } catch (Exception error) {
            Log.e(TAG, "Game environment is not ready", error);
        }
        registerActivityLifecycleCallbacks(this);
    }

    @Override
    public void onActivityPreCreated(Activity activity, Bundle savedInstanceState) {
        if (isGameActivity(activity) && gamePackage != null) {
            try {
                GameActivityContextBridge.install(activity);
                GameResourceBridge.install(activity.getResources(), gamePackage.sourceApk());
            } catch (Exception error) {
                throw new IllegalStateException("Cannot mount Valorant resources", error);
            }
        }
    }

    @Override
    public void onActivityCreated(Activity activity, Bundle savedInstanceState) {
        if (isGameActivity(activity)) {
            OverlayHost.attach(activity);
        }
    }

    @Override
    public void onActivityResumed(Activity activity) {
        if (isGameActivity(activity)) {
            OverlayHost.attach(activity);
        }
    }

    private boolean isGameActivity(Activity activity) {
        return GAME_ACTIVITY.equals(activity.getClass().getName());
    }

    @Override public void onActivityStarted(Activity activity) { }
    @Override public void onActivityPaused(Activity activity) { }
    @Override public void onActivityStopped(Activity activity) { }
    @Override public void onActivitySaveInstanceState(Activity activity, Bundle outState) { }
    @Override public void onActivityDestroyed(Activity activity) { }
}
