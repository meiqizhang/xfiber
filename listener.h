#pragma once

#include <memory>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include "fd.h"
#include "conn.h"

class Listener : public BaseFD {
public:

    Listener();

    ~Listener();

    std::shared_ptr<Connection> Accept();

    void FromRawFd(int fd);

    static Listener ListenTCP(uint16_t port);

private:
    uint16_t port_;
};