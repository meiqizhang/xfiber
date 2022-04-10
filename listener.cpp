#include <fcntl.h>
#include "listener.h"
#include "xfiber.h"

Listener::Listener() {

}

Listener::~Listener() {
    close(fd_);
}


Listener Listener::ListenTCP(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        exit(-1);
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int flag = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    fcntl(fd, F_SETFL, O_NONBLOCK);

    //bind
    if (bind(fd, (sockaddr *)&addr, sizeof(sockaddr_in)) < 0) {
        perror("bind");
        exit(-1);
    }

    //listen
    if (listen(fd, 10) < 0) {
        perror("listen");
        exit(-1);
    }

    Listener listener;
    listener.FromRawFd(fd);
    return listener;
}

void Listener::FromRawFd(int fd) {
    fd_ = fd;
}

std::shared_ptr<Connection> Listener::Accept() {
    XFiber *xfiber = XFiber::xfiber();

    while (true) {
        int client_fd = accept(fd_, nullptr, nullptr);
        if (client_fd > 0) {
            if (fcntl(client_fd, F_SETFL, O_NONBLOCK) != 0) {
                perror("fcntl");
                exit(-1);
            }
            return std::shared_ptr<Connection>(new Connection(client_fd));
        }
        else {
            if (errno == EAGAIN) {
                // accept失败，协程切出
                xfiber->RegisterFdToSchedWithFiber(fd_, xfiber->CurrFiber(), 0);
                xfiber->SwitchToSchedFiber();
            }
            else if (errno == EINTR) {
                LOG(INFO) << "accept client connect return interrupt error, ignore and conitnue...";
            }
            else {
                perror("accept");
            }
        }
    }
    return std::shared_ptr<Connection>(new Connection(-1));
}

