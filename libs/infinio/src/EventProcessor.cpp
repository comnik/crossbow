#include "EventProcessor.hpp"

#include "Logging.hpp"

#include <algorithm>
#include <cerrno>

#include <sys/epoll.h>
#include <sys/eventfd.h>

#define PROCESSOR_LOG(...) INFINIO_LOG("[EventProcessor] " __VA_ARGS__)
#define PROCESSOR_ERROR(...) INFINIO_ERROR("[EventProcessor] " __VA_ARGS__)
#define TASKQUEUE_LOG(...) INFINIO_LOG("[TaskQueue] " __VA_ARGS__)
#define TASKQUEUE_ERROR(...) INFINIO_ERROR("[TaskQueue] " __VA_ARGS__)

namespace crossbow {
namespace infinio {

EventProcessor::EventProcessor(uint64_t pollCycles)
        : mPollCycles(pollCycles) {
    PROCESSOR_LOG("Creating epoll file descriptor");
    mEpoll = epoll_create1(EPOLL_CLOEXEC);
    if (mEpoll == -1) {
        throw std::system_error(errno, std::system_category());
    }
}

EventProcessor::~EventProcessor() {
    // TODO We have to join the poll thread
    // We are not allowed to call join in the same thread as the poll loop is

    if (close(mEpoll)) {
        std::error_code ec(errno, std::system_category());
        PROCESSOR_ERROR("Failed to close the epoll descriptor [error = %1% %2%]", ec, ec.message());
    }
}

void EventProcessor::registerPoll(int fd, EventPoll* poll) {
    PROCESSOR_LOG("Register event poller");
    struct epoll_event event;
    event.data.ptr = poll;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(mEpoll, EPOLL_CTL_ADD, fd, &event)) {
        throw std::system_error(errno, std::system_category());
    }

    mPoller.emplace_back(poll);
}

void EventProcessor::deregisterPoll(int fd, EventPoll* poll) {
    PROCESSOR_LOG("Deregister event poller");
    auto i = std::find(mPoller.begin(), mPoller.end(), poll);
    if (i == mPoller.end()) {
        return;
    }
    mPoller.erase(i);

    if (epoll_ctl(mEpoll, EPOLL_CTL_DEL, fd, nullptr)) {
        throw std::system_error(errno, std::system_category());
    }
}

void EventProcessor::start() {
    mPollThread = std::thread([this] () {
        while (true) {
            doPoll();
        }
    });
}

void EventProcessor::doPoll() {
    for (uint32_t i = 0; i < mPollCycles; ++i) {
        for (auto poller : mPoller) {
            if (poller->poll()) {
                i = 0;
            }
        }
    }

    for (auto poller : mPoller) {
        poller->prepareSleep();
    }

    PROCESSOR_LOG("Going to epoll sleep");
    struct epoll_event events[mPoller.size()];
    auto num = epoll_wait(mEpoll, events, 10, -1);
    PROCESSOR_LOG("Wake up from epoll sleep with %1% events", num);

    for (int i = 0; i < num; ++i) {
        if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN))) {
            PROCESSOR_ERROR("Error has occured on fd");
            continue;
        }

        auto poller = reinterpret_cast<EventPoll*>(events[i].data.ptr);
        poller->wakeup();
    }
}

TaskQueue::TaskQueue(EventProcessor& processor)
        : mProcessor(processor),
          mSleeping(false) {
    mInterrupt = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (mInterrupt == -1) {
        throw std::system_error(errno, std::system_category());
    }

    mProcessor.registerPoll(mInterrupt, this);
}

TaskQueue::~TaskQueue() {
    try {
        mProcessor.deregisterPoll(mInterrupt, this);
    } catch (std::system_error& e) {
        TASKQUEUE_ERROR("Failed to deregister from EventProcessor [error = %1% %2%]", e.code(), e.what());
    }

    if (close(mInterrupt)) {
        std::error_code ec(errno, std::system_category());
        TASKQUEUE_ERROR("Failed to close the event descriptor [error = %1% %2%]", ec, ec.message());
    }
}

void TaskQueue::execute(std::function<void ()> fun, std::error_code& ec) {
    // TODO This can lead to a deadlock because write blocks when the queue is full
    mTaskQueue.write(std::move(fun));
    if (mSleeping.load()) {
        uint64_t counter = 0x1u;
        write(mInterrupt, &counter, sizeof(uint64_t));
    }
}

bool TaskQueue::poll() {
    bool result = false;

    // Process all task from the task queue
    std::function<void()> fun;
    while (mTaskQueue.read(fun)) {
        result = true;
        fun();
    }

    return result;
}

void TaskQueue::prepareSleep() {
    auto wasSleeping = mSleeping.exchange(true);
    if (wasSleeping) {
        return;
    }

    // Poll once more for tasks enqueud in the meantime
    poll();
}

void TaskQueue::wakeup() {
    mSleeping.store(false);

    uint64_t counter = 0;
    read(mInterrupt, &counter, sizeof(uint64_t));
}

} // namespace infinio
} // namespace crossbow