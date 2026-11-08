#ifndef EZREMOTE_ACTIVITY_H
#define EZREMOTE_ACTIVITY_H

#include <stdint.h>
#include <string>
#include <vector>

// Id of the activity owned by the calling thread, set by Guard.
extern thread_local uint64_t activity_current_id;

// Overwrite policy snapshot for the calling thread; -1 means "use the global".
extern thread_local int activity_overwrite_type;

// Background work shared by the native UI and the web UI: copy, move, delete,
// compress, extract, upload, download and install. Every worker thread registers
// itself here so both front ends can show what is running and warn before exit.
enum ActivityKind
{
    ACTIVITY_COPY,
    ACTIVITY_MOVE,
    ACTIVITY_DELETE,
    ACTIVITY_COMPRESS,
    ACTIVITY_EXTRACT,
    ACTIVITY_UPLOAD,
    ACTIVITY_DOWNLOAD,
    ACTIVITY_INSTALL
};

// Which front end started the work.
// Which resource class the work occupies; local and remote operations can run
// at the same time, remote ones contend on the site connection.
enum ActivityScope
{
    ACTIVITY_LOCAL,
    ACTIVITY_REMOTE
};

enum ActivityOrigin
{
    ACTIVITY_NATIVE,
    ACTIVITY_WEB
};

enum ActivityState
{
    ACTIVITY_RUNNING,
    ACTIVITY_DONE,
    ACTIVITY_FAILED,
    ACTIVITY_CANCELLED,
    ACTIVITY_PARTIAL // some items succeeded, some failed
};

struct Activity
{
    uint64_t id;
    int kind;
    int state;
    std::string label;
    std::string item;
    uint64_t done;
    uint64_t total;
    bool cancel;       // requested by the user
    bool cancellable;  // false during a phase that cannot be interrupted
    uint64_t started;  // Util::GetTick() when the activity began
    uint64_t finished; // when it ended, 0 while running
    int origin;
    int scope;
};

namespace Activities
{
    // Process shutdown: every running activity stops.
    void Shutdown();
    bool ShutdownRequested();

    uint64_t Begin(int kind, const std::string &label, int origin = ACTIVITY_NATIVE, int scope = ACTIVITY_LOCAL);
    // Same, but the label is a lang.h string id (STR_*), resolved through
    // lang_strings so the console and the web show it translated.
    uint64_t Begin(int kind, int label_id, int origin, int scope);

    // Overwrite policy for the calling thread; jobs keep their own snapshot so
    // one operation cannot change what another does with existing files.
    int OverwriteType();
    // Switches this thread's policy, e.g. after "always overwrite".
    void SetOverwriteType(int type);
    void SnapshotOverwrite();
    void Item(uint64_t id, const std::string &item);
    // Marks the calling thread's activity as interruptible or not, e.g. an
    // install hands off to the system installer, which cannot be stopped.
    void SetCancellable(bool cancellable);
    void Progress(uint64_t id, uint64_t done, uint64_t total);

    // Marks the activity failed with a message; Guard's destructor will not
    // overwrite the state afterwards.
    void Fail(uint64_t id, const std::string &error);

    // Like Fail, but part of the work succeeded.
    void Partial(uint64_t id, const std::string &error);
    // Report progress for the newest running activity; lets legacy code
    // (fs.cpp / installer.cpp) feed the tracker without knowing the id.
    void ProgressCurrent(uint64_t done, uint64_t total);
    bool CurrentRunning(Activity *out);
    void Finish(uint64_t id, int state, const std::string &error = "");

    std::vector<Activity> Snapshot();
    // Drops finished entries; returns how many were removed.
    int ClearFinished();
    // Requests cancellation of one activity.
    bool Cancel(uint64_t id);
    // True when the calling thread's activity should stop.
    bool Stopped();
    // Most recently started entry, used to react to new work.
    bool Latest(Activity *out);
    bool AnyRunning();
    bool IsCancelled(uint64_t id);
    bool AnyLocalRunning();
    bool AnyRemoteRunning();
    const char *KindName(int kind);
    const char *StateName(int state);
    const char *OriginName(int origin);

    // RAII helper: registers on construction and finishes on scope exit, so a
    // thread cannot leak a stale "running" entry on an early return.
    struct Guard
    {
        uint64_t id;
        uint64_t previous;

        Guard(int kind, const std::string &label, int origin = ACTIVITY_NATIVE, int scope = ACTIVITY_LOCAL)
            : id(Begin(kind, label, origin, scope)), previous(activity_current_id)
        {
            SnapshotOverwrite();
            activity_current_id = id;
        }

        Guard(int kind, int label_id, int origin = ACTIVITY_NATIVE, int scope = ACTIVITY_LOCAL)
            : id(Begin(kind, label_id, origin, scope)), previous(activity_current_id)
        {
            SnapshotOverwrite();
            activity_current_id = id;
        }

        void fail(const std::string &error) { Fail(id, error); }
        void partial(const std::string &error) { Partial(id, error); }

        ~Guard()
        {
            Finish(id, (ShutdownRequested() || IsCancelled(id)) ? ACTIVITY_CANCELLED : ACTIVITY_DONE);
            activity_current_id = previous;
        }
    };
}

#endif
