#include "platform/SurfaceRenderer.hpp"

#include "overlay/Menu.hpp"

#include <android/native_window_jni.h>
#include <jni.h>

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_hinnli_valoader_overlay_NativeBridge_runSurface(
    JNIEnv* environment,
    jclass,
    jobject surface,
    jfloat density
) {
    ANativeWindow* window = ANativeWindow_fromSurface(environment, surface);
    valoader::platform::SurfaceRenderer::run(window, density);
}

extern "C" JNIEXPORT void JNICALL
Java_com_hinnli_valoader_overlay_NativeBridge_stopSurface(JNIEnv*, jclass) {
    valoader::platform::SurfaceRenderer::stop();
}

extern "C" JNIEXPORT void JNICALL
Java_com_hinnli_valoader_overlay_NativeBridge_resizeSurface(JNIEnv*, jclass, jint width, jint height) {
    valoader::platform::SurfaceRenderer::resize(width, height);
}

extern "C" JNIEXPORT void JNICALL
Java_com_hinnli_valoader_overlay_NativeBridge_submitTouch(
    JNIEnv*,
    jclass,
    jfloat x,
    jfloat y,
    jint action
) {
    valoader::platform::SurfaceRenderer::submitTouch(x, y, action);
}

extern "C" JNIEXPORT void JNICALL
Java_com_hinnli_valoader_overlay_NativeBridge_setMenuButtonBounds(
    JNIEnv*,
    jclass,
    jfloat x,
    jfloat y,
    jfloat size
) {
    valoader::overlay::Menu::setToggleBounds(x, y, size);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_hinnli_valoader_overlay_NativeBridge_toggleMenu(JNIEnv*, jclass) {
    return valoader::overlay::Menu::toggle() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_hinnli_valoader_overlay_NativeBridge_isMenuVisible(JNIEnv*, jclass) {
    return valoader::overlay::Menu::visible() ? JNI_TRUE : JNI_FALSE;
}
