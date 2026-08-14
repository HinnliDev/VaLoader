package com.hinnli.valoader.overlay;

import android.view.Surface;

public final class NativeBridge {
    private static boolean loaded;

    private NativeBridge() {
    }

    public static synchronized void load() {
        if (!loaded) {
            System.loadLibrary("valoader");
            loaded = true;
        }
    }

    public static native void runSurface(Surface surface, float density);
    public static native void stopSurface();
    public static native void resizeSurface(int width, int height);
    public static native void submitTouch(float x, float y, int action);
    public static native void setMenuButtonBounds(float x, float y, float size);
    public static native boolean toggleMenu();
    public static native boolean isMenuVisible();
}
