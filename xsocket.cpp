#include "xsocket.h"
#include "xfiber.h"


uint32_t Fd::next_seq_ = 0;


Fd::Fd() {
    fd_ = -1;
    seq_ = Fd::next_seq_++;
}

Fd::~Fd() {

}

bool Fd::Available() {
    return fd_ > 0;
}

int Fd::RawFd() {
    return fd_;
}

void Fd::RegisterFdToSched() {
    XFiber *xfiber = XFiber::xfiber();
    xfiber->TakeOver(fd_);
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
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0) {
        LOG_ERROR("try set SO_REUSEADDR failed, msg=%s", strerror(errno));
        exit(-1);
    }

    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        LOG_ERROR("set set listen fd O_NONBLOCK failed, msg=%s", strerror(errno));
        exit(-1);
    }

    //bind
    if (bind(fd, (sockaddr *)&addr, sizeof(sockaddr_in)) < 0) {
        LOG_ERROR("try bind port [%d] failed, msg=%s", port, strerror(errno));
        exit(-1);
    }

    //listen
    if (listen(fd, 10) < 0) {
        LOG_ERROR("try listen port[%d] failed, msg=%s", port, strerror(errno));
        exit(-1);
    }

    Listener listener;
    listener.FromRawFd(fd);

    LOG_INFO("listen %d success...", port);
    XFiber::xfiber()->TakeOver(fd);

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
                LOG_ERROR("try set TCP_NODELAY failed, msg=%s", strerror(errno));
                close(client_fd);
                client_fd = -1;
            }
            xfiber->TakeOver(client_fd);
            return std::shared_ptr<Connection>(new Connection(client_fd));
        }
        else {
            if (errno == EAGAIN) {
                // accept失败，协程切出
                xfiber->RegisterFdWithCurrFiber(fd_, -1, false);
                xfiber->SwitchToSched();
            }
            else if (errno == EINTR) {
                LOG_INFO("accept client connect return interrupt error, ignore and conitnue...");
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
    XFiber::xfiber()->UnregisterFd(fd_);
    LOG_INFO("close fd[%d]", fd_);
    close(fd_);
    fd_ = -1;
}


std::shared_ptr<Connection> Connection::ConnectTCP(const char *ipv4, uint16_t port) {
    int fd = socket(AF_INET,SOCK_STREAM, 0);
 
    struct sockaddr_in svr_addr;
    memset(&svr_addr, 0, sizeof(svr_addr));
    svr_addr.sin_family = AF_INET;
    svr_addr.sin_port = htons(port);  
    svr_addr.sin_addr.s_addr = inet_addr(ipv4);  
 
    //连接服务器，成功返回0，错误返回-1
    if (connect(fd, (struct sockaddr *)&svr_addr, sizeof(svr_addr)) < 0)
    {
        LOG_ERROR("try connect %s:%d failed, msg=%s", ipv4, port, strerror(errno));
        return std::shared_ptr<Connection>(new Connection(-1));
    }

    int nodelay = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
        LOG_ERROR("try set TCP_NODELAY failed, msg=%s", strerror(errno));
        close(fd);
        return std::shared_ptr<Connection>(new Connection(-1));
    }

    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        LOG_ERROR("set set fd[%d] O_NONBLOCK failed, msg=%s", fd, strerror(errno));
        close(fd);
        return std::shared_ptr<Connection>(new Connection(-1));
    }
    LOG_DEBUG("connect %s:%d success with fd[%d]", ipv4, port, fd);
    XFiber::xfiber()->TakeOver(fd);

    return std::shared_ptr<Connection>(new Connection(fd));
}

ssize_t Connection::Write(const char *buf, size_t sz, int timeout_ms) const {
    size_t write_bytes = 0;
    XFiber *xfiber = XFiber::xfiber();
    int64_t expired_at = timeout_ms > 0 ? util::NowMs() + timeout_ms : -1;

    while (write_bytes < sz) {
        int n = write(fd_, buf + write_bytes, sz - write_bytes);
        if (n > 0) {
            write_bytes += n;
            LOG_DEBUG("write to fd[%d] return %d, total send %ld bytes", fd_, n, write_bytes);
        }
        else if (n == 0) {
            LOG_INFO("write to fd[%d] return 0 byte, peer has closed", fd_);
            return 0;
        }
        else {
            if (expired_at > 0 && util::NowMs() >= expired_at) {
                LOG_WARNING("write to fd[%d] timeout after wait %dms", fd_, timeout_ms);
                return 0;
            }
            if (errno != EAGAIN && errno != EINTR) {
                LOG_DEBUG("write to fd[%d] failed, msg=%s", fd_, strerror(errno));
                return -1;
            }
            else if (errno == EAGAIN) {
                LOG_DEBUG("write to fd[%d] return EAGIN, add fd into IO waiting events and switch to sched", fd_);
                xfiber->RegisterFdWithCurrFiber(fd_, expired_at, true);
                xfiber->SwitchToSched();
            }
            else {
                //pass
            }
        }
    }
    LOG_DEBUG("write to fd[%d] for %ld byte(s) success", fd_, sz);
    return sz;
}

ssize_t Connection::Read(char *buf, size_t sz, int timeout_ms) const {
    XFiber *xfiber = XFiber::xfiber();
    int64_t expired_at = timeout_ms > 0 ? util::NowMs() + timeout_ms : -1;

    while (true) {
        int n = read(fd_, buf, sz);
        LOG_DEBUG("read from fd[%d] reutrn %d bytes", fd_, n);
        if (n > 0) {
            return n;
        }
        else if (n == 0) {
            LOG_DEBUG("read from fd[%d] return 0 byte, peer has closed", fd_);
            return 0;
        }
        else {
            if (expired_at > 0 && util::NowMs() >= expired_at) {
                LOG_WARNING("read from fd[%d] timeout after wait %dms", fd_, timeout_ms);
                return 0;
            }
            if (errno != EAGAIN && errno != EINTR) {
                LOG_DEBUG("read from fd[%d] failed, msg=%s", fd_, strerror(errno))
                return -1;
            }
            else if (errno == EAGAIN) {
                LOG_DEBUG("read from fd[%d] return EAGIN, add into waiting/expired events with expired at %ld  and switch to sched", fd_, expired_at);
                xfiber->RegisterFdWithCurrFiber(fd_, expired_at, false);
                xfiber->SwitchToSched();
            }
            else if (errno == EINTR) {
                //pass
            }
        }
    }
    return -1;
}
