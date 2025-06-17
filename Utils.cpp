#include "Utils.h"

// 获取当前时间字符串，格式：2025-06-17 11::18::00
void get_timestamp(char* buffer, size_t size) {
    time_t rawtime;
    struct tm timeinfo;

    time(&rawtime);

    // 使用安全版本的 localtime
#ifdef _WIN32
    // Windows 平台使用 localtime_s
    if (localtime_s(&timeinfo, &rawtime) != 0) {
        // 如果转换失败，使用默认时间格式
        strncpy_s(buffer, size, "0000-00-00 00::00::00", _TRUNCATE);
        return;
    }
#else
    // Unix/Linux 平台使用 localtime_r
    if (localtime_r(&rawtime, &timeinfo) == NULL) {
        // 如果转换失败，使用默认时间格式
        strncpy(buffer, "0000-00-00 00::00::00", size - 1);
        buffer[size - 1] = '\0';
        return;
    }
#endif

    strftime(buffer, size, "%Y-%m-%d %H::%M::%S", &timeinfo);
}

// 获取日志级别字符串和颜色
void get_level_info(LogLevel level, const char** level_str, const char** color) {
    switch (level) {
    case LOG_INFO:
        *level_str = "INFO";
        *color = COLOR_GREEN;
        break;
    case LOG_WARNING:
        *level_str = "WARNING";
        *color = COLOR_YELLOW;
        break;
    case LOG_ERROR:
        *level_str = "ERROR";
        *color = COLOR_RED;
        break;
    case LOG_DEBUG:
        *level_str = "DEBUG";
        *color = COLOR_BLUE;
        break;
    default:
        *level_str = "UNKNOWN";
        *color = COLOR_RESET;
        break;
    }
}

// 带彩色日志输出函数
void log_message(LogLevel level, const char* format, ...) {
    char timestamp[32];
    const char* level_str;
    const char* color;
    va_list args;

    // 获取时间戳
    get_timestamp(timestamp, sizeof(timestamp));

    // 获取级别信息
    get_level_info(level, &level_str, &color);

    // 输出格式化的日志
    printf("%s[%s][%s]", color, level_str, timestamp);

    // 输出用户消息
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("%s\n", COLOR_RESET);
}

// 无颜色版本的日志输出
void log_message_plain(LogLevel level, const char* format, ...) {
    char timestamp[32];
    const char* level_str;
    const char* color;
    va_list args;

    get_timestamp(timestamp, sizeof(timestamp));
    get_level_info(level, &level_str, &color);

    printf("[%s][%s]", level_str, timestamp);

    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
}