package com.tencent.gcloud.msdk.qrcode;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.lang.reflect.Method;

/**
 * Host-side implementation of MSDK's QR window.
 *
 * <p>The original implementation resolves every resource by the hosting APK
 * package. Valoader deliberately does not copy the game's resource table, so
 * those lookups return zero. This implementation keeps the same class name and
 * Intent contract while constructing the tiny UI from platform widgets.</p>
 */
public final class WXQrCodeActivity extends Activity {
    public static final String ACTION_ON_QRCODE_AUTH =
            "com.tencent.msdk.weixin.qrcode.HIDE_AUTH";
    public static final String ACTION_ON_QRCODE_READY =
            "com.tencent.msdk.weixin.qrcode.QRCODE_READY";
    public static final String ACTION_ON_QRCODE_SCANNED =
            "com.tencent.msdk.weixin.qrcode.QRCODE_SCANNED";

    private static final String TAG = "Valoader.WeChatQR";
    private ImageView qrImage;
    private TextView status;
    private TextView prompt;
    private Bitmap bitmap;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        createContentView();
        handleIntent(getIntent());
    }

    @Override
    protected void onNewIntent(android.content.Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleIntent(intent);
    }

    private void createContentView() {
        int padding = dp(24);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(padding, padding, padding, padding);
        root.setBackgroundColor(Color.rgb(10, 22, 32));

        status = textView(22, Color.WHITE);
        status.setText("Preparing WeChat login…");
        root.addView(status, wrap());

        qrImage = new ImageView(this);
        qrImage.setAdjustViewBounds(true);
        qrImage.setScaleType(ImageView.ScaleType.FIT_CENTER);
        LinearLayout.LayoutParams imageParams = new LinearLayout.LayoutParams(dp(320), dp(320));
        imageParams.setMargins(0, dp(16), 0, dp(12));
        root.addView(qrImage, imageParams);

        prompt = textView(16, Color.LTGRAY);
        prompt.setGravity(Gravity.CENTER);
        prompt.setText("Open WeChat → Scan and scan the QR code.");
        root.addView(prompt, wrap());

        Button cancel = new Button(this);
        cancel.setText("Cancel");
        cancel.setOnClickListener(view -> {
            cancelQrLogin(true);
            finish();
        });
        LinearLayout.LayoutParams buttonParams = wrap();
        buttonParams.setMargins(0, dp(14), 0, 0);
        root.addView(cancel, buttonParams);
        setContentView(root);
    }

    private void handleIntent(android.content.Intent intent) {
        if (intent == null) {
            finish();
            return;
        }
        String action = intent.getStringExtra("action");
        Log.i(TAG, "handle action=" + action);
        if (ACTION_ON_QRCODE_AUTH.equals(action)) {
            finish();
            return;
        }
        if (ACTION_ON_QRCODE_SCANNED.equals(action)) {
            status.setText("QR code scanned");
            prompt.setText("Confirm WeChat login");
            return;
        }
        if (!ACTION_ON_QRCODE_READY.equals(action)) {
            return;
        }
        String path = intent.getStringExtra("qrcode_img");
        replaceBitmap(path);
        status.setText(bitmap != null ? "Log in via WeChat" : "Failed to load the QR code");
    }

    private void replaceBitmap(String path) {
        recycleBitmap();
        if (path != null) {
            bitmap = BitmapFactory.decodeFile(path);
        }
        qrImage.setImageBitmap(bitmap);
        Log.i(TAG, "QR image path=" + path + " loaded=" + (bitmap != null));
    }

    private void cancelQrLogin(boolean notify) {
        try {
            Class<?> type = Class.forName("com.tencent.gcloud.msdk.qrcode.QRLogin");
            Object instance = type.getMethod("getInstance", String.class).invoke(null, "");
            Method cancel = type.getMethod("cancel", boolean.class);
            cancel.invoke(instance, notify);
        } catch (Throwable error) {
            Log.w(TAG, "Could not cancel QR polling", error);
        }
    }

    private TextView textView(float sizeSp, int color) {
        TextView view = new TextView(this);
        view.setTextSize(sizeSp);
        view.setTextColor(color);
        return view;
    }

    private static LinearLayout.LayoutParams wrap() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void recycleBitmap() {
        if (bitmap != null && !bitmap.isRecycled()) {
            bitmap.recycle();
        }
        bitmap = null;
    }

    @Override
    protected void onDestroy() {
        recycleBitmap();
        if (isFinishing()) {
            cancelQrLogin(false);
        }
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        cancelQrLogin(true);
        super.onBackPressed();
    }
}
