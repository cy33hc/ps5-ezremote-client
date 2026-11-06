#include <mutex>

#include "activity.h"
#include "util.h"

thread_local uint64_t activity_current_id = 0;
thread_local int activity_overwrite_type = -1;

extern int overwrite_type;

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
    uint64_t Begin(int kind, const std::string &label, int origin, int scope)
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
        activity.cancel = false;
        activity.origin = origin;
        activity.scope = scope;
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

    void ProgressCurrent(uint64_t done, uint64_t total)
    {
        // Prefer the activity owned by this thread, so concurrent operations
        // never report into each other.
        if (activity_current_id != 0)
        {
            Progress(activity_current_id, done, total);
            return;
        }

        std::lock_guard<std::mutex> lock(activity_mutex);
        for (size_t i = activity_list.size(); i-- > 0;)
        {
            if (activity_list[i].state == ACTIVITY_RUNNING)
            {
                activity_list[i].done = done;
                activity_list[i].total = total;
                return;
            }
        }
    }

    bool CurrentRunning(Activity *out)
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        for (size_t i = activity_list.size(); i-- > 0;)
        {
            if (activity_list[i].state == ACTIVITY_RUNNING)
            {
                if (out != nullptr)
                    *out = activity_list[i];
                return true;
            }
        }
        return false;
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

    int ClearFinished()
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        int removed = 0;
        for (size_t i = activity_list.size(); i-- > 0;)
        {
            if (activity_list[i].state != ACTIVITY_RUNNING)
            {
                activity_list.erase(activity_list.begin() + i);
                removed++;
            }
        }
        return removed;
    }

    void SetOverwriteType(int type)
    {
        activity_overwrite_type = type;
    }

    int OverwriteType()
    {
        return activity_overwrite_type >= 0 ? activity_overwrite_type : overwrite_type;
    }

    void SnapshotOverwrite()
    {
        activity_overwrite_type = overwrite_type;
    }

    bool Cancel(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        for (size_t i = 0; i < activity_list.size(); i++)
        {
            if (activity_list[i].id == id && activity_list[i].state == ACTIVITY_RUNNING)
            {
                activity_list[i].cancel = true;
                return true;
            }
        }
        return false;
    }

    bool IsCancelled(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        for (size_t i = 0; i < activity_list.size(); i++)
        {
            if (activity_list[i].id == id)
                return activity_list[i].cancel;
        }
        return false;
    }

    bool Stopped()
    {
        if (stop_activity)
            return true;
        return IsCancelled(activity_current_id);
    }

    bool AnyLocalRunning()
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        for (size_t i = 0; i < activity_list.size(); i++)
        {
            if (activity_list[i].state == ACTIVITY_RUNNING && activity_list[i].scope == ACTIVITY_LOCAL)
                return true;
        }
        return false;
    }

    bool AnyRemoteRunning()
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        for (size_t i = 0; i < activity_list.size(); i++)
        {
            if (activity_list[i].state == ACTIVITY_RUNNING && activity_list[i].scope == ACTIVITY_REMOTE)
                return true;
        }
        return false;
    }

    bool Latest(Activity *out)
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        if (activity_list.empty())
            return false;
        if (out != nullptr)
            *out = activity_list.back();
        return true;
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
