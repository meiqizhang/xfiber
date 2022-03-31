#pragma once

#include "fd.h"

class Connection : public BaseFD {
public:
    Connection();

    Connection(int fd);

    ~Connection();

    // int ReadN(int n) override;

    // int WriteN(int n) override;

    int Write(const std::string &buf);

    int Write(const char *buf, size_t buf_size);

    int Read(std::string &buf);
};
