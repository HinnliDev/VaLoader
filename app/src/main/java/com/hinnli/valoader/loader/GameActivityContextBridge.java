package com.hinnli.valoader.loader;

import android.app.Activity;
import android.content.Context;
import android.content.ContextWrapper;
import android.util.Log;

import com.hinnli.valoader.game.GamePackage;

import java.lang.reflect.Field;

/** Installs the installed game's read-only identity before GameActivity.onCreate. */
public final class GameActivityContextBridge {
    private static final String TAG = "Valoader.ActivityContext";

    private GameActivityContextBridge() {
    }

    public static void install(Activity activity) {
        try {
            Field baseField = ContextWrapper.class.getDeclaredField("mBase");
            baseField.setAccessible(true);
            Context processActivityContext = (Context) baseField.get(activity);
            Context gameContext = processActivityContext.createPackageContext(GamePackage.TARGET_PACKAGE, 0);
            baseField.set(activity, new GameRuntimeContext(processActivityContext, gameContext));
            Log.i(TAG, "Installed game identity before " + activity.getClass().getName() +
                ": package=" + activity.getPackageName() +
                ", codePath=" + activity.getPackageCodePath());
        } catch (Exception error) {
            throw new IllegalStateException("Cannot install GameActivity context", error);
        }
    }
}
