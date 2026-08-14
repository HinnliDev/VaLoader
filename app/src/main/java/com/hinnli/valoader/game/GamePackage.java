package com.hinnli.valoader.game;

import android.content.pm.ApplicationInfo;

import java.io.File;

public record GamePackage(
    String packageName,
    String versionName,
    long versionCode,
    String sourceApk,
    String nativeLibraryDirectory
) {
    public static final String TARGET_PACKAGE = "com.tencent.tmgp.codev";
    public static final long TESTED_VERSION_CODE = 2911;

    public File nativeLibrary(String name) {
        return new File(nativeLibraryDirectory, name);
    }

    public boolean isTestedVersion() {
        return versionCode == TESTED_VERSION_CODE;
    }

    public static GamePackage from(ApplicationInfo applicationInfo, String versionName, long versionCode) {
        return new GamePackage(
            applicationInfo.packageName,
            versionName == null ? "unknown" : versionName,
            versionCode,
            applicationInfo.sourceDir,
            applicationInfo.nativeLibraryDir
        );
    }
}
