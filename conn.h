#pragma once

#include "fd.h"

class Connection : public BaseFD {
public:
    Connection();

    Connection(int fd);

    ~Connection();

    ssize_t Write(const char *buf, size_t sz, int timeout_ms=-1) const;

    ssize_t Read(char *buf, size_t sz, int timeout_ms=-1) const;
};
