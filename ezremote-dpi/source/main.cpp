#undef main

#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include "config.h"
#include "server/http_server.h"
#include "sceAppInstUtil.h"
#include "dbglogger.h"

using namespace std;

#define BUF_SIZE 4096
#define PORT 9041

typedef struct notify_request
{
    char useless1[45];
    char message[3075];
} notify_request_t;

extern "C"
{
    int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
}

void notify(const char *fmt, ...)
{
    notify_request_t req;
    va_list args;

    bzero(&req, sizeof req);
    va_start(args, fmt);
    vsnprintf(req.message, sizeof req.message, fmt, args);
    va_end(args);

    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

int main(int argc, char *argv[])
{
    int ret;
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUF_SIZE] = {0};
    char *pos;
    PlayGoInfo playgo_info;
    SceAppInstallPkgInfo pkg_info;
    MetaInfo metainfo;

    dbglogger_init();
	dbglogger_log("If you see this you've set up dbglogger correctly.");

	HttpServer::Start();

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        notify("ezRemote DPI create socket failed");
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Binding the socket to the specified address and port
    ret = bind(server_fd, (const sockaddr *)&address, (socklen_t)sizeof(address));
    if (ret < 0)
    {
        notify("ezRemote DPI Port %d already in use", PORT);
        return 0;
    }

    // Listening for incoming connections
    ret = listen(server_fd, 3);
    if (ret < 0)
    {
        notify("ezRemote DPI listen failed");
        return 0;
    }

    notify("ezRemote DPI listening on port %d", PORT);

    // Accepting incoming connections and handling requests
    while (true)
    {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0)
        {
            continue;
        }

        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(new_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const void *>(&tv), sizeof(tv));
        int yes = 1;
		setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (char *) &yes, sizeof(int));

        // Reading the request from the client
        memset(buffer, 0, sizeof(buffer));
        ret = -1;
        int valread = recv(new_socket, buffer, BUF_SIZE, 0);            
        if (valread > 0)
        {
            pos = strchr(buffer, '\r');
            if (pos != nullptr)
            {
                *pos = 0;
            }
            pos = strchr(buffer, '\n');
            if (pos != nullptr)
            {
                *pos = 0;
            }

            if (strncmp(buffer, "stop", 4) == 0)
            {
                close(new_socket);
                break;
            }
            notify("ezRemote DPI Received\n%s", buffer);

            memset(&playgo_info, 0, sizeof(playgo_info));

            for (size_t i = 0; i < SCE_NUM_LANGUAGES; i++)
            {
                strncpy(playgo_info.languages[i], "", sizeof(language_t) - 1);
            }
        
            for (size_t i = 0; i < SCE_NUM_IDS; i++)
            {
                strncpy(playgo_info.playgo_scenario_ids[i], "", sizeof(playgo_scenario_id_t) - 1);
                strncpy(*playgo_info.content_ids, "", sizeof(content_id_t) - 1);
            }
        
            metainfo.uri = buffer;
            metainfo.ex_uri = "";
            metainfo.playgo_scenario_id = "";
            metainfo.content_id = "";
            metainfo.content_name = buffer;
            metainfo.icon_url = "";
        
            ret = sceAppInstUtilInstallByPackage(&metainfo, &pkg_info, &playgo_info);
            if (ret != 0)
            {
                notify("Package install failed with\nError Code: 0x%08X\n", ret);
            }        
        }

        // Sending the response to the client
        sprintf(buffer, "%d", ret);
        send(new_socket, buffer, strlen(buffer), 0);

        // Closing the connection
        close(new_socket);
    }

    HttpServer::Stop();
    
    // Closing the listening socket (this part will not be reached in the current loop implementation)
    notify("Closing ezRemote DPI");
    close(server_fd);

    return 0;
}
