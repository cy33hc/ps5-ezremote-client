/* 
*
*   DBGLOGGER - debug logger library / (c) 2019 El Bucanero  <www.bucanero.com.ar>
*

 By default the logger will send debug messages to UDP multicast address 239.255.0.100:30000. 
 To receive them you can use socat on your PC:

 $ socat udp4-recv:30000,ip-add-membership=239.255.0.100:0.0.0.0 -

 TCP/UDP Usage:
 1. Set the correct IP/port to your computer (default port 18194)
 2. Execute a local tool to listen to the incoming messages (e.g. netcat, socat)
 
 Try any of these commands in your terminal:

 UDP
 $ nc -l -u 18194
 $ socat udp-recv:18194 stdout

 TCP
 $ nc -l -k 18194
 $ socat tcp-listen:18194 stdout

 3. Start the app on your console.

*/

#ifndef LIBDEBUGLOG_H
#define LIBDEBUGLOG_H

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_LOG_INIT  "udp:239.255.0.100:30000"
//  TCP example string    "tcp:192.168.1.123:18194"
//  File example string   "file:/dev_hdd0/temp/app.log"


typedef enum {
    NO_LOGGER,
    UDP_LOGGER,
    TCP_LOGGER,
    FILE_LOGGER,
    TTY_LOGGER
} LOGGER_MODES;

typedef struct {
	char method;
	char *resource;
} dWebRequest_t;

typedef struct {
	char type[8];
	char *data;
	int64_t size;
} dWebResponse_t;

/*
	int webReq_GetHandler(dWebRequest_t* request, dWebResponse_t* response, void* usr_data);
*/
typedef int (*dWebReqHandler_t)(dWebRequest_t*, dWebResponse_t*, void*);


int dbglogger_init(void);
int dbglogger_init_str(const char* ini_str);
int dbglogger_init_mode(const unsigned char log_mode, const char* dest, const unsigned short port);
int dbglogger_init_file(const char* ini_file);

int dbglogger_stop(void);

// function to print with format string similar to printf
void dbglogger_printf(const char* fmt, ...);

// function that prints "[timestamp] log \n" similar to printf
void dbglogger_log(const char* fmt, ...);

// starts a thread that terminates the process if the file exists
int dbglogger_failsafe(const char* fpath);

// screenshot method
int dbglogger_screenshot(const char* filename, const unsigned char alpha);

// screenshot will be placed in /dev_hdd0/tmp/screenshot_YYYY_MM_DD_HH_MM_SS.bmp 
int dbglogger_screenshot_tmp(const unsigned char alpha);

// base64 file encoding method
int dbglogger_b64encode(const char* filename);

// base64 data encoding method
char* dbg_base64_encode(const unsigned char *data, int data_len);

// base64 data decoding method
unsigned char * dbg_base64_decode(const char *src, size_t *out_len);

// web server methods
int dbg_webserver_start(int port, dWebReqHandler_t req, void* usr_data);
void dbg_webserver_stop();

// a simple http server handler that serves the system '/' root folder
int dbg_simpleWebServerHandler(dWebRequest_t* req, dWebResponse_t* res, void* data);

#ifdef __cplusplus
}
#endif

#endif
