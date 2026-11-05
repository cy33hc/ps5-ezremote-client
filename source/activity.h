#ifndef EZREMOTE_ACTIVITY_H
#define EZREMOTE_ACTIVITY_H

#include <stdint.h>
#include <string>
#include <vector>

extern bool stop_activity;

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
    ACTIVITY_CANCELLED
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
    uint64_t started;  // Util::GetTick() when the activity began
    uint64_t finished; // when it ended, 0 while running
    int origin;
};

namespace Activities
{
    uint64_t Begin(int kind, const std::string &label, int origin = ACTIVITY_NATIVE);
    void Item(uint64_t id, const std::string &item);
    void Progress(uint64_t id, uint64_t done, uint64_t total);
    void Finish(uint64_t id, int state, const std::string &error = "");

    std::vector<Activity> Snapshot();
    bool AnyRunning();
    const char *KindName(int kind);
    const char *StateName(int state);
    const char *OriginName(int origin);

    // RAII helper: registers on construction and finishes on scope exit, so a
    // thread cannot leak a stale "running" entry on an early return.
    struct Guard
    {
        uint64_t id;
        Guard(int kind, const std::string &label, int origin = ACTIVITY_NATIVE) : id(Begin(kind, label, origin)) {}
        ~Guard() { Finish(id, stop_activity ? ACTIVITY_CANCELLED : ACTIVITY_DONE); }
    };
}

#endif
