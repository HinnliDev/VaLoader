package com.hinnli.valoader;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.hinnli.valoader.game.GamePackage;
import com.hinnli.valoader.game.GamePackageLocator;
import com.hinnli.valoader.loader.GameResourceBridge;
import com.hinnli.valoader.loader.GameShellInitializer;
import com.hinnli.valoader.loader.NativeLibraryLoader;

public final class LauncherActivity extends Activity {
    private static final String TAG = "Valoader.Launcher";
    private TextView status;
    private Button launchButton;
    private GamePackage gamePackage;
    private volatile boolean shellReady;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(createContent());
        inspectGame();
    }

    private LinearLayout createContent() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setPadding(dp(28), dp(64), dp(28), dp(28));
        root.setBackgroundColor(Color.rgb(15, 17, 22));

        TextView title = new TextView(this);
        title.setText("Valoader");
        title.setTextColor(Color.WHITE);
        title.setTextSize(32);
        title.setGravity(Gravity.CENTER);
        root.addView(title, matchWrap());

        TextView subtitle = new TextView(this);
        subtitle.setText("Valorant Mobile launcher with ESP overlay");
        subtitle.setTextColor(Color.rgb(165, 171, 184));
        subtitle.setTextSize(15);
        subtitle.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams subtitleParams = matchWrap();
        subtitleParams.setMargins(0, dp(8), 0, dp(36));
        root.addView(subtitle, subtitleParams);

        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setTextSize(16);
        root.addView(status, matchWrap());

        launchButton = new Button(this);
        launchButton.setText("Launch Valorant");
        launchButton.setEnabled(false);
        launchButton.setOnClickListener(view -> launchGame());
        LinearLayout.LayoutParams buttonParams = matchWrap();
        buttonParams.setMargins(0, dp(28), 0, 0);
        root.addView(launchButton, buttonParams);

        return root;
    }

    private void inspectGame() {
        try {
            gamePackage = GamePackageLocator.require(this);
            String support = gamePackage.isTestedVersion() ? "supported" : "not tested";
            status.setText(
                "Installed: " + gamePackage.versionName() + " (" + gamePackage.versionCode() + ")\n" +
                "Loader profile: " + support + "\n" +
                "Initializing game shell..."
            );
            Thread initializer = new Thread(() -> initializeGameShell(support), "Valoader-TPShell");
            initializer.start();
        } catch (Exception error) {
            status.setText("Valorant Mobile is not installed\nRequired package: " + GamePackage.TARGET_PACKAGE);
        }
    }

    private void initializeGameShell(String support) {
        try {
            GameShellInitializer.initialize(this, gamePackage);
            shellReady = true;
            runOnUiThread(() -> {
                status.setText(
                    "Installed: " + gamePackage.versionName() + " (" + gamePackage.versionCode() + ")\n" +
                    "Loader profile: " + support + "\n" +
                    "Ready"
                );
                launchButton.setEnabled(true);
            });
        } catch (Throwable error) {
            Log.e(TAG, "TPShell initialization failed", error);
            runOnUiThread(() -> showLaunchError(error));
        }
    }

    private void launchGame() {
        if (!shellReady) {
            status.setText("Game shell is still initializing...");
            return;
        }
        launchButton.setEnabled(false);
        showStatus("Mounting installed game resources...");
        Thread worker = new Thread(() -> {
            try {
                Log.i(TAG, "Mounting resources from " + gamePackage.sourceApk());
                GameResourceBridge.install(getResources(), gamePackage.sourceApk());
                Log.i(TAG, "Game resources mounted");
                showStatus("Loading installed game libraries...");
                NativeLibraryLoader.prepareGame(gamePackage);
                Log.i(TAG, "Game dependencies loaded");
                NativeLibraryLoader.installGameNativePath(getClassLoader(), gamePackage);
                Log.i(TAG, "Installed game native path; UE4 loading is deferred to GameActivity");
                runOnUiThread(() -> {
                    try {
                        status.setText("Starting Unreal Engine...");
                        Intent intent = new Intent();
                        intent.setClassName(getPackageName(), "com.epicgames.ue4.GameActivity");
                        // SplashActivity always supplies an extras Bundle. The game build
                        // dereferences it unconditionally later in GameActivity.onCreate.
                        intent.putExtra("ShouldHideUI", "true");
                        intent.putExtra("UseDisplayCutout", "true");
                        // The stock SplashActivity always supplies this flag.
                        // GameActivity reports OnSplashScreenDismiss to UE4 only
                        // after the installed APK's startup video completes.
                        intent.putExtra("UseSplashScreen", "true");
                        intent.putExtra("applinksuri", "");
                        startActivity(intent);
                        launchButton.setEnabled(true);
                        status.setText("Game started");
                    } catch (Throwable error) {
                        showLaunchError(error);
                    }
                });
            } catch (Throwable error) {
                Log.e(TAG, "Launch failed", error);
                runOnUiThread(() -> showLaunchError(error));
            }
        }, "Valoader-GameLoader");
        worker.start();
    }

    private void showStatus(String message) {
        runOnUiThread(() -> status.setText(message));
    }

    private void showLaunchError(Throwable error) {
        Log.e(TAG, "Launch failed", error);
        launchButton.setEnabled(true);
        status.setText("Launch failed:\n" + error);
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
