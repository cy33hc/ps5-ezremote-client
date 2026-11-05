#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "remote_pool.h"
#include "actions.h"

namespace
{
    std::mutex pool_mutex;
    std::map<std::string, std::vector<RemoteClient *>> pool_idle;
    std::map<RemoteClient *, std::string> pool_key;

    // Enough to overlap a couple of operations without exhausting the console's
    // session slots.
    const size_t POOL_MAX_PER_SITE = 4;

    std::string PoolKey(const RemoteSettings &settings)
    {
        return std::string(settings.server) + "|" + settings.username + "|" +
               std::to_string(settings.type) + "|" + settings.http_server_type;
    }
}

namespace RemotePool
{
    RemoteClient *Acquire(const RemoteSettings &settings)
    {
        const std::string key = PoolKey(settings);

        while (true)
        {
            RemoteClient *candidate = nullptr;
            {
                std::lock_guard<std::mutex> lock(pool_mutex);
                std::vector<RemoteClient *> &idle = pool_idle[key];
                if (!idle.empty())
                {
                    candidate = idle.back();
                    idle.pop_back();
                }
            }

            if (candidate == nullptr)
                break;

            // The server may have dropped the session while it was idle.
            if (candidate->IsConnected())
                return candidate;

            candidate->Quit();
            delete candidate;
            std::lock_guard<std::mutex> lock(pool_mutex);
            pool_key.erase(candidate);
        }

        RemoteClient *client = Actions::CreateRemoteClient(const_cast<RemoteSettings *>(&settings));
        if (client == nullptr)
            return nullptr;

        if (!client->Connect(settings.server, settings.username, settings.password))
        {
            client->Quit();
            delete client;
            return nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(pool_mutex);
            pool_key[client] = key;
        }
        return client;
    }

    void Release(RemoteClient *client, bool healthy)
    {
        if (client == nullptr)
            return;

        if (!healthy || !client->IsConnected())
        {
            client->Quit();
            delete client;
            return;
        }

        std::string key;
        {
            std::lock_guard<std::mutex> lock(pool_mutex);
            std::map<RemoteClient *, std::string>::iterator it = pool_key.find(client);
            if (it == pool_key.end())
            {
                client->Quit();
                delete client;
                return;
            }
            key = it->second;

            std::vector<RemoteClient *> &idle = pool_idle[key];
            if (idle.size() >= POOL_MAX_PER_SITE)
            {
                pool_key.erase(it);
                client->Quit();
                delete client;
                return;
            }
            idle.push_back(client);
        }
    }

    void Clear()
    {
        std::map<std::string, std::vector<RemoteClient *>> idle;
        {
            std::lock_guard<std::mutex> lock(pool_mutex);
            idle.swap(pool_idle);
        }

        {
            std::lock_guard<std::mutex> lock(pool_mutex);
            pool_key.clear();
        }

        for (std::map<std::string, std::vector<RemoteClient *>>::iterator it = idle.begin(); it != idle.end(); ++it)
        {
            for (size_t i = 0; i < it->second.size(); i++)
            {
                it->second[i]->Quit();
                delete it->second[i];
            }
        }
    }
}
