#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include "xfiber.h"
#include "util.h"


XFiber::XFiber() {
    curr_fiber_ = nullptr;
    efd_ = epoll_create1(0);
    if (efd_ < 0) {
        perror("epoll_create");
        exit(-1);
    }
}

XFiber::~XFiber() {
    close(efd_);
}

XFiberCtx *XFiber::SchedCtx() {
    return &sched_ctx_;
}

void XFiber::WakeupFiber(Fiber *fiber) {
    ready_fibers_.push_back(fiber);
}

void XFiber::CreateFiber(std::function<void ()> run, size_t stack_size, std::string fiber_name) {
    if (stack_size == 0) {
        stack_size = 1024 * 1024;
    }
    Fiber *fiber = new Fiber(run, this, stack_size, fiber_name);
    ready_fibers_.push_back(fiber);
    LOG(DEBUG) << "create a new fiber with id[" << fiber->Seq() << "]";
}

void XFiber::Dispatch() {
    while (true) {
        if (ready_fibers_.size() > 0) {
            running_fibers_ = std::move(ready_fibers_);
            ready_fibers_.clear();
            LOG(DEBUG) << "There are " << running_fibers_.size() << " fiber(s) in ready list, ready to run...";

            for (auto iter = running_fibers_.begin(); iter != running_fibers_.end(); iter++) {
                Fiber *fiber = *iter;
                curr_fiber_ = fiber;
                LOG(DEBUG) << "switch from sched to fiber[" << fiber->Seq() << "]";
                assert(SwitchCtx(SchedCtx(), fiber->Ctx()) == 0);
                curr_fiber_ = nullptr;

                if (fiber->IsFinished()) {
                    LOG(INFO) << "fiber[" << fiber->Seq() << "] finished, free it!";
                    delete fiber;
                }
            }
            running_fibers_.clear();
        }

        uint64_t time_now = NowMs();
        while (!expire_fibers_.empty() && expire_fibers_.begin()->first <= time_now) {
            auto iter = expire_fibers_.begin();
            std::set<ExpireEvents> &eevs = iter->second;
            while (!eevs.empty()) {
                const ExpireEvents &eev = *(eevs.begin());
                LOG(DEBUG) << "fd [" << eev.fd_ << "] has wakedup by expired";
                if (eev.r_ != nullptr) {
                    WakeupFiber(eev.r_);
                    // 如果fiber的fd注册到ep_fd，则还需要注销fd
                    UnregisterFd(eev.fd_, false);
                }
                else if (eev.w_ != nullptr) {
                    WakeupFiber(eev.w_);
                    // 如果fiber的fd注册到ep_fd，则还需要注销fd
                    UnregisterFd(eev.fd_, true);
                }
                else if (eev.curr_ != nullptr) {
                    WakeupFiber(eev.curr_);
                }
                eevs.erase(eevs.begin());
            }
            expire_fibers_.erase(iter);
        }

        #define MAX_EVENT_COUNT 512
        struct epoll_event evs[MAX_EVENT_COUNT];
        int n = epoll_wait(efd_, evs, MAX_EVENT_COUNT, 1);
        if (n < 0) {
            perror("epoll_wait");
            exit(-1);
        }

        for (int i = 0; i < n; i++) {
            struct epoll_event &ev = evs[i];
            int fd = ev.data.fd;

            auto fiber_iter = io_waiting_fibers_.find(fd);
            if (fiber_iter != io_waiting_fibers_.end()) {
                WaitingFibers &waiting_fibers = fiber_iter->second;
                if (ev.events & EPOLLIN) {
                    // wakeup
                    LOG(DEBUG) << "waiting fd[" << fd << "] has fired IN event, wake up pending fiber[" << waiting_fibers.r_->Seq() << "]";
                    WakeupFiber(waiting_fibers.r_);
                    // 唤醒后删除超时任务
                    int64_t read_expired_at = waiting_fibers.r_expired_at_;
                    auto iter = expire_fibers_.find(read_expired_at);
                    if (iter != expire_fibers_.end()) {
                        ExpireEvents eev;
                        eev.fd_ = fd;
                        eev.r_ = waiting_fibers.r_;
                        if (iter->second.find(eev) != iter->second.end()) {
                            iter->second.erase(iter->second.find(eev));
                            LOG(DEBUG) << "remove fd[" << fd << "] from read expire_fibers";
                        }
                     }
                }
                else if (ev.events & EPOLLOUT) {
                    if (waiting_fibers.w_ == nullptr) {
                        LOG(WARNING) << "fd[" << fd << "] has been fired OUT event, but not found any fiber to handle!";
                    }
                    else {
                        LOG(DEBUG) << "waiting fd[" << fd << "] has fired OUT event, wake up pending fiber[" << waiting_fibers.w_->Seq() << "]";
                        WakeupFiber(waiting_fibers.w_);
                        // 唤醒后删除超时任务
                        int64_t write_expired_at = waiting_fibers.w_expired_at_;
                        auto iter = expire_fibers_.find(write_expired_at);
                        if (iter != expire_fibers_.end()) {
                            ExpireEvents eev;
                            eev.fd_ = fd;
                            eev.r_ = waiting_fibers.w_;
                            if (iter->second.find(eev) != iter->second.end()) {
                                iter->second.erase(iter->second.find(eev));
                                LOG(DEBUG) << "remove fd[" << fd << "] from write expire_fibers";
                            }
                        }
                    }
                }
            }
        }
    }
}

void XFiber::Yield() {
    assert(curr_fiber_ != nullptr);
    // 主动切出的后仍然是ready状态，等待下次调度
    ready_fibers_.push_back(curr_fiber_);
    SwitchToSched();
}

void XFiber::SwitchToSched() {
    assert(curr_fiber_ != nullptr);
    LOG(DEBUG) << "swicth to sched";
    assert(SwitchCtx(curr_fiber_->Ctx(), SchedCtx()) == 0);
}

bool XFiber::RegisterFd(int fd, bool is_write, int timeout_ms) {
    /*
        op = 0 读
        op = 1 写
    */
    int64_t expired_at = NowMs() + timeout_ms;
    assert(curr_fiber_ != nullptr);
    auto iter = io_waiting_fibers_.find(fd);
    if (iter == io_waiting_fibers_.end()) {
        WaitingFibers wf;
        if (!is_write) { // 读
            wf.r_ = curr_fiber_;
            wf.r_expired_at_ = expired_at;
            io_waiting_fibers_.insert(std::make_pair(fd, wf));
        }
        else {
            wf.w_ = curr_fiber_;
            wf.w_expired_at_ = expired_at;
            io_waiting_fibers_.insert(std::make_pair(fd, wf));
        }
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.fd = fd;

        if (epoll_ctl(efd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            perror("epoll_ctl");
            exit(-1);
        }
        LOG(DEBUG) << "add fd[" << fd << "] into epoll event success";
    }
    else {
        if (!is_write) {
            iter->second.r_ = curr_fiber_;
            iter->second.r_expired_at_ = expired_at;
        }
        else {
            iter->second.w_ = curr_fiber_;
            iter->second.w_expired_at_ = expired_at;
        }
    }

    if (timeout_ms > 0) {
        uint64_t expire_at = NowMs() + timeout_ms;
        if (expire_fibers_.find(expire_at) == expire_fibers_.end()) {
            expire_fibers_.insert(std::make_pair(expire_at, std::set<ExpireEvents>{}));
        }
        ExpireEvents eev;
        if (!is_write) {
            eev.fd_ = fd;
            eev.r_ = curr_fiber_;
        }
        else {
            eev.fd_ = fd;
            eev.w_ = curr_fiber_;
        }
        expire_fibers_[expire_at].insert(eev);
    }

    return true;
}


void XFiber::UnregisterFd(int fd, bool is_write) {
    auto iter = io_waiting_fibers_.find(fd);
    if (iter != io_waiting_fibers_.end()) {
        if (!is_write) {
            iter->second.r_ = nullptr;
        }
        else {
            iter->second.w_ = nullptr;
        }
        if (iter->second.r_ == nullptr && iter->second.w_ == nullptr) {
            io_waiting_fibers_.erase(iter);
            if (epoll_ctl(efd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
                LOG(ERROR) << "unregister fd[" << fd << "] from epoll efd[" << efd_ << "] failed, msg=" << strerror(errno);
            }
            else {
                LOG(INFO) << "unregister fd[" << fd << "] from epoll efd[" << efd_ << "] success!";
            }
        }
    }
    else {
        LOG(INFO) << "fd[" << fd << "] not register [" << (is_write ? "write" : "read") << "] into sched or has been expired";
    }
}

void XFiber::SleepMs(int ms) {
    uint64_t expire_at = NowMs() + ms;
    if (expire_fibers_.find(expire_at) == expire_fibers_.end()) {
        expire_fibers_.insert(std::make_pair(expire_at, std::set<ExpireEvents>{}));
    }
    ExpireEvents eev;
    eev.curr_ = curr_fiber_;
    expire_fibers_[expire_at].insert(eev);

    SwitchCtx(curr_fiber_->Ctx(), &sched_ctx_);
}

thread_local uint64_t fiber_seq = 0;

Fiber::Fiber(std::function<void ()> run, XFiber *xfiber, size_t stack_size, std::string fiber_name) {
    run_ = run;
    xfiber_ = xfiber;
    fiber_name_ = fiber_name;
    stack_size_ = stack_size;
    stack_ptr_ = new uint8_t[stack_size_];
    
    getcontext(&ctx_);
    ctx_.uc_stack.ss_sp = stack_ptr_;
    ctx_.uc_stack.ss_size = stack_size_;
    ctx_.uc_link = xfiber->SchedCtx();
    makecontext(&ctx_, (void (*)())Fiber::Start, 1, this);

    seq_ = fiber_seq++;
    status_ = FiberStatus::INIT;
}

Fiber::~Fiber() {
    delete []stack_ptr_;
    stack_ptr_ = nullptr;
    stack_size_ = 0;
}
    
uint64_t Fiber::Seq() {
    return seq_;
}

XFiberCtx *Fiber::Ctx() {
    return &ctx_;
}

void Fiber::Start(Fiber *fiber) {
    fiber->run_();
    fiber->status_ = FiberStatus::FINISHED;
    LOG(DEBUG) << "fiber[" << fiber->Seq() << "] finished...";
}

std::string Fiber::Name() {
    return fiber_name_;
}

bool Fiber::IsFinished() {
    return status_ == FiberStatus::FINISHED;
}
