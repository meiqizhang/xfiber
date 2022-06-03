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
#include "util.h"

typedef enum {
    INIT = 0,
    READYING = 1,
    WAITING = 2,
    FINISHED = 3
}FiberStatus;

typedef struct ucontext_t XFiberCtx;


#define SwitchCtx(from, to) \
    swapcontext(from, to)

struct WaitingEvents {
    WaitingEvents() {
        expire_at_ = -1;
    }
    void Reset() {
        expire_at_ = -1;
        waiting_fds_r_.clear();
        waiting_fds_w_.clear();
    }

    // 一个协程中监听的fd不会太多，所以直接用数组
    std::vector<int> waiting_fds_r_;
    std::vector<int> waiting_fds_w_;
    int64_t expire_at_;
};

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

    void TakeOver(int fd);

    bool UnregisterFd(int fd);

    void RegisterWaitingEvents(WaitingEvents &events);

    void SleepMs(int ms);

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
        WaitingFibers(Fiber *r = nullptr, Fiber *w = nullptr) {
            r_ = r;
            w_ = w;
        }
    };

    std::map<int, WaitingFibers> io_waiting_fibers_;
    // 会不会出现一个fd的读/写被多个协程监听？？不会！
    // 但是一个fiber可能会监听多个fd，实际也不存在，一个连接由一个协程处理

    std::map<int64_t, std::set<Fiber *>> expire_events_;

    std::vector<Fiber *> finished_fibers_;
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

    struct FdEvent {
        int fd_;
        int64_t expired_at_;

        FdEvent(int fd =-1, int64_t expired_at=-1) {
            if (expired_at <= 0) {
                expired_at = -1;
            }
            fd_ = fd;
            expired_at_ = expired_at;
        }
    };

    WaitingEvents &GetWaitingEvents() {
        return waiting_events_;
    }

    void SetWaitingEvent(const WaitingEvents &events);

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

