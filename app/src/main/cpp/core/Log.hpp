#pragma once

#include <android/log.h>

#define VALOADER_LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, "Valoader", __VA_ARGS__)
#define VALOADER_LOG_WARN(...) __android_log_print(ANDROID_LOG_WARN, "Valoader", __VA_ARGS__)
#define VALOADER_LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, "Valoader", __VA_ARGS__)
