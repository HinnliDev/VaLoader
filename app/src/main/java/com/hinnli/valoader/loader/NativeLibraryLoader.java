package com.hinnli.valoader.loader;

import android.util.Log;

import dalvik.system.BaseDexClassLoader;

import com.hinnli.valoader.game.GamePackage;

import java.io.File;
import java.lang.reflect.Method;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public final class NativeLibraryLoader {
    private static final String TAG = "Valoader.NativeLoader";
    private static final Pattern MISSING_LIBRARY = Pattern.compile("library \\\"([^\\\"]+\\.so)\\\" not found");
    private static final Set<String> LOADED = new HashSet<>();
    private static final Set<String> ACTIVE = new HashSet<>();
    private static final String[] ENGINE_DEPENDENCIES = {
        "libtersafe.so",
        "libTDataMaster.so",
        "libCrashSight.so",
        "libMSDKCore.so",
        "libPluginCrosCurl.so",
        "libGCloudVoice.so",
        "libgsdk.so",
        "libopenplatform.so",
        "libgcloud.so",
        "libgcloudcore.so",
        "libHttpDnsCore.so",
        "libHttpDnsPlugin.so",
        "libGPM.so",
        "libgrobot.so"
    };

    private NativeLibraryLoader() {
    }

    public static synchronized void prepareGame(GamePackage gamePackage) {
        loadAbsolute(gamePackage, gamePackage.nativeLibrary("libc++_shared.so"));
        for (String dependency : ENGINE_DEPENDENCIES) {
            loadAbsolute(gamePackage, gamePackage.nativeLibrary(dependency));
        }
    }

    public static void installGameNativePath(ClassLoader classLoader, GamePackage gamePackage) {
        if (!(classLoader instanceof BaseDexClassLoader)) {
            throw new IllegalStateException("Unsupported application ClassLoader: " + classLoader);
        }
        try {
            Method addNativePath = BaseDexClassLoader.class.getDeclaredMethod(
                "addNativePath",
                Collection.class
            );
            addNativePath.setAccessible(true);
            addNativePath.invoke(classLoader, List.of(gamePackage.nativeLibraryDirectory()));
            Log.i(TAG, "Added native search path " + gamePackage.nativeLibraryDirectory());
        } catch (ReflectiveOperationException error) {
            throw new IllegalStateException("Cannot add installed game native search path", error);
        }
    }

    private static void loadAbsolute(GamePackage gamePackage, File library) {
        String path = library.getAbsolutePath();
        if (LOADED.contains(path)) {
            return;
        }
        if (!library.isFile()) {
            throw new IllegalStateException("Native library is missing: " + path);
        }
        if (!ACTIVE.add(path)) {
            throw new IllegalStateException("Circular native dependency while loading " + path);
        }

        try {
            for (int attempt = 0; attempt < 128; attempt++) {
                try {
                    Log.i(TAG, "Loading " + library.getName() + " from " + path);
                    System.load(path);
                    LOADED.add(path);
                    Log.i(TAG, "Loaded " + library.getName());
                    return;
                } catch (UnsatisfiedLinkError error) {
                    Matcher matcher = MISSING_LIBRARY.matcher(error.getMessage() == null ? "" : error.getMessage());
                    if (!matcher.find()) {
                        throw error;
                    }
                    String dependencyName = matcher.group(1);
                    File dependency = gamePackage.nativeLibrary(dependencyName);
                    if (dependency.getAbsolutePath().equals(path)) {
                        throw error;
                    }
                    Log.i(TAG, library.getName() + " requires " + dependencyName);
                    loadAbsolute(gamePackage, dependency);
                }
            }
            throw new IllegalStateException("Too many dependencies while loading " + library.getName());
        } finally {
            ACTIVE.remove(path);
        }
    }
}
