#pragma once
#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

// ��־����ö��
typedef enum {
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_DEBUG
} LogLevel;

// ��־�ĺ궨��
#define LOG_INFO(fmt, ...)    log_message(LOG_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) log_message(LOG_WARNING, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)   log_message(LOG_ERROR, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)   log_message(LOG_DEBUG, fmt, ##__VA_ARGS__)

// ����ɫ�汾�ĺ�
#define LOG_INFO_PLAIN(fmt, ...)    log_message_plain(LOG_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARNING_PLAIN(fmt, ...) log_message_plain(LOG_WARNING, fmt, ##__VA_ARGS__)
#define LOG_ERROR_PLAIN(fmt, ...)   log_message_plain(LOG_ERROR, fmt, ##__VA_ARGS__)
#define LOG_DEBUG_PLAIN(fmt, ...)   log_message_plain(LOG_DEBUG, fmt, ##__VA_ARGS__)

// ANSI��ɫ����
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[91m"
#define COLOR_GREEN   "\033[92m"
#define COLOR_YELLOW  "\033[93m"
#define COLOR_BLUE    "\033[94m"

// ��������
void get_timestamp(char* buffer, size_t size);
void get_level_info(LogLevel level, const char** level_str, const char** color);
void log_message(LogLevel level, const char* format, ...);
void log_message_plain(LogLevel level, const char* format, ...);

#endif // UTILS_H