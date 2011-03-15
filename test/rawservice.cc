/**
 * @file
 * Sample implementation of an AllJoyn service that provides a raw socket.
 */

/******************************************************************************
 * Copyright 2009-2011, Qualcomm Innovation Center, Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 ******************************************************************************/
#include <qcc/platform.h>

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <vector>
#include <errno.h>

#include <qcc/Debug.h>
#include <qcc/Environ.h>
#include <qcc/Mutex.h>
#include <qcc/String.h>
#include <qcc/Thread.h>
#include <qcc/time.h>
#include <qcc/Util.h>

#include <alljoyn/BusAttachment.h>
#include <alljoyn/DBusStd.h>
#include <alljoyn/AllJoynStd.h>
#include <alljoyn/MsgArg.h>
#include <alljoyn/version.h>

#include <Status.h>


#define QCC_MODULE "ALLJOYN"

using namespace std;
using namespace qcc;
using namespace ajn;

namespace org {
namespace alljoyn {
namespace raw_test {
const char* InterfaceName = "org.alljoyn.raw_test";
const char* DefaultWellKnownName = "org.alljoyn.raw_test";
const char* ObjectPath = "/org/alljoyn/raw_test";
}
}
}

/** Static top level message bus object */
static BusAttachment* g_msgBus = NULL;
static String g_wellKnownName = ::org::alljoyn::raw_test::DefaultWellKnownName;

/** Signal handler */
static void SigIntHandler(int sig)
{
    if (NULL != g_msgBus) {
        QStatus status = g_msgBus->Stop(false);
        if (ER_OK != status) {
            QCC_LogError(status, ("BusAttachment::Stop() failed"));
        }
    }
}

class MyBusListener : public BusListener {

  public:
    MyBusListener() : BusListener(), sessionId(0) { }

    bool AcceptSession(const char* sessionName, SessionId id, const char* joiner, const QosInfo& qos)
    {
        QCC_SyncPrintf("Accepting JoinSession request from %s (sessionId=%u) \n", joiner, id);
        sessionId = id;

        /* Allow the join attempt */
        return true;
    }

    SocketFd GetSessionId() { return sessionId; }

  private:
    SessionId sessionId;
};

static void usage(void)
{
    printf("Usage: rawservice [-h] [-n <name>]\n\n");
    printf("Options:\n");
    printf("   -h         = Print this help message\n");
    printf("   -n <name>  = Well-known name to advertise\n");
}

/** Main entry point */
int main(int argc, char** argv)
{
    QStatus status = ER_OK;

    printf("AllJoyn Library version: %s\n", ajn::GetVersion());
    printf("AllJoyn Library build info: %s\n", ajn::GetBuildInfo());

    /* Install SIGINT handler */
    signal(SIGINT, SigIntHandler);

    /* Parse command line args */
    for (int i = 1; i < argc; ++i) {
        if (0 == strcmp("-h", argv[i])) {
            usage();
            exit(0);
        } else if (0 == strcmp("-n", argv[i])) {
            ++i;
            if (i == argc) {
                printf("option %s requires a parameter\n", argv[i - 1]);
                usage();
                exit(1);
            } else {
                g_wellKnownName = argv[i];
            }
        } else {
            status = ER_FAIL;
            printf("Unknown option %s\n", argv[i]);
            usage();
            exit(1);
        }
    }

    /* Get env vars */
    Environ* env = Environ::GetAppEnviron();
    qcc::String clientArgs = env->Find("DBUS_STARTER_ADDRESS");

    if (clientArgs.empty()) {
#ifdef _WIN32
        clientArgs = env->Find("BUS_ADDRESS", "tcp:addr=127.0.0.1,port=9955");
#else
        clientArgs = env->Find("BUS_ADDRESS", "unix:abstract=alljoyn");
#endif
    }

    /* Create message bus */
    g_msgBus = new BusAttachment("rawservice", true);
    MyBusListener myBusListener;

    /* Start the msg bus */
    status = g_msgBus->Start();
    if (status != ER_OK) {
        QCC_LogError(status, ("BusAttachment::Start failed"));
    }

    /* Create a bus listener and connect to the local daemon */
    if (status == ER_OK) {
        g_msgBus->RegisterBusListener(myBusListener);

        /* Connect to the daemon */
        status = g_msgBus->Connect(clientArgs.c_str());
        if (status != ER_OK) {
            QCC_LogError(status, ("Failed to connect to \"%s\"", clientArgs.c_str()));
        }
    }

    /* Request a well-known name */
    if (status == ER_OK) {
        uint32_t disposition = 0;
        QStatus status = g_msgBus->RequestName(g_wellKnownName.c_str(), DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_DO_NOT_QUEUE, disposition);
        if ((status != ER_OK) || (disposition != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)) {
            status = (status == ER_OK) ? ER_FAIL : status;
            QCC_LogError(status, ("Failed to request name %s (disposition=%d)", g_wellKnownName.c_str(), disposition));
        }
    }

    /* Create a session */
    QosInfo qos(QosInfo::TRAFFIC_RAW_RELIABLE, QosInfo::PROXIMITY_ANY, QosInfo::TRANSPORT_ANY);
    uint32_t replyCode = 0;
    SessionId sessionId;
    status = g_msgBus->CreateSession(g_wellKnownName.c_str(), false, qos, replyCode, sessionId);
    if ((status != ER_OK) || (replyCode != ALLJOYN_CREATESESSION_REPLY_SUCCESS)) {
        status = (status == ER_OK) ? ER_BUS_ERROR_RESPONSE : status;
        QCC_LogError(status, ("CreateSession(%s,<>) failed (%d)", g_wellKnownName.c_str(), replyCode));
    }

    /* Begin Advertising the well-known name */
    if (status == ER_OK) {
        uint32_t disposition = 0;
        QosInfo qos(QosInfo::TRAFFIC_RAW_RELIABLE, QosInfo::PROXIMITY_ANY, QosInfo::TRANSPORT_ANY);
        status = g_msgBus->AdvertiseName(g_wellKnownName.c_str(), qos, disposition);
        if ((status != ER_OK) || (disposition != ALLJOYN_ADVERTISENAME_REPLY_SUCCESS)) {
            status = (status == ER_OK) ? ER_FAIL : status;
            QCC_LogError(status, ("AdvertiseName failed (disposition=%d)", disposition));
        }
    }

    SessionId lastSessionId = 0;
    while ((status == ER_OK) && (!g_msgBus->IsStopping())) {
        /* Wait for someone to join our session */
        SessionId id;
        if ((id = myBusListener.GetSessionId()) == lastSessionId) {
            qcc::Sleep(100);
            continue;
        }
        printf("Found a new joiner with session id = %u\n", id);
        lastSessionId = id;

        /* Get the socket */
        SocketFd sockFd;
        status = g_msgBus->GetSessionFd(id, sockFd);
        if (status != ER_OK) {
            QCC_LogError(status, ("Failed to get socket from GetSessionFd args"));
        }

        /* Write test message on socket */
        if (status == ER_OK) {
            const char* testMessage = "abcdefghijklmnopqrstuvwxyz";
            size_t testMessageLen = ::strlen(testMessage);
            size_t sent;
            status = qcc::Send(sockFd, testMessage, testMessageLen, sent);
            if (status == ER_OK) {
                printf("Wrote %lu of %lu bytes of testMessage to socket\n", sent, testMessageLen);
            } else {
                printf("Failed to write testMessage (%s)\n", ::strerror(errno));
                status = ER_FAIL;
            }
#ifdef WIN32
            closesocket(sockFd);
#else
            ::shutdown(sockFd, SHUT_RDWR);
            ::close(sockFd);
#endif
        }
    }

    /* Wait for bus to stop */
    if (status == ER_OK) {
        /* Wait until bus is stopped */
        g_msgBus->WaitStop();
    }

    /* Delete the bus */
    delete g_msgBus;

    printf("%s exiting with status %d (%s)\n", argv[0], status, QCC_StatusText(status));

    return (int) status;
}
