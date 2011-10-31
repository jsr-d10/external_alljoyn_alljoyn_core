/**
 * @file
 * @brief  Sample implementation of an AllJoyn client. That has an implmentation
 * a secure client that is using a shared keystore file.
 */

/******************************************************************************
 *
 *
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

/*constants*/
static const char* INTERFACE_NAME = "org.alljoyn.bus.samples.secure.SecureInterface";
static const char* SERVICE_NAME = "org.alljoyn.bus.samples.secure";
static const char* SERVICE_PATH = "/SecureService";
static const SessionPort SERVICE_PORT = 42;

static bool s_joinComplete = false;
static SessionId s_sessionId = 0;

static volatile sig_atomic_t g_interrupt = false;

static void SigIntHandler(int sig)
{
    g_interrupt = true;
}

/*
 * get a line of input from the the file pointer (most likely stdin).
 * This will capture the the num-1 characters or till a newline character is
 * entered.
 *
 * @param[out] str a pointer to a character array that will hold the user input
 * @param[in]  num the size of the character array 'str'
 * @param[in]  fp  the file pointer the sting will be read from. (most likely stdin)
 *
 * @return returns the same string as 'str' if there has been a read error a null
 *                 pointer will be returned and 'str' will remain unchanged.
 */
char*get_line(char*str, size_t num, FILE*fp)
{
    char*p = fgets(str, num, fp);

    // fgets will capture the '\n' character if the string entered is shorter than
    // num. Remove the '\n' from the end of the line and replace it with nul '\0'.
    if (p != NULL) {
        size_t last = strlen(str) - 1;
        if (str[last] == '\n') {
            str[last] = '\0';
        }
    }
    return p;
}

/** AllJoynListener receives discovery events from AllJoyn */
class MyBusListener : public BusListener, public SessionListener {
  public:
    void FoundAdvertisedName(const char* name, TransportMask transport, const char* namePrefix)
    {
        printf("FoundAdvertisedName(name=%s, prefix=%s)\n", name, namePrefix);
        if (0 == strcmp(name, SERVICE_NAME)) {
            /* We found a remote bus that is advertising basic sercice's  well-known name so connect to it */
            SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);
            QStatus status = g_msgBus->JoinSession(name, SERVICE_PORT, this, s_sessionId, opts);
            if (ER_OK != status) {
                printf("JoinSession failed (status=%s)\n", QCC_StatusText(status));
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

/*
 * This is the local implementation of the an AuthListener.  SrpKeyXListener is
 * designed to only handle SRP Key Exchange Authentication requests.
 *
 * When a Password request (CRED_PASSWORD) comes in using ALLJOYN_SRP_KEYX the
 * code will ask the user to enter the pin code that was generated by the
 * service. The pin code must match the service's pin code for Authentication to
 * be successful.
 *
 * If any other authMechanism is used other than SRP Key Exchange authentication
 * will fail.
 */
class SrpKeyXListener : public AuthListener {
    bool RequestCredentials(const char* authMechanism, const char* authPeer, uint16_t authCount, const char* userId, uint16_t credMask, Credentials& creds) {
        printf("RequestCredentials for authenticating %s using mechanism %s\n", authPeer, authMechanism);
        if (strcmp(authMechanism, "ALLJOYN_SRP_KEYX") == 0) {
            if (credMask & AuthListener::CRED_PASSWORD) {
                if (authCount <= 3) {
                    /* Take input from stdin and send it as a chat messages */
                    printf("Please enter one time password : ");
                    const int bufSize = 7;
                    char buf[bufSize];
                    get_line(buf, bufSize, stdin);
                    creds.SetPassword(buf);
                    return true;
                } else {
                    return false;
                }
            }
        }
        return false;
    }

    void AuthenticationComplete(const char* authMechanism, const char* authPeer, bool success) {
        printf("Authentication %s %s\n", authMechanism, success ? "succesful" : "failed");
    }
};

/** Static bus listener */
static MyBusListener g_busListener;


/** Main entry point */
int main(int argc, char** argv, char** envArg)
{
    QStatus status = ER_OK;

    printf("AllJoyn Library version: %s\n", ajn::GetVersion());
    printf("AllJoyn Library build info: %s\n", ajn::GetBuildInfo());

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
    g_msgBus = new BusAttachment("SRPSecurityClientA", true);

    /* Add org.alljoyn.bus.samples.secure.SecureInterface interface */
    InterfaceDescription* testIntf = NULL;
    status = g_msgBus->CreateInterface(INTERFACE_NAME, testIntf, true);
    if (status == ER_OK) {
        testIntf->AddMethod("Ping", "s",  "s", "inStr,outStr", 0);
        testIntf->Activate();
    } else {
        printf("Failed to create interface %s\n", INTERFACE_NAME);
    }

    /* Start the msg bus */
    if (ER_OK == status) {
        status = g_msgBus->Start();
        if (ER_OK != status) {
            printf("BusAttachment::Start failed\n");
        } else {
            printf("BusAttachment started.\n");
        }
    }

    /*
     * enable security
     * note the location of the keystore file has been specified and the
     * isShared parameter is being set to true. So this keystore file can
     * be used by multiple applications
     */
    if (ER_OK == status) {
        status = g_msgBus->EnablePeerSecurity("ALLJOYN_SRP_KEYX", new SrpKeyXListener(), "/.alljoyn_keystore/central.ks", true);
        if (ER_OK != status) {
            printf("BusAttachment::EnablePeerSecurity failed (%s)\n", QCC_StatusText(status));
        } else {
            printf("BusAttachment::EnablePeerSecurity succesful\n");
        }
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
        status = g_msgBus->FindAdvertisedName(SERVICE_NAME);
        if (status != ER_OK) {
            printf("org.alljoyn.Bus.FindAdvertisedName failed (%s))\n", QCC_StatusText(status));
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

    if (status == ER_OK) {
        ProxyBusObject remoteObj(*g_msgBus, SERVICE_NAME, SERVICE_PATH, s_sessionId);
        const InterfaceDescription* alljoynTestIntf = g_msgBus->GetInterface(INTERFACE_NAME);
        assert(alljoynTestIntf);
        remoteObj.AddInterface(*alljoynTestIntf);

        Message reply(*g_msgBus);
        MsgArg inputs[1];
        inputs[0].Set("s", "ClientA says Hello AllJoyn!");
        status = remoteObj.MethodCall(INTERFACE_NAME, "Ping", inputs, 1, reply, 5000);
        if (ER_OK == status) {
            printf("%s.%s ( path=%s) returned \"%s\"\n", INTERFACE_NAME, "Ping",
                   SERVICE_PATH, reply->GetArg(0)->v_string.str);
        } else {
            printf("MethodCall on %s.%s failed\n", INTERFACE_NAME, "Ping");
        }
    }

    /* Deallocate bus */
    if (g_msgBus) {
        BusAttachment* deleteMe = g_msgBus;
        g_msgBus = NULL;
        delete deleteMe;
    }

    printf("exiting with status %d (%s)\n", status, QCC_StatusText(status));

    return (int) status;
}
