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
    // 1. 加入就绪队列
    ready_fibers_.push_back(fiber);

    // 2. 从等待队列中删除
    std::set<int> waiting_fds;
    auto waiting_events = fiber->GetWaitingEvents();
    for (size_t i = 0; i < waiting_events.waiting_fds_r_.size(); i++) {
        int fd = waiting_events.waiting_fds_r_[i].fd_;
        auto iter = io_waiting_fibers_.find(fd);
        if (iter != io_waiting_fibers_.end()) {
            io_waiting_fibers_.erase(iter);
            waiting_fds.insert(fd);
        }
    }
    for (size_t i = 0; i < waiting_events.waiting_fds_w_.size(); i++) {
        int fd = waiting_events.waiting_fds_w_[i].fd_;
        auto iter = io_waiting_fibers_.find(fd);
        if (iter != io_waiting_fibers_.end()) {
            io_waiting_fibers_.erase(iter);
            waiting_fds.insert(fd);
        }
    }

    // 3. 从超时队列中删除
    Fiber::WaitingEvents &evs = fiber->GetWaitingEvents();
    for (size_t i = 0; i < evs.waiting_fds_r_.size(); i++) {
        int64_t expired_at = evs.waiting_fds_r_[i].expired_at_;
        if (expired_at > 0) {
            auto expired_iter = expired_events_.find(expired_at);
            if (expired_iter->second.find(fiber) == expired_iter->second.end()) {
                LOG_ERROR("not fiber [%lu] in expired events", fiber->Seq());
            }
            else {
                expired_iter->second.erase(fiber);
            }
        }
    }

    for (size_t i = 0; i < evs.waiting_fds_w_.size(); i++) {
        int64_t expired_at = evs.waiting_fds_w_[i].expired_at_;
        if (expired_at > 0) {
            auto expired_iter = expired_events_.find(expired_at);
            if (expired_iter->second.find(fiber) == expired_iter->second.end()) {
                LOG_ERROR("not fiber [%lu] in expired events", fiber->Seq());
            }
            else {
                expired_iter->second.erase(fiber);
            }
        }
    }
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
        while (!expired_events_.empty() && expired_events_.begin()->first <= now_ms) {
            std::set<Fiber *> &expired_fibers = expired_events_.begin()->second;
            while (!expired_fibers.empty()) {
                std::set<Fiber *>::iterator expired_fiber = expired_fibers.begin();
                WakeupFiber(*expired_fiber);
            }
            expired_events_.erase(expired_events_.begin());
        }

        #define MAX_EVENT_COUNT 512
        struct epoll_event evs[MAX_EVENT_COUNT];
        int n = epoll_wait(efd_, evs, MAX_EVENT_COUNT, 2);
        if (n < 0) {
            LOG_ERROR("epoll_wait erorr, msg=%s", strerror(errno));
            continue;
        }

        for (int i = 0; i < n; i++) {
            struct epoll_event &ev = evs[i];
            int fd = ev.data.fd;

            auto fiber_iter = io_waiting_fibers_.find(fd);
            if (fiber_iter != io_waiting_fibers_.end()) {
                WaitingFiber &waiting_fiber = fiber_iter->second;
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
    LOG_DEBUG("swicth to sched");
    assert(SwitchCtx(curr_fiber_->Ctx(), SchedCtx()) == 0);
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

bool XFiber::RegisterFdWithCurrFiber(int fd, int64_t expired_at, bool is_write) {
    /*
        op = 0 读
        op = 1 写
    */

    assert(curr_fiber_ != nullptr);
    if (expired_at > 0) {
        expired_events_[expired_at].insert(curr_fiber_);
    }

    auto iter = io_waiting_fibers_.find(fd);
    if (iter == io_waiting_fibers_.end()) {
        WaitingFiber wf;
        if (!is_write) { // 读
            wf.r_ = curr_fiber_;
            io_waiting_fibers_.insert(std::make_pair(fd, wf));
            curr_fiber_->SetReadEvent(Fiber::FdEvent(fd, expired_at));
        }
        else {
            wf.w_ = curr_fiber_;
            io_waiting_fibers_.insert(std::make_pair(fd, wf));
            curr_fiber_->SetWriteEvent(Fiber::FdEvent(fd, expired_at));
        }
    }
    else {
        if (!is_write) {
            iter->second.r_ = curr_fiber_;
            curr_fiber_->SetReadEvent(Fiber::FdEvent(fd, expired_at));
        }
        else {
            iter->second.w_ = curr_fiber_;
            curr_fiber_->SetWriteEvent(Fiber::FdEvent(fd, expired_at));
        }
    }
    return true;
}

bool XFiber::UnregisterFd(int fd) {
    auto iter = io_waiting_fibers_.find(fd);
    if (iter != io_waiting_fibers_.end()) {
        WaitingFiber &waiting_fibers = iter->second;
        Fiber *fiber_r = waiting_fibers.r_;
        Fiber *fiber_w = waiting_fibers.w_;

        if (fiber_r != nullptr) {
            Fiber::WaitingEvents &evs_r = fiber_r->GetWaitingEvents();
            for (size_t i = 0; i < evs_r.waiting_fds_r_.size(); i++) {
                if (evs_r.waiting_fds_r_[i].fd_ == fd) {
                    int64_t expired_at = evs_r.waiting_fds_r_[i].expired_at_;
                    if (expired_at > 0) {
                        auto expired_iter = expired_events_.find(expired_at);
                        if (expired_iter->second.find(fiber_r) == expired_iter->second.end()) {
                            LOG_ERROR("not fiber [%lu] in expired events", fiber_r->Seq());
                        }
                        else {
                            expired_iter->second.erase(fiber_r);
                        }
                    }
                }
            }
        }

        if (fiber_w != nullptr) {
            Fiber::WaitingEvents &evs_w = fiber_w->GetWaitingEvents();
            for (size_t i = 0; i < evs_w.waiting_fds_w_.size(); i++) {
                if (evs_w.waiting_fds_w_[i].fd_ == fd) {
                    int64_t expired_at = evs_w.waiting_fds_w_[i].expired_at_;
                    if (expired_at > 0) {
                        auto expired_iter = expired_events_.find(expired_at);
                        if (expired_iter->second.find(fiber_w) == expired_iter->second.end()) {
                            LOG_ERROR("not fiber [%lu] in expired events", fiber_w->Seq());
                        }
                        else {
                            expired_iter->second.erase(fiber_r);
                        }
                    }
                }
            }
        }
        io_waiting_fibers_.erase(iter);
    }
    else {
        LOG_INFO("fd[%d] not register into sched", fd);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;

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

