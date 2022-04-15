#pragma once

#include <cstdio>
#include <sstream>
#include <string>

#define LOG(level) \
    XFiber##level(__FILE__, __LINE__, __FUNCTION__, __DATE__, __TIME__).stream()


class BaseLog{
public:
    ~BaseLog() {
        std::string str_newline(stream_.str());
        fprintf(stderr, "%s\n", str_newline.c_str());
    }

    std::ostream& stream() {
         return stream_;
    }

    std::ostringstream stream_;
};

class XFiberDEBUG : public BaseLog {
public:
    XFiberDEBUG(const char* file, int line,const char* function, const char* date, const char* time) {
        stream_ << "[D] [" << date << " " << time << "][" << file <<" - " << function << ":" << line << "] - ";
    }
};

class XFiberINFO : public BaseLog {
public:
    XFiberINFO(const char* file, int line,const char* function, const char* date, const char* time) {
        stream_ << "[I] [" << date << " " << time << "][" << file <<" - " << function << ":" << line << "] - ";
    }
};

class XFiberWARNING : public BaseLog {
public:
    XFiberWARNING(const char* file, int line,const char* function, const char* date, const char* time) {
        stream_ << "[W] [" << date << " " << time << "][" << file <<" - " << function << ":" << line << "] - ";
    }
};

class XFiberERROR : public BaseLog {
public:
    XFiberERROR(const char* file, int line,const char* function, const char* date, const char* time) {
        stream_ << "[E] [" << date << " " << time << "][" << file <<" - " << function << ":" << line << "] - ";
    }
};
