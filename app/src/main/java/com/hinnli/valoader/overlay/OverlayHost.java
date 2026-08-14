package com.hinnli.valoader.overlay;

import android.app.Activity;
import android.graphics.Color;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.widget.FrameLayout;

public final class OverlayHost extends FrameLayout {
    private static final String TAG = "ValoaderOverlay";
    private static final int TOGGLE_SIZE_DP = 32;
    private static final int TOGGLE_MARGIN_DP = 10;
    private final View inputLayer;
    private final View toggleTouchTarget;
    private final int touchSlop;
    private float pointerDownRawX;
    private float pointerDownRawY;
    private float toggleDownX;
    private float toggleDownY;
    private boolean toggleDragged;
    private boolean togglePositionInitialized;
    private boolean menuVisible;

    private OverlayHost(Activity activity) {
        super(activity);
        setTag(TAG);
        setClipChildren(false);
        setClipToPadding(false);
        setClickable(true);

        OverlaySurface surface = new OverlaySurface(activity);
        addView(surface, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));

        inputLayer = new View(activity);
        inputLayer.setClickable(true);
        inputLayer.setOnTouchListener(this::handleTouch);
        addView(inputLayer, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));

        // GameActivity recreates its content view while switching maps and
        // some full-screen panels. The native renderer survives that change,
        // so mirror its current state instead of always exposing the game.
        menuVisible = NativeBridge.isMenuVisible();
        inputLayer.setVisibility(menuVisible ? VISIBLE : GONE);

        touchSlop = ViewConfiguration.get(activity).getScaledTouchSlop();
        toggleTouchTarget = createToggleTouchTarget(activity);
        int toggleSize = dp(TOGGLE_SIZE_DP);
        addView(toggleTouchTarget, new LayoutParams(toggleSize, toggleSize, Gravity.TOP | Gravity.START));
    }

    public static void attach(Activity activity) {
        ViewGroup content = activity.findViewById(android.R.id.content);
        if (content == null || content.findViewWithTag(TAG) != null) {
            return;
        }
        content.addView(
            new OverlayHost(activity),
            new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        );
    }

    @Override
    protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        super.onSizeChanged(width, height, oldWidth, oldHeight);
        if (width <= 0 || height <= 0) {
            return;
        }

        if (!togglePositionInitialized) {
            toggleTouchTarget.setX(width - toggleSize() - dp(TOGGLE_MARGIN_DP));
            toggleTouchTarget.setY(dp(TOGGLE_MARGIN_DP));
            togglePositionInitialized = true;
        } else {
            moveToggle(toggleTouchTarget.getX(), toggleTouchTarget.getY());
        }
        syncToggleBounds();
    }

    private View createToggleTouchTarget(Activity activity) {
        View toggle = new View(activity);
        toggle.setBackgroundColor(Color.TRANSPARENT);
        toggle.setClickable(true);
        toggle.setOnTouchListener(this::handleToggleTouch);
        return toggle;
    }

    private boolean handleToggleTouch(View view, MotionEvent event) {
        float overlayX = view.getX() + event.getX();
        float overlayY = view.getY() + event.getY();
        NativeBridge.submitTouch(overlayX, overlayY, event.getActionMasked());

        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                getParent().requestDisallowInterceptTouchEvent(true);
                pointerDownRawX = event.getRawX();
                pointerDownRawY = event.getRawY();
                toggleDownX = view.getX();
                toggleDownY = view.getY();
                toggleDragged = false;
                return true;
            case MotionEvent.ACTION_MOVE:
                float deltaX = event.getRawX() - pointerDownRawX;
                float deltaY = event.getRawY() - pointerDownRawY;
                if (!toggleDragged && Math.hypot(deltaX, deltaY) >= touchSlop) {
                    toggleDragged = true;
                }
                if (toggleDragged) {
                    moveToggle(toggleDownX + deltaX, toggleDownY + deltaY);
                }
                return true;
            case MotionEvent.ACTION_UP:
                if (!toggleDragged) {
                    menuVisible = NativeBridge.toggleMenu();
                    inputLayer.setVisibility(menuVisible ? VISIBLE : GONE);
                    view.bringToFront();
                }
                getParent().requestDisallowInterceptTouchEvent(false);
                return true;
            case MotionEvent.ACTION_CANCEL:
                getParent().requestDisallowInterceptTouchEvent(false);
                return true;
            default:
                return true;
        }
    }

    private void moveToggle(float requestedX, float requestedY) {
        int size = toggleSize();
        float maxX = Math.max(0.0F, getWidth() - size);
        float maxY = Math.max(0.0F, getHeight() - size);
        toggleTouchTarget.setX(Math.max(0.0F, Math.min(requestedX, maxX)));
        toggleTouchTarget.setY(Math.max(0.0F, Math.min(requestedY, maxY)));
        syncToggleBounds();
    }

    private void syncToggleBounds() {
        NativeBridge.setMenuButtonBounds(
            toggleTouchTarget.getX(),
            toggleTouchTarget.getY(),
            toggleSize()
        );
    }

    private int toggleSize() {
        int measured = toggleTouchTarget.getWidth();
        return measured > 0 ? measured : toggleTouchTarget.getLayoutParams().width;
    }

    private boolean handleTouch(View view, MotionEvent event) {
        NativeBridge.submitTouch(event.getX(), event.getY(), event.getActionMasked());
        return true;
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            final boolean nativeMenuVisible = NativeBridge.isMenuVisible();
            if (nativeMenuVisible != menuVisible) {
                menuVisible = nativeMenuVisible;
                inputLayer.setVisibility(menuVisible ? VISIBLE : GONE);
                if (menuVisible) {
                    inputLayer.bringToFront();
                    toggleTouchTarget.bringToFront();
                }
            }
        }
        // The transparent overlay is the exclusive owner of every touch while
        // the menu is open. Returning true here is the final guard against a
        // rejected/late child event falling through to UE4's game viewport.
        final boolean mustConsume = menuVisible || isInsideToggle(event.getX(), event.getY());
        final boolean handled = super.dispatchTouchEvent(event);
        return mustConsume || handled;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        return menuVisible || super.onTouchEvent(event);
    }

    private boolean isInsideToggle(float x, float y) {
        final float left = toggleTouchTarget.getX();
        final float top = toggleTouchTarget.getY();
        final float size = toggleSize();
        return x >= left && x <= left + size && y >= top && y <= top + size;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
