#include <mutex>

#include "activity.h"
#include "util.h"

namespace
{
    std::mutex activity_mutex;
    std::vector<Activity> activity_list;
    uint64_t activity_next_id = 1;

    // Finished entries are kept so both UIs can show a short history, but the
    // list must not grow forever.
    const size_t ACTIVITY_MAX_ENTRIES = 50;

    Activity *Find(uint64_t id)
    {
        for (size_t i = 0; i < activity_list.size(); i++)
        {
            if (activity_list[i].id == id)
                return &activity_list[i];
        }
        return nullptr;
    }
}

namespace Activities
{
    uint64_t Begin(int kind, const std::string &label, int origin)
    {
        std::lock_guard<std::mutex> lock(activity_mutex);

        Activity activity;
        activity.id = activity_next_id++;
        activity.kind = kind;
        activity.state = ACTIVITY_RUNNING;
        activity.label = label;
        activity.done = 0;
        activity.total = 0;
        activity.started = Util::GetTick();
        activity.finished = 0;
        activity.origin = origin;
        activity_list.push_back(activity);

        while (activity_list.size() > ACTIVITY_MAX_ENTRIES)
            activity_list.erase(activity_list.begin());

        return activity.id;
    }

    void Item(uint64_t id, const std::string &item)
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        Activity *activity = Find(id);
        if (activity != nullptr)
            activity->item = item;
    }

    void Progress(uint64_t id, uint64_t done, uint64_t total)
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        Activity *activity = Find(id);
        if (activity != nullptr && activity->state == ACTIVITY_RUNNING)
        {
            activity->done = done;
            activity->total = total;
        }
    }

    void Finish(uint64_t id, int state, const std::string &error)
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        Activity *activity = Find(id);
        if (activity == nullptr || activity->state != ACTIVITY_RUNNING)
            return;

        activity->state = state;
        activity->finished = Util::GetTick();
        if (!error.empty())
            activity->item = error;
        if (state != ACTIVITY_DONE)
            return;
    }

    std::vector<Activity> Snapshot()
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        return activity_list;
    }

    bool AnyRunning()
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        for (size_t i = 0; i < activity_list.size(); i++)
        {
            if (activity_list[i].state == ACTIVITY_RUNNING)
                return true;
        }
        return false;
    }

    const char *KindName(int kind)
    {
        switch (kind)
        {
        case ACTIVITY_COPY:
            return "Copy";
        case ACTIVITY_MOVE:
            return "Move";
        case ACTIVITY_DELETE:
            return "Delete";
        case ACTIVITY_COMPRESS:
            return "Compress";
        case ACTIVITY_EXTRACT:
            return "Extract";
        case ACTIVITY_UPLOAD:
            return "Upload";
        case ACTIVITY_DOWNLOAD:
            return "Download";
        case ACTIVITY_INSTALL:
            return "Install";
        default:
            return "Activity";
        }
    }

    const char *OriginName(int origin)
    {
        return origin == ACTIVITY_WEB ? "Web" : "Native";
    }

    const char *StateName(int state)
    {
        switch (state)
        {
        case ACTIVITY_RUNNING:
            return "Running";
        case ACTIVITY_DONE:
            return "Done";
        case ACTIVITY_FAILED:
            return "Failed";
        case ACTIVITY_CANCELLED:
            return "Cancelled";
        default:
            return "Unknown";
        }
    }
}
