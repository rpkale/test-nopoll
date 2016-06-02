
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <nopoll.h>

static int debug_nopoll = 0;
static int debug_local = 0;
static char serverResponseBuffer[33000];
static char * instanceID = NULL;
static FILE * nopollLogFile = NULL;

int LogPrintf(const char * a_format, ...)
{
    char buffer[1024];
    int len = sprintf(buffer, "%s: ", instanceID);
    va_list va;
    va_start(va, a_format);
    len += vsnprintf(buffer + len, 1000, a_format, va);
    va_end(va);
    fprintf(stderr, "%s\n", buffer);
    return len;
}

static void noPollLogger(noPollCtx *ctx, noPollDebugLevel level, const char *log_msg, noPollPtr user_data) {
    if (nopollLogFile == NULL) {
        char filename[256];
        sprintf(filename, "nopoll_%s.log", instanceID);
        nopollLogFile = fopen(filename, "w");
        if (nopollLogFile == NULL)
            return;
    }
    if (level >= 0) { // adjust level as needed
        fprintf(nopollLogFile, "%d: %s\n", level, log_msg);
    }
}

#define DebugPrintf(...)   do { if (debug_local) LogPrintf(__VA_ARGS__); } while(0)

uint64_t GetTime()
{
    struct timeval ts;
    gettimeofday(&ts, NULL);
    uint64_t tTimeStamp = (((uint64_t) ts.tv_sec) * 1000 + (ts.tv_usec/1000));
    return tTimeStamp;
}

/********** Server Side Code **********/
void sendResponse(noPollConn * conn, int requestLen) {
    int rc = nopoll_conn_send_binary(conn, serverResponseBuffer, requestLen);
    if (rc != requestLen) {
        LogPrintf("sendResponse failed %d", errno);
        // TODO: Handle EAGAIN to retry. So far this condition has not happened yet.
    }
}

void handleRequest(noPollConn * conn, const char * message) {
    int requestLen = 0;
    int requestCount = 0;
    int rc = sscanf(message, "Get %d %d", &requestLen, &requestCount);
    if ((rc == 2) && (requestLen > 0)) {
        DebugPrintf("Got a request for %d messages of %d bytes each", requestCount, requestLen);
        for (int i = 0; i < requestCount; i++) {
            sendResponse(conn, requestLen);
        }
    }
    else {
        LogPrintf("Invalid request from client: Expecting Get request.");
        exit(1);
    }
}

void listener_on_message(noPollCtx * ctx, noPollConn * conn, noPollMsg * msg, noPollPtr  user_data)
{
    noPollOpCode code = nopoll_msg_opcode(msg);
    if (code == NOPOLL_TEXT_FRAME) {
        int msglen = nopoll_msg_get_payload_size(msg);
        char message[32];
        if (msglen > 0 && msglen < 32) {
            memcpy(message, nopoll_msg_get_payload(msg), msglen);
            message[msglen] = 0;
            handleRequest(conn, message);
        }
        else {
            LogPrintf("Invalid request from client: Invalid message length.");
            exit(1);
        }
    }
    else {
        LogPrintf("Invalid request from client: Expecting text message.");
        exit(1);
    }
}


int create_listener(const char * port)
{
    memset(serverResponseBuffer, 0xab, 33000);
    instanceID = strdup("server");

    noPollCtx * ctx = nopoll_ctx_new ();
    if (!ctx) {
        LogPrintf("Could not create context");
        return 1;
    }
    if (debug_nopoll) {
       nopoll_log_set_handler(ctx, noPollLogger, NULL);
       //nopoll_log_enable(ctx, nopoll_true);
    }

    noPollConn * listener = nopoll_listener_new(ctx, "0.0.0.0", port);
    if (!nopoll_conn_is_ok (listener)) {
        LogPrintf("Could not listen on port %s", port);
        return 1;
    }
    LogPrintf("noPoll listener started at: %s:%s",
            nopoll_conn_host(listener), nopoll_conn_port(listener));

    nopoll_ctx_set_on_msg(ctx, listener_on_message, NULL);

    nopoll_loop_wait(ctx, 0);

    nopoll_ctx_unref(ctx);
    return 0;
}

/********** Client Side Code **********/
int getResponse(noPollConn * conn, int requestLen, int index) {
    noPollMsg * msg;
    int count = 0;
    uint64_t beginTime = GetTime();
    int msglen = 0;
    while (true) {
        count++;
        msg = nopoll_conn_get_msg(conn);
        if (msg != NULL) {
            noPollOpCode code = nopoll_msg_opcode(msg);
            if (!((code == NOPOLL_BINARY_FRAME) || (code == NOPOLL_CONTINUATION_FRAME))) {
                LogPrintf("Received unexpected message type %d", code);
                return -1;
            }
            msglen += nopoll_msg_get_payload_size(msg);
            DebugPrintf("Retrieved message %d frag %d:%d of size %d (collected %d of %d)",
                index, nopoll_msg_is_fragment(msg), nopoll_msg_is_final(msg),
                    nopoll_msg_get_payload_size(msg), msglen, requestLen);
            if (nopoll_msg_is_final(msg) && (msglen != requestLen)) {
                DebugPrintf("Final flag set arbitrarily??");
            }
            if (msglen == requestLen) {
                if (!nopoll_msg_is_final(msg))
                    DebugPrintf("Final flag not set??");
                break;
            }
            nopoll_msg_unref(msg);
        }
        if (!nopoll_conn_is_ok(conn)) {
            LogPrintf("Client disconnected!!");
            return -1;
        }
        nopoll_sleep(100);
    }

    DebugPrintf("Retrieved full message %d in %d ms looped: %d",
            index, (int) (GetTime() - beginTime), count);
    if (msglen != requestLen) {
        LogPrintf("Received invalid response length %d != %d", msglen, requestLen);
        return -1;
    }
    nopoll_msg_unref(msg);
    return 0;
}

int create_client(const char * host, const char * port, int duration)
{
    int rc = 0;

    // Create noPoll context
    noPollCtx * ctx = nopoll_ctx_new ();
    if (!ctx) {
        LogPrintf("Could not create context");
        return 1;
    }
    if (debug_nopoll) {
       nopoll_log_set_handler(ctx, noPollLogger, NULL);
       //nopoll_log_enable(ctx, nopoll_true);
    }

    // Create connection
    uint64_t begin_time = GetTime();
    noPollConn * conn = nopoll_conn_new (ctx, host, port, NULL, NULL, NULL, NULL);
    if (!nopoll_conn_is_ok(conn)) {
        LogPrintf("Failed setting up connection to %s:%s", host, port);
        return 1;
    }

    // Wait till connected
    if (!nopoll_conn_wait_until_connection_ready (conn, 120)) {
        LogPrintf("Could not get connected to %s:%s within 120 seconds. Failed in %d ms",
                host, port, (int)(GetTime() - begin_time));
        return 1;
    }
    LogPrintf("Connected in %d ms.", (int)(GetTime() - begin_time));

    // Don't make any requests. Just sleep for given duration and exit
    // sleep(duration);
    // LogPrintf("Exiting...");
    // return 0;

    // Start the client's request/response loop
    char message[32];
    int messageCount = 0;
    begin_time = GetTime();
    while (true) {
        // Send request
        int requestLen = (rand() % 32000) + 1;
        int requestCount = (rand() % 64) + 1;
        DebugPrintf("Requesting %d messages of %d bytes each [%d]", requestCount, requestLen, messageCount);
        int len = sprintf(message, "Get %d %d", requestLen, requestCount);
        if (nopoll_conn_send_text(conn, message, len) != len) {
            rc = 1;
            LogPrintf("Could not send message: %d", errno);
            break;
        }

        // Collect responses
        for (int index = 0; index < requestCount; index++) {
            if (getResponse(conn, requestLen, index) < 0) {
                rc = 1;
                break;
            }
            messageCount++;
        }
        if (rc == 1)
            break;

        if ((int)(GetTime() - begin_time) > duration * 1000)
            break;
    }

    LogPrintf("Exited after %d ms. Received %d messages.",
            (int)(GetTime() - begin_time), messageCount);

    if (rc == 1) {
        LogPrintf("Error: Client Failed.");
    }
    nopoll_ctx_unref(ctx);
    return rc;
}

/********** Main **********/
void usage() {
    printf("Usage: test_nopoll server port\n");
    printf("    or\n");
    printf("Usage: test_nopoll client id ipaddress port duration\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        usage();
    }
    if (getenv("NOPOLL_TEST_DEBUG"))
        debug_local = 1;
    if (getenv("NOPOLL_TEST_DEBUG_NOPOLL"))
        debug_nopoll = 1;

    if (strcmp(argv[1], "server") == 0) {
        return create_listener(argv[2]);
    }
    else if (strcmp(argv[1], "client") == 0) {
        if (argc < 6)
            usage();
        instanceID = strdup(argv[2]);
        const char * host = argv[3];
        const char * port = argv[4];
        int duration = atoi(argv[5]);
        if (duration <= 0) {
            printf("Duration should be greater than 0 seconds.\n");
            usage();
        }
        return create_client(host, port, duration);
    }
    else {
        printf("First argument should be  \"server\" or \"client\"\n");
        exit(1);
    }
}
