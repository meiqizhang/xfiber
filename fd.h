#pragma once

#include <unistd.h>
#include <sys/socket.h>
#include "xfiber.h"

class XFiber;

class BaseFD {
public:
    BaseFD();

    ~BaseFD();

    // bool RegisterToXFiber(XFiber *xfiber);

    // bool UnregisterFromXFiber(XFiber *xfiber);

    // virtual int ReadN(int n);

    // virtual int WriteN(int n);

    static uint32_t next_seq_;

    int RawFd();

protected:
    int fd_;
    int seq_;
};