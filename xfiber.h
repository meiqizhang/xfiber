#pragma once

#include <set>
#include <map>
#include <list>
#include <queue>
#include <vector>
#include <string>
#include <functional>
#include <ucontext.h>

#include "log.h"

typedef enum {
    INIT = 0,
    READYING = 1,
    WAITING = 2,
    FINISHED = 3
}FiberStatus;

typedef struct ucontext_t XFiberCtx;


#define SwitchCtx(from, to) \
    swapcontext(from, to)


class Fiber;

class XFiber {
public:
    XFiber();

    ~XFiber();

    void WakeupFiber(Fiber *fiber);

    void CreateFiber(std::function<void()> run, size_t stack_size = 0, std::string fiber_name="");

    void Dispatch();

    void Yield();

    void SwitchToSched();

    bool RegisterFd(int fd, int64_t expired_at, bool is_write);

    bool UnregisterFd(int fd);

    XFiberCtx *SchedCtx();

    static XFiber *xfiber() {
        static thread_local XFiber xf;
        return &xf;
    }

private:
    int efd_;
    
    std::deque<Fiber *> ready_fibers_;

    std::deque<Fiber *> running_fibers_;

    XFiberCtx sched_ctx_;

    Fiber *curr_fiber_;

    struct WaitingFiber {
        Fiber *r_, *w_;
        WaitingFiber() {
            r_ = nullptr;
            w_ = nullptr;
        }
    };

    std::map<int, WaitingFiber> io_waiting_fibers_;
    // 会不会出现一个fd的读/写被多个协程监听？？不会！
    // 但是一个fiber可能会监听多个fd，实际也不存在，一个连接由一个协程处理

    std::map<int64_t, std::set<Fiber *>> expired_events_;
};


class Fiber
{
public:
    Fiber(std::function<void ()> run, XFiber *xfiber, size_t stack_size, std::string fiber_name);

    ~Fiber();

    XFiberCtx *Ctx();

    std::string Name();

    bool IsFinished();
    
    uint64_t Seq();

    static void Start(Fiber *fiber);

    struct WaitingFd {
        int fd_;
        int64_t expired_at_;

        WaitingFd(int fd =-1, int64_t expired_at=-1) {
            fd_ = fd;
            expired_at_ = expired_at;
        }
    };

    struct WaitingEvents {
        // 一个协程中监听的fd不会太多，所以直接用数组
        std::vector<WaitingFd> waiting_fds_r_;
        std::vector<WaitingFd> waiting_fds_w_;
    };

    WaitingEvents &GetWaitingEvents() {
        return waiting_events_;
    }

    void SetReadEvent(const WaitingFd &waiting_fd) {
        for (size_t i = 0; i < waiting_events_.waiting_fds_r_.size(); ++i) {
            if (waiting_events_.waiting_fds_r_[i].fd_ == waiting_fd.fd_) {
                waiting_events_.waiting_fds_r_[i].expired_at_ = waiting_fd.expired_at_;
                return ;
            }
        }
        waiting_events_.waiting_fds_r_.push_back(waiting_fd);
    }

    void SetWriteEvent(const WaitingFd &waiting_fd) {
        for (size_t i = 0; i < waiting_events_.waiting_fds_w_.size(); ++i) {
            if (waiting_events_.waiting_fds_w_[i].fd_ == waiting_fd.fd_) {
                waiting_events_.waiting_fds_w_[i].expired_at_ = waiting_fd.expired_at_;
                return ;
            }
        }
        waiting_events_.waiting_fds_w_.push_back(waiting_fd);
    }



private:
    uint64_t seq_;

    XFiber *xfiber_;

    std::string fiber_name_;

    FiberStatus status_;

    ucontext_t ctx_;

    uint8_t *stack_ptr_;

    size_t stack_size_;
    
    std::function<void ()> run_;

    WaitingEvents waiting_events_;

};

