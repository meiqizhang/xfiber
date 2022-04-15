#include "xsocket.h"
#include "xfiber.h"


uint32_t Fd::next_seq_ = 0;


Fd::Fd() {
    seq_ = Fd::next_seq_++;
}

Fd::~Fd() {

}

int Fd::RawFd() {
    return fd_;
}

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
            int nodelay = 1;
            if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
                perror("setsockopt tcp_nodelay");
                close(client_fd);
                client_fd = -1;
            }

            return std::shared_ptr<Connection>(new Connection(client_fd));
        }
        else {
            if (errno == EAGAIN) {
                // accept失败，协程切出
                xfiber->RegisterFdToCurrFiber(fd_, false);
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


Connection::Connection() {

}

Connection::Connection(int fd) {
    fd_ = fd;
}

Connection::~Connection() {
    XFiber::xfiber()->UnregisterFdFromSched(fd_);
    LOG(INFO) << "close fd[" << fd_ << "]";
    close(fd_);
    fd_ = -1;
}


ssize_t Connection::Write(const char *buf, size_t sz, int timeout_ms) const {
    size_t write_bytes = 0;
    XFiber *xfiber = XFiber::xfiber();

    while (write_bytes < sz) {
        int n = write(fd_, buf + write_bytes, sz - write_bytes);
        if (n > 0) {
            write_bytes += n;
            LOG(DEBUG) << "write to fd[" << fd_ << "] return " << n << ", total send " << write_bytes << " bytes";
        }
        else if (n == 0) {
            LOG(DEBUG) << "write to fd[" << fd_ << "] return 0 byte, peer has closed";
            return 0;
        }
        else {
            if (errno != EAGAIN && errno != EINTR) {
                LOG(DEBUG) << "write to fd[" << fd_ << "] failed, msg=" << strerror(errno);
                return -1;
            }
            else if (errno == EAGAIN) {
                LOG(DEBUG) << "write to fd[" << fd_ << "] "
                              "return EAGIN, add fd into IO waiting events and switch to sched";
                xfiber->RegisterFdToCurrFiber(fd_, true);
                xfiber->SwitchToSchedFiber();
            }
            else {
                //pass
            }
        }

    }
    LOG(DEBUG) << "write to fd[" << fd_ << "] for " << sz << " byte(s) success";
    return 0;
}

ssize_t Connection::Read(char *buf, size_t sz, int timeout_ms) const {
    XFiber *xfiber = XFiber::xfiber();

    while (true) {
        int n = read(fd_, buf, sz);
        LOG(DEBUG) << "read from fd[" << fd_ << "] reutrn " << n <<  " bytes";
        if (n > 0) {
            return n;
        }
        else if (n == 0) {
            LOG(DEBUG) << "read from fd[" << fd_ << "] return 0 byte, peer has closed";
            return 0;
        }
        else {
            if (errno != EAGAIN && errno != EINTR) {
                LOG(DEBUG) << "read from fd[" << fd_ << "] failed, msg=" << strerror(errno);
                return -1;
            }
            else if (errno == EAGAIN) {
                LOG(DEBUG) << "read from fd[" << fd_ << "] "
                              "return EAGIN, add fd into IO waiting events and switch to sched";
                xfiber->RegisterFdToCurrFiber(fd_, false);
                xfiber->SwitchToSchedFiber();
            }
            else if (errno == EINTR) {
                //pass
            }
        }
    }
    return -1;
}
