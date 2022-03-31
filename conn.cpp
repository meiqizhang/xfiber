#include "conn.h"

Connection::Connection() {

}

Connection::Connection(int fd) {
    fd_ = fd;
}

Connection::~Connection() {
    LOG(INFO) << "close fd[" << fd_ << "]";
    close(fd_);
    fd_ = -1;
}

int Connection::Write(const std::string &buf) {
    return Write(buf.c_str(), buf.length());
}

int Connection::Write(const char *buf, size_t buf_size) {
    size_t write_bytes = 0;
    XFiber *xfiber = XFiber::xfiber();

    while (write_bytes < buf_size) {
        int n = write(fd_, buf + write_bytes, buf_size - write_bytes);
        if (n > 0) {
            write_bytes += n;
            LOG(DEBUG) << "send to fd[" << fd_ << "] return " << n << ", total send " << write_bytes << " bytes";
        }
        else if (n < 0) {
            if (errno == EAGAIN) {
                LOG(DEBUG) << "write to fd[" << fd_ << "] "
                              "return EAGIN, add fd into IO waiting events and switch to sched";
                xfiber->RegisterFdToSchedWithFiber(fd_, xfiber->CurrFiber(), 1);
                xfiber->SwitchToSchedFiber();
            }
        }
        else if (n == 0) {
            perror("write");
        }
    }
    LOG(DEBU) << "send to fd[" << fd_ << "] for " << buf_size << " byte(s) success";

    return 0;
}

int Connection::Read(std::string &buf) {
    XFiber *xfiber = XFiber::xfiber();

    while (true) {
        char recv_buf[512];
        int n = read(fd_, recv_buf, 512);
        LOG(DEBUG) << "read from fd[" << fd_ << "] reutrn " << n <<  " bytes";
        printf("x%sy\n", recv_buf);
        if (n > 0) {
            buf.append(recv_buf, n);
            return n;
        }
        else if (n == 0) {
            xfiber->UnregisterFdFromSched(fd_);
            return 0;
        }
        else {
            if (errno == EAGAIN) {
                LOG(DEBUG) << "read from fd[" << fd_ << "] "
                              "return EAGIN, add fd into IO waiting events and switch to sched";
                xfiber->RegisterFdToSchedWithFiber(fd_, xfiber->CurrFiber(), 0);
                xfiber->SwitchToSchedFiber();
            }
            else {
                perror("read");
            }
        }
    }
    return buf.size();
}

