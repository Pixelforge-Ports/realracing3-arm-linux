#include <mutex>
#include <vector>

#include "cloudcell_defer.h"

/* Cloudcell owns worker threads (CC_Thread_Class) and some of these facades are
 * reachable from them, so the queue is locked even though the drain is always
 * the game thread. */
static std::mutex g_queue_lock;
static std::vector<std::function<void()>> g_queue;

void cloudcell_defer(std::function<void()> fn)
{
    std::lock_guard<std::mutex> guard(g_queue_lock);
    g_queue.push_back(std::move(fn));
}

void cloudcell_pump(void)
{
    std::vector<std::function<void()>> batch;

    {
        std::lock_guard<std::mutex> guard(g_queue_lock);
        if (g_queue.empty())
            return;
        batch.swap(g_queue);
    }

    /* Drained outside the lock: a callback is free to queue the next request,
     * which is exactly what CC_SyncManager_Class does when one blob completes
     * and the next one is posted. Those land in the following frame's batch
     * rather than extending this one, which keeps a failing endpoint from
     * spinning the whole chain inside a single frame. */
    for (auto &fn : batch)
        fn();
}
