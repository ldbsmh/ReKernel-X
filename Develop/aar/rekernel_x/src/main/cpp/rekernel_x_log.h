#ifndef REKERNEL_X_LOG_H
#define REKERNEL_X_LOG_H

#include <android/log.h>

#define TAG "ReKernel-X"

#define rekernel_x_log_info(fmt, ...) __android_log_print(ANDROID_LOG_INFO,  TAG, fmt, ##__VA_ARGS__)
#define rekernel_x_log_err(fmt, ...)  __android_log_print(ANDROID_LOG_ERROR, TAG, fmt, ##__VA_ARGS__)
#define rekernel_x_log_warn(fmt, ...) __android_log_print(ANDROID_LOG_WARN,  TAG, fmt, ##__VA_ARGS__)

#ifdef REKERNEL_X_DEBUG
#define rekernel_x_log_debug(fmt, ...) __android_log_print(ANDROID_LOG_DEBUG, TAG, fmt, ##__VA_ARGS__)
#else
#define rekernel_x_log_debug(fmt, ...) ((void)0)
#endif

#endif // REKERNEL_X_LOG_H