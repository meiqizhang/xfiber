#pragma once

#include <map>
#include <set>
#include <list>
#include <queue>
#include <vector>
#include <string>
#include <functional>
#include <ucontext.h>
#include <sys/time.h>

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

    void SleepMs(int ms);

    void SwitchToSched();

    bool RegisterFd(int fd, bool is_write, int timeout_ms);

    void UnregisterFd(int fd, bool is_write);

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

    struct WaitingFibers {
        Fiber *r_, *w_;
        int64_t r_expired_at_, w_expired_at_;

        WaitingFibers() {
            r_ = nullptr;
            w_ = nullptr;
            r_expired_at_ = -1;
            w_expired_at_ = -1;
        }
    };

    std::map<int, WaitingFibers> io_waiting_fibers_;

    // 会不会出现一个fd的读/写被多个协程监听？？不会！
    // 但是一个fiber可能会监听多个fd，实际也不存在，一个连接由一个协程处理

    struct ExpireEvents {
        Fiber *r_, *w_;
        Fiber *curr_;
        int fd_;

        ExpireEvents() {
            r_ = nullptr;
            w_ = nullptr;
            curr_ = nullptr;
            fd_ = -1;
        }
        bool operator < (const ExpireEvents &other) const {
            return r_ < other.r_ && w_ < other.w_ && curr_ < other.curr_ && fd_ < other.fd_;
        }

    };
    std::map<uint64_t, std::set<ExpireEvents>> expire_fibers_;
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

private:
    uint64_t seq_;

    XFiber *xfiber_;

    std::string fiber_name_;

    FiberStatus status_;

    ucontext_t ctx_;

    uint8_t *stack_ptr_;

    size_t stack_size_;
    
    std::function<void ()> run_;
};
