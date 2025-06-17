#include "Utils.h"

// ��ȡ��ǰʱ���ַ�������ʽ��2025-06-17 11::18::00
void get_timestamp(char* buffer, size_t size) {
    time_t rawtime;
    struct tm timeinfo;

    time(&rawtime);

    // ʹ�ð�ȫ�汾�� localtime
#ifdef _WIN32
    // Windows ƽ̨ʹ�� localtime_s
    if (localtime_s(&timeinfo, &rawtime) != 0) {
        // ���ת��ʧ�ܣ�ʹ��Ĭ��ʱ���ʽ
        strncpy_s(buffer, size, "0000-00-00 00::00::00", _TRUNCATE);
        return;
    }
#else
    // Unix/Linux ƽ̨ʹ�� localtime_r
    if (localtime_r(&rawtime, &timeinfo) == NULL) {
        // ���ת��ʧ�ܣ�ʹ��Ĭ��ʱ���ʽ
        strncpy(buffer, "0000-00-00 00::00::00", size - 1);
        buffer[size - 1] = '\0';
        return;
    }
#endif

    strftime(buffer, size, "%Y-%m-%d %H::%M::%S", &timeinfo);
}

// ��ȡ��־�����ַ�������ɫ
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

// ����ɫ��־�������
void log_message(LogLevel level, const char* format, ...) {
    char timestamp[32];
    const char* level_str;
    const char* color;
    va_list args;

    // ��ȡʱ���
    get_timestamp(timestamp, sizeof(timestamp));

    // ��ȡ������Ϣ
    get_level_info(level, &level_str, &color);

    // �����ʽ������־
    printf("%s[%s][%s]", color, level_str, timestamp);

    // ����û���Ϣ
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("%s\n", COLOR_RESET);
}

// ����ɫ�汾����־���
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