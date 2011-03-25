/**
 * @file
 * @brief  Sample implementation of an AllJoyn client.
 *
 * This is a simple client that will run and change the 'name' property of the
 * 'org.alljoyn.Bus.signal_sample' service then exit.
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

#include <signal.h>
#include <stdio.h>
#include <assert.h>
#include <vector>

#include <qcc/String.h>

#include <alljoyn/BusAttachment.h>
#include <alljoyn/version.h>
#include <alljoyn/AllJoynStd.h>
#include <Status.h>

using namespace std;
using namespace qcc;
using namespace ajn;

/** Static top level message bus object */
static BusAttachment* g_msgBus = NULL;

static const char* SERVICE_NAME = "org.alljoyn.Bus.signal_sample";
static const char* SERVICE_PATH = "/";
static const SessionPort SERVICE_PORT = 25;

static bool s_joinComplete = false;
static SessionId s_sessionId = 0;

/** Signal handler */
static void SigIntHandler(int sig)
{
    if (NULL != g_msgBus) {
        QStatus status = g_msgBus->Stop(false);
        if (ER_OK != status) {
            printf("BusAttachment::Stop() failed\n");
        }
    }
    exit(0);
}

/** AlljounListener receives discovery events from AllJoyn */
class MyBusListener : public BusListener {
  public:
    void FoundAdvertisedName(const char* name, TransportMask transport, const char* namePrefix)
    {
        if (0 == strcmp(name, SERVICE_NAME)) {
            printf("FoundAdvertisedName(name=%s, prefix=%s)\n", name, namePrefix);
            /* We found a remote bus that is advertising basic sercice's  well-known name so connect to it */
            uint32_t returnValue;
            SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);
            QStatus status = g_msgBus->JoinSession(name, SERVICE_PORT, returnValue, s_sessionId, opts);
            if ((ER_OK != status) || (ALLJOYN_JOINSESSION_REPLY_SUCCESS != returnValue)) {
                printf("JoinSession failed (status=%s, returnValue=%d)\n", QCC_StatusText(status), returnValue);
            } else {
                printf("JoinSession SUCCESS (Session id=%d)\n", s_sessionId);
            }
        }
        s_joinComplete = true;
    }

    void NameOwnerChanged(const char* busName, const char* previousOwner, const char* newOwner)
    {
        if (newOwner && (0 == strcmp(busName, SERVICE_NAME))) {
            printf("NameOwnerChanged: name=%s, oldOwner=%s, newOwner=%s\n",
                   busName,
                   previousOwner ? previousOwner : "<none>",
                   newOwner ? newOwner : "<none>");
        }
    }
};

/** Static bus listener */
static MyBusListener g_busListener;

/** Main entry point */
int main(int argc, char** argv, char** envArg)
{
    QStatus status = ER_OK;

    printf("AllJoyn Library version: %s\n", ajn::GetVersion());

    /* Install SIGINT handler */
    signal(SIGINT, SigIntHandler);

    const char* connectArgs = getenv("BUS_ADDRESS");
    if (connectArgs == NULL) {
#ifdef _WIN32
        connectArgs = "tcp:addr=127.0.0.1,port=9955";
#else
        connectArgs = "unix:abstract=alljoyn";
#endif
    }

    /* Create message bus */
    g_msgBus = new BusAttachment("myApp", true);

    /* Start the msg bus */
    status = g_msgBus->Start();
    if (ER_OK != status) {
        printf("BusAttachment::Start failed\n");
    } else {
        printf("BusAttachment started.\n");
    }

    /* Connect to the bus */
    if (ER_OK == status) {
        status = g_msgBus->Connect(connectArgs);
        if (ER_OK != status) {
            printf("BusAttachment::Connect(\"%s\") failed\n", connectArgs);
        } else {
            printf("BusAttchement connected to %s\n", connectArgs);
        }
    }

    /* Register a bus listener in order to get discovery indications */
    if (ER_OK == status) {
        g_msgBus->RegisterBusListener(g_busListener);
        printf("BusListener Registered.\n");
    }

    /* Begin discovery on the well-known name of the service to be called */
    if (ER_OK == status) {
        uint32_t returnValue = 0;
        status = g_msgBus->FindAdvertisedName(SERVICE_NAME, returnValue);
        if ((status != ER_OK) || (returnValue != ALLJOYN_FINDADVERTISEDNAME_REPLY_SUCCESS)) {
            printf("org.alljoyn.Bus.FindAdvertisedName failed (%s) (returnValue=%d)\n", QCC_StatusText(status), returnValue);
            status = (status == ER_OK) ? ER_FAIL : status;
        }
    }

    /* Wait for join session to complete */
    while (!s_joinComplete) {
#ifdef _WIN32
        Sleep(10);
#else
        sleep(1);
#endif
    }

    ProxyBusObject remoteObj;
    if (ER_OK == status) {
        remoteObj = ProxyBusObject(*g_msgBus, SERVICE_NAME, SERVICE_PATH, s_sessionId);
        status = remoteObj.IntrospectRemoteObject();
        if (ER_OK != status) {
            printf("Introspection of %s (path=%s) failed\n", SERVICE_NAME, SERVICE_PATH);
            printf("Make sure the service is running before launching the client.\n");
        }
    }

    if (argc > 1) {
        status = remoteObj.SetProperty(SERVICE_NAME, "name", argv[1]);
        if (status != ER_OK) {
            printf("Error calling SetProperty to change the 'name' property.\n");
        }
    } else {
        printf("Error new name not given: nameChange_client [new name]\n");
    }

    /* Stop the bus (not strictly necessary since we are going to delete it anyways) */
    if (g_msgBus) {
        QStatus s = g_msgBus->Stop();
        if (ER_OK != s) {
            printf("BusAttachment::Stop failed");
        }
    }

    /* Deallocate bus */
    if (g_msgBus) {
        BusAttachment* deleteMe = g_msgBus;
        g_msgBus = NULL;
        delete deleteMe;
    }

    printf("name Change client exiting with status %d (%s)\n", status, QCC_StatusText(status));

    return (int) status;
}



