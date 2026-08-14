package com.hinnli.valoader.loader;

import android.content.res.Resources;
import android.content.res.loader.ResourcesLoader;
import android.content.res.loader.ResourcesProvider;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.File;
import java.io.IOException;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.Set;

public final class GameResourceBridge {
    private static final String TAG = "Valoader.Resources";
    private static final Set<Resources> INSTALLED = Collections.newSetFromMap(new IdentityHashMap<>());
    private static ParcelFileDescriptor gameApkDescriptor;
    private static ResourcesProvider gameProvider;
    private static ResourcesLoader resourcesLoader;

    private GameResourceBridge() {
    }

    public static synchronized void install(Resources resources, String gameApkPath) throws IOException {
        if (INSTALLED.contains(resources)) {
            return;
        }
        if (resourcesLoader == null) {
            gameApkDescriptor = ParcelFileDescriptor.open(new File(gameApkPath), ParcelFileDescriptor.MODE_READ_ONLY);
            gameProvider = ResourcesProvider.loadFromApk(gameApkDescriptor);
            resourcesLoader = new ResourcesLoader();
            resourcesLoader.addProvider(gameProvider);
        }
        resources.addLoaders(resourcesLoader);
        INSTALLED.add(resources);
        Log.i(TAG, "Mounted game resources and assets from " + gameApkPath);
    }
}
