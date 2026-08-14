package com.hinnli.valoader.loader;

import android.app.Application;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.database.DatabaseErrorHandler;
import android.database.sqlite.SQLiteDatabase;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;

/**
 * Context presented to the embedded game Application.
 *
 * <p>Package resources/code/native libraries belong to the installed game,
 * while writable storage and the Linux UID necessarily belong to Valoader.
 * Keeping that distinction in one Context avoids passing an unattached
 * Application object to TPShell's JNI entry point.</p>
 */
final class GameRuntimeContext extends ContextWrapper {
    private static volatile Application gameApplication;

    private final Context processContext;
    private final Context gameContext;
    private final ApplicationInfo applicationInfo;

    GameRuntimeContext(Context processContext, Context gameContext) {
        super(gameContext);
        this.processContext = processContext;
        this.gameContext = gameContext;

        ApplicationInfo target = new ApplicationInfo(gameContext.getApplicationInfo());
        ApplicationInfo process = processContext.getApplicationInfo();
        target.uid = process.uid;
        target.dataDir = process.dataDir;
        target.deviceProtectedDataDir = process.deviceProtectedDataDir;
        this.applicationInfo = target;
    }

    static void bindGameApplication(Application application) {
        gameApplication = application;
    }

    @Override
    public String getPackageName() {
        // QRLogin builds an explicit Intent(Context, WXQrCodeActivity.class).
        // That constructor takes the component package from this method. The
        // embedded game's identity would target the separately installed APK
        // and ActivityTaskManager rejects it for Valoader's UID. Keep Tencent's
        // OAuth identity everywhere else and route only this internal MSDK UI
        // component to the real host package.
        if (isMsdkQrActivityRouting()) {
            return processContext.getPackageName();
        }
        return gameContext.getPackageName();
    }

    private static boolean isMsdkQrActivityRouting() {
        for (StackTraceElement frame : Thread.currentThread().getStackTrace()) {
            if ("com.tencent.gcloud.msdk.qrcode.QRLogin".equals(frame.getClassName())
                    && "updateQrCodeImgActivity".equals(frame.getMethodName())) {
                return true;
            }
        }
        return false;
    }

    @Override
    public Context getApplicationContext() {
        // A ContextImpl created for a foreign package has no instantiated
        // Application. Several bundled SDKs explicitly cast this value to
        // android.app.Application, so expose the retained, attached game
        // Application instead of the hybrid Context wrapper.
        Application application = gameApplication;
        return application != null ? application : this;
    }


    @Override
    public Object getSystemService(String name) {
        // Framework managers (AudioManager, ConnectivityManager, etc.) must be
        // constructed with the real registered package/attribution context.
        return processContext.getSystemService(name);
    }

    @Override
    public boolean bindService(Intent service, ServiceConnection connection, int flags) {
        Intent routed = routeHostOwnedService(service);
        return processContext.bindService(routed, connection, flags);
    }

    @Override
    public void unbindService(ServiceConnection connection) {
        processContext.unbindService(connection);
    }

    private Intent routeHostOwnedService(Intent service) {
        ComponentName component = service.getComponent();
        if (component == null
                || !gameContext.getPackageName().equals(component.getPackageName())
                || !isEmbeddedEstService(component.getClassName())) {
            return service;
        }
        Intent routed = new Intent(service);
        routed.setComponent(new ComponentName(
                processContext.getPackageName(), component.getClassName()));
        android.util.Log.i("Valoader.ServiceRoute",
                "Routed embedded EST service to " + routed.getComponent());
        return routed;
    }

    private static boolean isEmbeddedEstService(String className) {
        return "com.tencent.estv.livesdk.service.PluginProcessService".equals(className)
                || "com.tencent.estv.ves.services.PluginService".equals(className);
    }

    @Override
    public String getOpPackageName() {
        return processContext.getOpPackageName();
    }

    @Override
    public String getAttributionTag() {
        return processContext.getAttributionTag();
    }

    @Override
    public ApplicationInfo getApplicationInfo() {
        return applicationInfo;
    }

    @Override
    public File getFilesDir() {
        return processContext.getFilesDir();
    }

    @Override
    public File getCacheDir() {
        return processContext.getCacheDir();
    }

    @Override
    public File getCodeCacheDir() {
        return processContext.getCodeCacheDir();
    }

    @Override
    public File getNoBackupFilesDir() {
        return processContext.getNoBackupFilesDir();
    }

    @Override
    public File getDataDir() {
        return processContext.getDataDir();
    }

    @Override
    public SharedPreferences getSharedPreferences(String name, int mode) {
        return processContext.getSharedPreferences(name, mode);
    }

    @Override
    public boolean deleteSharedPreferences(String name) {
        return processContext.deleteSharedPreferences(name);
    }

    @Override
    public FileInputStream openFileInput(String name) throws FileNotFoundException {
        return processContext.openFileInput(name);
    }

    @Override
    public FileOutputStream openFileOutput(String name, int mode) throws FileNotFoundException {
        return processContext.openFileOutput(name, mode);
    }

    @Override
    public boolean deleteFile(String name) {
        return processContext.deleteFile(name);
    }

    @Override
    public File getFileStreamPath(String name) {
        return processContext.getFileStreamPath(name);
    }

    @Override
    public String[] fileList() {
        return processContext.fileList();
    }

    @Override
    public File getDir(String name, int mode) {
        return processContext.getDir(name, mode);
    }

    @Override
    public File getDatabasePath(String name) {
        return processContext.getDatabasePath(name);
    }

    @Override
    public SQLiteDatabase openOrCreateDatabase(String name, int mode,
                                                SQLiteDatabase.CursorFactory factory) {
        return processContext.openOrCreateDatabase(name, mode, factory);
    }

    @Override
    public SQLiteDatabase openOrCreateDatabase(String name, int mode,
                                                SQLiteDatabase.CursorFactory factory,
                                                DatabaseErrorHandler errorHandler) {
        return processContext.openOrCreateDatabase(name, mode, factory, errorHandler);
    }

    @Override
    public boolean deleteDatabase(String name) {
        return processContext.deleteDatabase(name);
    }

    @Override
    public String[] databaseList() {
        return processContext.databaseList();
    }

    @Override
    public File getExternalFilesDir(String type) {
        return processContext.getExternalFilesDir(type);
    }

    @Override
    public File[] getExternalFilesDirs(String type) {
        return processContext.getExternalFilesDirs(type);
    }

    @Override
    public File getExternalCacheDir() {
        return processContext.getExternalCacheDir();
    }

    @Override
    public File[] getExternalCacheDirs() {
        return processContext.getExternalCacheDirs();
    }

    @Override
    public File getObbDir() {
        // Android rejects creating another package's scoped OBB directory for
        // Valoader's UID. The game data itself remains inside the installed
        // target APK; this is only the writable/fallback OBB location.
        return processContext.getObbDir();
    }

    @Override
    public File[] getObbDirs() {
        return processContext.getObbDirs();
    }

    @Override
    public String getPackageCodePath() {
        return applicationInfo.sourceDir;
    }

    @Override
    public String getPackageResourcePath() {
        return applicationInfo.publicSourceDir;
    }

    @Override
    public ClassLoader getClassLoader() {
        return processContext.getClassLoader();
    }
}
