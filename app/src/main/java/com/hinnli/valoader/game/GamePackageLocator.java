package com.hinnli.valoader.game;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Build;

public final class GamePackageLocator {
    private GamePackageLocator() {
    }

    public static GamePackage require(Context context) throws PackageManager.NameNotFoundException {
        PackageInfo packageInfo = context.getPackageManager().getPackageInfo(
            GamePackage.TARGET_PACKAGE,
            PackageManager.GET_SHARED_LIBRARY_FILES | PackageManager.GET_META_DATA
        );
        long versionCode = Build.VERSION.SDK_INT >= Build.VERSION_CODES.P
            ? packageInfo.getLongVersionCode()
            : packageInfo.versionCode;
        return GamePackage.from(packageInfo.applicationInfo, packageInfo.versionName, versionCode);
    }
}
