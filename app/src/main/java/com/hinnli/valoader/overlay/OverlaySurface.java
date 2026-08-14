package com.hinnli.valoader.overlay;

import android.content.Context;
import android.graphics.PixelFormat;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public final class OverlaySurface extends SurfaceView implements SurfaceHolder.Callback {
    private final float density;
    private Thread renderThread;

    public OverlaySurface(Context context) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        setZOrderOnTop(true);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        getHolder().addCallback(this);
        setClickable(false);
        setFocusable(false);
    }

    @Override
    public synchronized void surfaceCreated(SurfaceHolder holder) {
        if (renderThread != null && renderThread.isAlive()) {
            return;
        }
        renderThread = new Thread(
            () -> NativeBridge.runSurface(holder.getSurface(), density),
            "Valoader-ImGui"
        );
        renderThread.start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        NativeBridge.resizeSurface(width, height);
    }

    @Override
    public synchronized void surfaceDestroyed(SurfaceHolder holder) {
        NativeBridge.stopSurface();
        renderThread = null;
    }
}
