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

        #define MAX_EVENT_COUNT 512
        struct epoll_event evs[MAX_EVENT_COUNT];
        int n = epoll_wait(efd_, evs, MAX_EVENT_COUNT, 2);
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
                    ready_fibers_.push_back(waiting_fibers.r_);
                }
                else if (ev.events & EPOLLOUT) {
                    if (waiting_fibers.w_ == nullptr) {
                        LOG(WARNING) << "fd[" << fd << "] has been fired OUT event, but not found any fiber to handle!";
                    }
                    else {
                        LOG(DEBUG) << "waiting fd[" << fd << "] has fired OUT event, wake up pending fiber[" << waiting_fibers.w_->Seq() << "]";
                        ready_fibers_.push_back(waiting_fibers.w_);
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
    SwitchToSchedFiber();
}

void XFiber::SwitchToSchedFiber() {
    assert(curr_fiber_ != nullptr);
    LOG(DEBUG) << "swicth to sched";
    assert(SwitchCtx(curr_fiber_->Ctx(), SchedCtx()) == 0);
}

bool XFiber::RegisterFdToCurrFiber(int fd, bool is_write) {
    /*
        op = 0 读
        op = 1 写
    */
   assert(curr_fiber_ != nullptr);
    auto iter = io_waiting_fibers_.find(fd);
    if (iter == io_waiting_fibers_.end()) {
        WaitingFibers wf;
        if (!is_write) { // 读
            wf.r_ = curr_fiber_;
            io_waiting_fibers_.insert(std::make_pair(fd, wf));
        }
        else {
            wf.w_ = curr_fiber_;
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
        }
        else {
            iter->second.w_ = curr_fiber_;
        }
    }

    return true;
}


bool XFiber::UnregisterFdFromSched(int fd) {
    auto iter = io_waiting_fibers_.find(fd);
    if (iter != io_waiting_fibers_.end()) {
        io_waiting_fibers_.erase(iter);
    }
    else {
        LOG(INFO) << "fd[" << fd << "] not registned into sched";
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;

    if (epoll_ctl(efd_, EPOLL_CTL_DEL, fd, &ev) < 0) {
        //exit(-1);
        LOG(ERROR) << "unregister fd[" << fd << "] from epoll efd[" << efd_ << "] failed, msg=" << strerror(errno);
    }
    else {
        LOG(INFO) << "unregister fd[" << fd << "] from epoll efd[" << efd_ << "] success!";
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
    LOG(DEBUG) << "fiber[" << fiber->Seq() << "] finished...";
}

std::string Fiber::Name() {
    return fiber_name_;
}

bool Fiber::IsFinished() {
    return status_ == FiberStatus::FINISHED;
}

