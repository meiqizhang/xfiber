#include "conn.h"
#include <cstring>

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
                xfiber->RegisterFdToSchedWithFiber(fd_, xfiber->CurrFiber(), 1);
                xfiber->SwitchToSchedFiber();
            }
            else {
                //pass
            }
        }

    }
    LOG(DEBU) << "write to fd[" << fd_ << "] for " << sz << " byte(s) success";
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
                xfiber->RegisterFdToSchedWithFiber(fd_, xfiber->CurrFiber(), 0);
                xfiber->SwitchToSchedFiber();
            }
            else if (errno == EINTR) {
                //pass
            }
        }
    }
    return -1;
}

