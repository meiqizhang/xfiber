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


XFiber::XFiber() {
    curr_fiber_ = nullptr;
    efd_ = epoll_create1(0);
    if (efd_ < 0) {
        LOG_ERROR("epoll_create failed, msg=%s", strerror(errno));
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
    LOG_DEBUG("try wakeup fiber[%lu] %p", fiber->Seq(), fiber);
    // 1. 加入就绪队列
    ready_fibers_.push_back(fiber);

    // 2. 从等待队列中删除
    WaitingEvents &waiting_events = fiber->GetWaitingEvents();
    for (size_t i = 0; i < waiting_events.waiting_fds_r_.size(); i++) {
        int fd = waiting_events.waiting_fds_r_[i];
        auto iter = io_waiting_fibers_.find(fd);
        if (iter != io_waiting_fibers_.end()) {
            io_waiting_fibers_.erase(iter);
        }
    }
    for (size_t i = 0; i < waiting_events.waiting_fds_w_.size(); i++) {
        int fd = waiting_events.waiting_fds_w_[i];
        auto iter = io_waiting_fibers_.find(fd);
        if (iter != io_waiting_fibers_.end()) {
            io_waiting_fibers_.erase(iter);
        }
    }

    // 3. 从超时队列中删除
    int64_t expire_at = waiting_events.expire_at_;
    if (expire_at > 0) {
        auto expired_iter = expire_events_.find(expire_at);
        if (expired_iter->second.find(fiber) == expired_iter->second.end()) {
            LOG_WARNING("not fiber [%lu] in expired events", fiber->Seq());
        }
        else {
            LOG_DEBUG("remove fiber [%lu] from expire events...", fiber->Seq());
            expired_iter->second.erase(fiber);
        }
    }
    LOG_DEBUG("fiber [%lu] %p has wakeup success, ready to run!", fiber->Seq(), fiber);
}

void XFiber::CreateFiber(std::function<void ()> run, size_t stack_size, std::string fiber_name) {
    if (stack_size == 0) {
        stack_size = 1024 * 1024;
    }
    Fiber *fiber = new Fiber(run, this, stack_size, fiber_name);
    ready_fibers_.push_back(fiber);
    LOG_DEBUG("create a new fiber with id[%lu]", fiber->Seq());
}

void XFiber::Dispatch() {
    while (true) {
        if (ready_fibers_.size() > 0) {
            running_fibers_ = std::move(ready_fibers_);
            ready_fibers_.clear();
            LOG_DEBUG("there are %ld fiber(s) in ready list, ready to run...", running_fibers_.size());

            for (auto iter = running_fibers_.begin(); iter != running_fibers_.end(); iter++) {
                Fiber *fiber = *iter;
                curr_fiber_ = fiber;
                LOG_DEBUG("switch from sched to fiber[%lu]", fiber->Seq());
                assert(SwitchCtx(SchedCtx(), fiber->Ctx()) == 0);
                curr_fiber_ = nullptr;

                if (fiber->IsFinished()) {
                    LOG_INFO("fiber[%lu] finished, free it!", fiber->Seq());
                    delete fiber;
                }
            }
            running_fibers_.clear();
        }

        int64_t now_ms = util::NowMs();
        while (!expire_events_.empty() && expire_events_.begin()->first <= now_ms) {
            std::set<Fiber *> &expired_fibers = expire_events_.begin()->second;
            while (!expired_fibers.empty()) {
                std::set<Fiber *>::iterator expired_fiber = expired_fibers.begin();
                WakeupFiber(*expired_fiber);
            }
            expire_events_.erase(expire_events_.begin());
        }

        #define MAX_EVENT_COUNT 512
        struct epoll_event evs[MAX_EVENT_COUNT];
        int n = epoll_wait(efd_, evs, MAX_EVENT_COUNT, 2);
        if (n < 0) {
            LOG_ERROR("epoll_wait error, msg=%s", strerror(errno));
            continue;
        }

        for (int i = 0; i < n; i++) {
            struct epoll_event &ev = evs[i];
            int fd = ev.data.fd;

            auto fiber_iter = io_waiting_fibers_.find(fd);
            if (fiber_iter != io_waiting_fibers_.end()) {
                WaitingFibers &waiting_fiber = fiber_iter->second;
                if (ev.events & EPOLLIN) {
                    LOG_DEBUG("waiting fd[%d] has fired IN event, wake up pending fiber[%lu]", fd, waiting_fiber.r_->Seq());
                    WakeupFiber(waiting_fiber.r_);
                }
                else if (ev.events & EPOLLOUT) {
                    if (waiting_fiber.w_ == nullptr) {
                        LOG_WARNING("fd[%d] has been fired OUT event, but not found any fiber to handle!", fd);
                    }
                    else {
                        LOG_DEBUG("waiting fd[%d] has fired OUT event, wake up pending fiber[%lu]", fd, waiting_fiber.w_->Seq());
                        WakeupFiber(waiting_fiber.w_);
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
    LOG_DEBUG("switch to sched");
    assert(SwitchCtx(curr_fiber_->Ctx(), SchedCtx()) == 0);
}

void XFiber::SleepMs(int ms) {
    if (ms < 0) {
        return;
    }

    int64_t expired_at = util::NowMs() + ms;
    WaitingEvents events;
    events.expire_at_ = expired_at;
    RegisterWaitingEvents(events);
    SwitchToSched();
}

void XFiber::TakeOver(int fd) {
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;

    if (epoll_ctl(efd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        LOG_ERROR("add fd [%d] into epoll failed, msg=%s", fd, strerror(errno));
        exit(-1);
    }
    LOG_DEBUG("add fd[%d] into epoll event success", fd);
}

void XFiber::RegisterWaitingEvents(WaitingEvents &events) {
    assert(curr_fiber_ != nullptr);
    if (events.expire_at_ > 0) {
        expire_events_[events.expire_at_].insert(curr_fiber_);
        curr_fiber_->SetWaitingEvent(events);
        LOG_DEBUG("register fiber [%lu] with expire event at %ld", curr_fiber_->Seq(), events.expire_at_);
    }

    for (size_t i = 0; i < events.waiting_fds_r_.size(); i++) {
        int fd = events.waiting_fds_r_[i];
        auto iter = io_waiting_fibers_.find(fd);
        if (iter == io_waiting_fibers_.end()) {
            io_waiting_fibers_.insert(std::make_pair(fd, WaitingFibers(curr_fiber_, nullptr)));
            curr_fiber_->SetWaitingEvent(events);
        }
    }

    for (size_t i = 0; i < events.waiting_fds_w_.size(); i++) {
        int fd = events.waiting_fds_w_[i];
        auto iter = io_waiting_fibers_.find(fd);
        if (iter == io_waiting_fibers_.end()) {
            io_waiting_fibers_.insert(std::make_pair(fd, WaitingFibers(nullptr, curr_fiber_)));
            curr_fiber_->SetWaitingEvent(events);
        }
    }
}

bool XFiber::UnregisterFd(int fd) {
    LOG_DEBUG("unregister fd[%d] from sheduler", fd);
    auto io_waiting_fibers_iter = io_waiting_fibers_.find(fd);
    // assert(io_waiting_fibers_iter != io_waiting_fibers_.end());

    if (io_waiting_fibers_iter != io_waiting_fibers_.end()) {
        WaitingFibers &waiting_fibers = io_waiting_fibers_iter->second;
        if (waiting_fibers.r_ != nullptr) {
            WakeupFiber(waiting_fibers.r_);
        }
        if (waiting_fibers.w_ != nullptr) {
            WakeupFiber(waiting_fibers.w_);
        }

        io_waiting_fibers_.erase(io_waiting_fibers_iter);
    }

    struct epoll_event ev;
    if (epoll_ctl(efd_, EPOLL_CTL_DEL, fd, &ev) < 0) {
        LOG_ERROR("unregister fd[%d] from epoll efd[%d] failed, msg=%s", fd, efd_, strerror(errno));
    }
    else {
        LOG_INFO("unregister fd[%d] from epoll efd[%d] success!", fd, efd_);
    }
    return true;
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
    LOG_DEBUG("fiber[%lu] finished...", fiber->Seq());
}

std::string Fiber::Name() {
    return fiber_name_;
}

bool Fiber::IsFinished() {
    return status_ == FiberStatus::FINISHED;
}

void Fiber::SetWaitingEvent(const WaitingEvents &events) {
    for (size_t i = 0; i < events.waiting_fds_r_.size(); i++) {
        waiting_events_.waiting_fds_r_.push_back(events.waiting_fds_r_[i]);
    }
    for (size_t i = 0; i < events.waiting_fds_w_.size(); i++) {
        waiting_events_.waiting_fds_w_.push_back(events.waiting_fds_w_[i]);
    }
    if (events.expire_at_ > 0) {
        waiting_events_.expire_at_ = events.expire_at_;
    }
}

