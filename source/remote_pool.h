#ifndef EZREMOTE_REMOTE_POOL_H
#define EZREMOTE_REMOTE_POOL_H

#include "clients/remote_client.h"
#include "config.h"

// Keeps idle RemoteClient connections per site so worker threads can run
// remote operations concurrently instead of sharing one global client.
namespace RemotePool
{
    // Returns a connected client, or nullptr when the site cannot be reached.
    RemoteClient *Acquire(const RemoteSettings &settings);

    // Returns the client to its idle list; destroy it when the connection is
    // no longer usable (failed transfer, dropped session).
    void Release(RemoteClient *client, bool healthy);

    // Closes every idle connection, e.g. on disconnect or shutdown.
    void Clear();
}

#endif
