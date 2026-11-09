#include <atomic>
#include <mutex>

#include "activity.h"
#include "lang.h"
#include "util.h"

thread_local uint64_t activity_current_id = 0;
thread_local int activity_overwrite_type = -1;

namespace
{
    std::atomic<bool> shutdown_requested{false};
}

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
    uint64_t Begin(int kind, int label_id, int origin, int scope)
    {
        return Begin(kind, std::string(lang_strings[label_id]), origin, scope);
    }

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
        activity.cancellable = true;
        activity.items_done = 0;
        activity.items_total = 0;
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

    void ItemCurrent(const std::string &item)
    {
        if (activity_current_id == 0)
            return;

        std::lock_guard<std::mutex> lock(activity_mutex);
        Activity *activity = Find(activity_current_id);
        if (activity != nullptr)
            activity->item = item;
    }

    void CountCurrent(uint32_t done, uint32_t total)
    {
        if (activity_current_id == 0)
            return;

        std::lock_guard<std::mutex> lock(activity_mutex);
        Activity *activity = Find(activity_current_id);
        if (activity != nullptr)
        {
            activity->items_done = done;
            activity->items_total = total;
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

    void Fail(uint64_t id, const std::string &error)
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        Activity *activity = Find(id);
        if (activity == nullptr || activity->state != ACTIVITY_RUNNING)
            return;

        activity->state = ACTIVITY_FAILED;
        activity->finished = Util::GetTick();
        if (!error.empty())
            activity->item = error;
    }

    void Partial(uint64_t id, const std::string &error)
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        Activity *activity = Find(id);
        if (activity == nullptr || activity->state != ACTIVITY_RUNNING)
            return;

        activity->state = ACTIVITY_PARTIAL;
        activity->finished = Util::GetTick();
        if (!error.empty())
            activity->item = error;
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
                // A phase that cannot be interrupted refuses the request.
                if (!activity_list[i].cancellable)
                    return false;

                activity_list[i].cancel = true;
                return true;
            }
        }
        return false;
    }

    void SetCancellable(bool cancellable)
    {
        std::lock_guard<std::mutex> lock(activity_mutex);
        for (size_t i = 0; i < activity_list.size(); i++)
        {
            if (activity_list[i].id == activity_current_id)
            {
                activity_list[i].cancellable = cancellable;
                return;
            }
        }
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

    void Shutdown()
    {
        shutdown_requested = true;
    }

    bool ShutdownRequested()
    {
        return shutdown_requested;
    }

    bool Stopped()
    {
        if (shutdown_requested)
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
        // Localised for the console; the web reads the numeric kind and applies
        // its own dictionary, so this never reaches the API as an identifier.
        switch (kind)
        {
        case ACTIVITY_COPY:
            return lang_strings[STR_COPY];
        case ACTIVITY_MOVE:
            return lang_strings[STR_MOVE];
        case ACTIVITY_DELETE:
            return lang_strings[STR_DELETE];
        case ACTIVITY_COMPRESS:
            return lang_strings[STR_COMPRESS];
        case ACTIVITY_EXTRACT:
            return lang_strings[STR_EXTRACT];
        case ACTIVITY_UPLOAD:
            return lang_strings[STR_UPLOAD];
        case ACTIVITY_DOWNLOAD:
            return lang_strings[STR_DOWNLOAD];
        case ACTIVITY_INSTALL:
            return lang_strings[STR_INSTALL];
        default:
            return "Activity";
        }
    }

    const char *OriginName(int origin)
    {
        return origin == ACTIVITY_WEB ? lang_strings[STR_ORIGIN_WEB] : lang_strings[STR_ORIGIN_NATIVE];
    }

    const char *StateName(int state)
    {
        switch (state)
        {
        case ACTIVITY_RUNNING:
            return lang_strings[STR_STATE_RUNNING];
        case ACTIVITY_DONE:
            return lang_strings[STR_STATE_DONE];
        case ACTIVITY_FAILED:
            return lang_strings[STR_STATE_FAILED];
        case ACTIVITY_PARTIAL:
            return lang_strings[STR_STATE_PARTIAL];
        case ACTIVITY_CANCELLED:
            return lang_strings[STR_STATE_CANCELLED];
        default:
            return lang_strings[STR_STATE_FAILED];
        }
    }
}
