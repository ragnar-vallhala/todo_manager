// Long polling for room messages: a request can wait until a room gets a new
// message instead of asking over and over.
#pragma once

#include "TodoStore.h"

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

class RoomHub
{
  public:
    using Callback = std::function<void(std::vector<Message>)>;

    static RoomHub &get();

    // Calls `cb` with the room's messages after `since` (at most `limit`):
    // right away if there are some or `timeout` is 0, else as soon as one is
    // posted, or with none after `timeout` seconds. `cb` may run on any thread.
    void wait(int64_t roomId, int64_t since, size_t limit, double timeout, Callback cb);

    // Wakes the waiters on `roomId`. Safe to call with the store locked: the
    // work happens later on the main event loop.
    void notify(int64_t roomId);

  private:
    struct Waiter
    {
        int64_t roomId, since;
        size_t limit;
        Callback cb;
    };
    // Answers `w` unless it has been answered already.
    void fire(const std::shared_ptr<Waiter> &w);

    std::mutex mu_;
    std::list<std::shared_ptr<Waiter>> waiters_;
};
