#pragma once

#include <cstdio>
#include <string>

#define DEBUG_ENABLE    1
#define INFO_ENABLE     1
#define WARNING_ENABLE  1
#define ERROR_ENABLE    1


static std::string log_date() {
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
    return buf;
}

#ifndef __LOG_DATE__
#define __LOG_DATE__ log_date().c_str()
#endif

#if DEBUG_ENABLE
#define LOG_DEBUG(fmt, args...)  fprintf(stderr, "[D][%s][%s %d] " fmt"\n", __LOG_DATE__, __FILE__, __LINE__, ##args);
#else
#define LOG_DEBUG(fmt, ...)
#endif

#if INFO_ENABLE
#define LOG_INFO(fmt, args...)  fprintf(stderr, "[I][%s][%s %d] " fmt"\n", __LOG_DATE__, __FILE__, __LINE__, ##args);
#else
#define LOG_INFO(fmt, ...)
#endif

#if WARNING_ENABLE
#define LOG_WARNING(fmt, args...)  fprintf(stderr, "[W][%s][%s %d] " fmt"\n", __LOG_DATE__, __FILE__, __LINE__, ##args);
#else
#define LOG_WARNING(fmt, ...)
#endif

#if ERROR_ENABLE
#define LOG_ERROR(fmt, args...)  fprintf(stderr, "[E][%s][%s %d] " fmt"\n", __LOG_DATE__, __FILE__, __LINE__, ##args);
#else
#define LOG_ERROR(fmt, ...)
#endif
