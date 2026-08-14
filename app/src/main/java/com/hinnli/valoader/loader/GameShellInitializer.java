package com.hinnli.valoader.loader;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.app.Application;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.hinnli.valoader.game.GamePackage;
import com.hinnli.valoader.overlay.NativeBridge;

import java.lang.reflect.Method;
import java.util.concurrent.FutureTask;

public final class GameShellInitializer {
    private static final String TAG = "Valoader.GameShell";
    private static final Object LOCK = new Object();
    private static State state = State.NEW;
    private static RuntimeException failure;
    // The native shell keeps JNI references to its Application receiver.
    // Retain the same object for the lifetime of the process.
    private static Object gameApplication;

    private GameShellInitializer() {
    }

    public static void initialize(Context context, GamePackage gamePackage) {
        synchronized (LOCK) {
            while (state == State.RUNNING) {
                waitForInitialization();
            }
            if (state == State.READY) {
                return;
            }
            if (state == State.FAILED) {
                throw failure;
            }
            state = State.RUNNING;
        }

        try {
            initializeNativeShell(context.getApplicationContext(), gamePackage);
            synchronized (LOCK) {
                state = State.READY;
                LOCK.notifyAll();
            }
            Log.i(TAG, "TPShell initialized for installed Valorant package");
        } catch (Throwable error) {
            RuntimeException wrapped = error instanceof RuntimeException
                ? (RuntimeException) error
                : new IllegalStateException("Cannot initialize Valorant TPShell", error);
            synchronized (LOCK) {
                failure = wrapped;
                state = State.FAILED;
                LOCK.notifyAll();
            }
            throw wrapped;
        }
    }

    private static void initializeNativeShell(Context context, GamePackage gamePackage) throws Exception {
        Context gameContext = context.createPackageContext(GamePackage.TARGET_PACKAGE, 0);
        GameRuntimeContext runtimeContext = new GameRuntimeContext(context, gameContext);
        ApplicationInfo target = runtimeContext.getApplicationInfo();
        Log.i(TAG, "Attaching game Application with contextPackage=" + runtimeContext.getPackageName() +
            ", processPackage=" + context.getPackageName() +
            ", processName=" + android.app.Application.getProcessName() +
            ", runtimeUid=" + android.os.Process.myUid() +
            ", filesDir=" + runtimeContext.getFilesDir() +
            ", nativeLibraryDir=" + target.nativeLibraryDir +
            ", shellSource=" + target.sourceDir);
        NativeLibraryLoader.installGameNativePath(context.getClassLoader(), gamePackage);
        System.loadLibrary("tprt");
        NativeBridge.load();

        Class<?> applicationClass = Class.forName("com.epicgames.ue4.GameApplication");
        Object application = applicationClass.getDeclaredConstructor().newInstance();
        Method attachBaseContext = applicationClass.getMethod("attachBaseContext", Context.class);
        attachBaseContext.invoke(application, runtimeContext);
        GameRuntimeContext.bindGameApplication((Application) application);
        gameApplication = application;
        FutureTask<Void> createApplication = new FutureTask<>(() -> {
            applicationClass.getMethod("onCreate").invoke(application);
            return null;
        });
        new Handler(Looper.getMainLooper()).post(createApplication);
        createApplication.get();
        Log.i(TAG, "GameApplication attach/onCreate sequence completed");
    }

    private static void waitForInitialization() {
        try {
            LOCK.wait();
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("TPShell initialization was interrupted", error);
        }
    }

    private enum State {
        NEW,
        RUNNING,
        READY,
        FAILED
    }
}
