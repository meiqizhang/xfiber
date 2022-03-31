#pragma once


#include <cstdio>
#include <sstream>
#include <string>

#define LOG(level) \
    XFiberLog(__FILE__, __LINE__, __FUNCTION__, __DATE__, __TIME__).stream()


class XFiberLog{
public:
    XFiberLog(const char* file, int line,const char* function, const char* date, const char* time) {
        stream_ << "[" << date << " " << time << "][" << file <<" - " << function << ":" << line << "] [INFO] - ";
    }

    ~XFiberLog() {
        stream_ << std::endl;
        std::string str_newline(stream_.str());
        fprintf(stderr, "%s", str_newline.c_str());
        fflush(stderr);
    }

    std::ostream& stream() { return stream_;}

private:

    std::ostringstream stream_;
};