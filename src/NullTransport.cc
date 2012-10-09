/**
 * @file
 * NullTransport implementation
 */

/******************************************************************************
 * Copyright 2012, Qualcomm Innovation Center, Inc.
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

#include <list>

#include <errno.h>
#include <qcc/Socket.h>
#include <qcc/SocketStream.h>
#include <qcc/String.h>
#include <qcc/StringUtil.h>
#include <qcc/Util.h>
#include <qcc/Debug.h>

#include <alljoyn/BusAttachment.h>

#include "BusInternal.h"
#include "RemoteEndpoint.h"
#include "NullTransport.h"
#include "AllJoynPeerObj.h"

#define QCC_MODULE "NULL_TRANSPORT"

using namespace std;
using namespace qcc;

namespace ajn {

const char* NullTransport::TransportName = "null";

DaemonLauncher* NullTransport::daemonLauncher;

/*
 * The null endpoint simply moves messages between the daemon router to the client router and lets
 * the routers handle it from there. The only wrinkle is that messages forwarded to the daemon may
 * need to be encrypted because in the non-bundled case encryption is done in _Message::Deliver()
 * and that method does not get called in this case.
 */
class NullEndpoint : public BusEndpoint {

    friend class NullTransport;

  public:

    NullEndpoint(BusAttachment& clientBus, BusAttachment& daemonBus);

    ~NullEndpoint();

    QStatus PushMessage(Message& msg)
    {
        if (closing) {
            return ER_BUS_ENDPOINT_CLOSING;
        }

        IncrementPushCount();

        QStatus status = ER_OK;
        /*
         * In the un-bundled daemon case messages store the name of the endpoint they were received
         * on. As far as the client and daemon routers are concerned the message was received from
         * this endpoint so we must set the received name to the unique name of this endpoint.
         */
        msg->rcvEndpointName = uniqueName;
        /*
         * If the message came from the client forward it to the daemon and visa versa. Note that
         * if the message didn't come from the client it must be assumed that it came from the
         * daemon to handle to the (rare) case of a broadcast signal being sent to multiple bus
         * attachments in a single application.
         */
        if (msg->bus == &clientBus) {
            /*
             * In the non-bundled case messages are encrypted when they are delivered to the daemon
             * endpoint by a call to Message::Deliver. The null transport bypasses Message::Deliver
             * by pushing the messages directly to the daemon router. This means we need to encrypt
             * messages here before we do the push.
             */
            if (msg->encrypt) {
                status = msg->EncryptMessage();
                /* Report authorization failure as a security violation */
                if (status == ER_BUS_NOT_AUTHORIZED) {
                    clientBus.GetInternal().GetLocalEndpoint().GetPeerObj()->HandleSecurityViolation(msg, status);
                }
            }
            if (status == ER_OK) {
                msg->bus = &daemonBus;
                status = daemonBus.GetInternal().GetRouter().PushMessage(msg, *this);
            } else if (status == ER_BUS_AUTHENTICATION_PENDING) {
                status = ER_OK;
            }
        } else {
            assert(msg->bus == &daemonBus);
            /*
             * Register the endpoint with the client on receiving the first message from the daemon.
             */
            if (IncrementAndFetch(&clientReady) == 1) {
                QCC_DbgHLPrintf(("Registering null endpoint with client"));
                clientBus.GetInternal().GetRouter().RegisterEndpoint(*this, false);
            } else {
                DecrementAndFetch(&clientReady);
            }
            /*
             * We need to clone broadcast signals because each receiving bus attachment must be
             * able to unmarshal the arg list including decrypting and doing header expansion.
             */
            if (msg->IsBroadcastSignal()) {
                Message clone(msg, true /*deep copy*/);
                clone->bus = &clientBus;
                status = clientBus.GetInternal().GetRouter().PushMessage(clone, *this);
            } else {
                msg->bus = &clientBus;
                status = clientBus.GetInternal().GetRouter().PushMessage(msg, *this);
            }
        }
        DecrementPushCount();
        return status;
    }

    const qcc::String& GetUniqueName() const { return uniqueName; }

    uint32_t GetUserId() const { return qcc::GetUid(); }
    uint32_t GetGroupId() const { return qcc::GetGid(); }
    uint32_t GetProcessId() const { return qcc::GetPid(); }
    bool SupportsUnixIDs() const {
#if defined(QCC_OS_GROUP_WINDOWS) || defined(QCC_OS_GROUP_WINRT)
        return false;
#else
        return true;
#endif
    }

    bool AllowRemoteMessages() { return true; }

    int32_t clientReady;
    bool closing;
    BusAttachment& clientBus;
    BusAttachment& daemonBus;

    qcc::String uniqueName;

};

NullEndpoint::NullEndpoint(BusAttachment& clientBus, BusAttachment& daemonBus) :
    BusEndpoint(ENDPOINT_TYPE_NULL),
    clientReady(0),
    closing(false),
    clientBus(clientBus),
    daemonBus(daemonBus)
{
    /*
     * We short-circuit all of the normal authentication and hello handshakes and
     * simply get a unique name for the null endpoint directly from the daemon.
     */
    uniqueName = daemonBus.GetInternal().GetRouter().GenerateUniqueName();
    QCC_DbgHLPrintf(("Creating null endpoint %s", uniqueName.c_str()));
}

NullEndpoint::~NullEndpoint()
{
    QCC_DbgHLPrintf(("Destroying null endpoint %s", uniqueName.c_str()));

    /*
     * Don't finalize the destructor while there are threads pushing to this endpoint.
     */
    WaitForZeroPushCount();
}

NullTransport::NullTransport(BusAttachment& bus) : bus(bus), running(false), endpoint(NULL), daemonBus(NULL)
{
}

NullTransport::~NullTransport()
{
    Stop();
    Join();
}

QStatus NullTransport::Start()
{
    running = true;
    return ER_OK;
}

QStatus NullTransport::Stop(void)
{
    running = false;
    Disconnect("null:");
    return ER_OK;
}

QStatus NullTransport::Join(void)
{
    if (daemonLauncher) {
        daemonLauncher->Join();
    }
    return ER_OK;
}

QStatus NullTransport::NormalizeTransportSpec(const char* inSpec, qcc::String& outSpec, std::map<qcc::String, qcc::String>& argMap) const
{
    outSpec = inSpec;
    return ER_OK;
}

QStatus NullTransport::LinkBus(BusAttachment* otherBus)
{
    QCC_DbgHLPrintf(("Linking client and daemon busses"));

    assert(otherBus);

    endpoint = new NullEndpoint(bus, *otherBus);
    /*
     * The compression rules are shared between the client bus and the daemon bus
     */
    bus.GetInternal().OverrideCompressionRules(otherBus->GetInternal().GetCompressionRules());
    /*
     * Register the null endpoint with the daemon router. The client is registered as soon as we
     * receive a message from the daemon. This will happen as soon as the daemon has completed
     * the registration.
     */
    QCC_DbgHLPrintf(("Registering null endpoint with daemon"));
    QStatus status = otherBus->GetInternal().GetRouter().RegisterEndpoint(*endpoint, false);
    if (status != ER_OK) {
        delete endpoint;
        endpoint = NULL;
    }
    return status;
}

QStatus NullTransport::Connect(const char* connectSpec, const SessionOpts& opts, BusEndpoint** newep)
{
    QStatus status = ER_OK;

    if (!running) {
        return ER_BUS_TRANSPORT_NOT_STARTED;
    }
    if (!daemonLauncher) {
        return ER_BUS_TRANSPORT_NOT_AVAILABLE;
    }
    assert(!endpoint);

    status = daemonLauncher->Start(this);
    if (status == ER_OK) {
        assert(endpoint);
        if (newep) {
            *newep = endpoint;
        }
    }
    return status;
}

QStatus NullTransport::Disconnect(const char* connectSpec)
{
    NullEndpoint* ep = reinterpret_cast<NullEndpoint*>(endpoint);
    if (ep) {
        assert(daemonLauncher);
        endpoint = NULL;
        ep->closing = true;
        ep->clientBus.GetInternal().GetRouter().UnregisterEndpoint(*ep);
        ep->daemonBus.GetInternal().GetRouter().UnregisterEndpoint(*ep);
        daemonLauncher->Stop(this);
        daemonLauncher->Join();
        delete ep;
    }
    return ER_OK;
}

void NullTransport::RegisterDaemonLauncher(DaemonLauncher* launcher)
{
    daemonLauncher = launcher;
}

} // namespace ajn
