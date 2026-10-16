#include "RoomHub.h"

#include <drogon/drogon.h>

#include <algorithm>

RoomHub &RoomHub::get()
{
    static RoomHub hub;
    return hub;
}

void RoomHub::wait(int64_t roomId, int64_t since, size_t limit, double timeout, Callback cb)
{
    auto now = TodoStore::get().messages(roomId, since, limit);
    if (!now.empty() || timeout <= 0)
        return cb(std::move(now));

    auto w = std::make_shared<Waiter>(Waiter{roomId, since, limit, std::move(cb)});
    {
        std::lock_guard<std::mutex> lock(mu_);
        waiters_.push_back(w);
    }
    std::weak_ptr<Waiter> weak = w;
    drogon::app().getLoop()->runAfter(timeout, [this, weak] {
        if (auto w = weak.lock())
            fire(w);
    });
    // A message posted between the first look and registering would
    // otherwise be missed until the timeout.
    if (!TodoStore::get().messages(roomId, since, 1).empty())
        fire(w);
}

void RoomHub::notify(int64_t roomId)
{
    drogon::app().getLoop()->queueInLoop([this, roomId] {
        std::vector<std::shared_ptr<Waiter>> ready;
        {
            std::lock_guard<std::mutex> lock(mu_);
            for (const auto &w : waiters_)
                if (w->roomId == roomId)
                    ready.push_back(w);
        }
        for (const auto &w : ready)
            fire(w);
    });
}

void RoomHub::fire(const std::shared_ptr<Waiter> &w)
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = std::find(waiters_.begin(), waiters_.end(), w);
        if (it == waiters_.end())
            return;  // already answered
        waiters_.erase(it);
    }
    w->cb(TodoStore::get().messages(w->roomId, w->since, w->limit));
}
