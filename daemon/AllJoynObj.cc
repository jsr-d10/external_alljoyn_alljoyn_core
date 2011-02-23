/**
 * @file
 *
 * This file implements the org.alljoyn.Bus interfaces
 */

/******************************************************************************
 * Copyright 2010-2011, Qualcomm Innovation Center, Inc.
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

#include <algorithm>
#include <assert.h>
#include <map>
#include <vector>

#include <qcc/Debug.h>
#include <qcc/ManagedObj.h>
#include <qcc/String.h>
#include <qcc/Thread.h>
#include <qcc/Util.h>
#include <qcc/Crypto.h>

#include <alljoyn/BusAttachment.h>
#include <alljoyn/DBusStd.h>
#include <alljoyn/InterfaceDescription.h>
#include <alljoyn/AllJoynStd.h>
#include <alljoyn/Message.h>
#include <alljoyn/MessageReceiver.h>
#include <alljoyn/ProxyBusObject.h>

#include "DaemonRouter.h"
#include "AllJoynObj.h"
#include "TransportList.h"
#include "BusUtil.h"

#define QCC_MODULE "ALLJOYN_OBJ"

using namespace std;
using namespace qcc;

namespace ajn {


AllJoynObj::AllJoynObj(Bus& bus) :
    BusObject(bus, org::alljoyn::Bus::ObjectPath, false),
    bus(bus),
    router(reinterpret_cast<DaemonRouter&>(bus.GetInternal().GetRouter())),
    foundNameSignal(NULL),
    lostAdvNameSignal(NULL),
    busConnLostSignal(NULL),
    guid(bus.GetInternal().GetGlobalGUID()),
    exchangeNamesSignal(NULL),
    detachSessionSignal(NULL),
    nameMapReaper(this),
    isStopping(false)
{
}

AllJoynObj::~AllJoynObj()
{
    bus.DeregisterBusObject(*this);

    // TODO: Unregister signal handlers.
    // TODO: Unregister name listeners
    // TODO: Unregister transport listener
    // TODO: Unregister local object

    /* Wait for any outstanding JoinSessionThreads */
    joinSessionThreadsLock.Lock();
    isStopping = true;
    vector<JoinSessionThread*>::iterator it = joinSessionThreads.begin();
    while (it != joinSessionThreads.end()) {
        (*it)->Stop();
        ++it;
    }
    while (!joinSessionThreads.empty()) {
        joinSessionThreadsLock.Unlock();
        qcc::Sleep(50);
        joinSessionThreadsLock.Lock();
    }
    joinSessionThreadsLock.Unlock();
}

QStatus AllJoynObj::Init()
{
    QStatus status;

    /* Make this object implement org.alljoyn.Bus */
    const InterfaceDescription* alljoynIntf = bus.GetInterface(org::alljoyn::Bus::InterfaceName);
    if (!alljoynIntf) {
        status = ER_BUS_NO_SUCH_INTERFACE;
        QCC_LogError(status, ("Failed to get %s interface", org::alljoyn::Bus::InterfaceName));
        return status;
    }

    /* Hook up the methods to their handlers */
    const MethodEntry methodEntries[] = {
        { alljoynIntf->GetMember("AdvertiseName"),            static_cast<MessageReceiver::MethodHandler>(&AllJoynObj::AdvertiseName) },
        { alljoynIntf->GetMember("CancelAdvertiseName"),      static_cast<MessageReceiver::MethodHandler>(&AllJoynObj::CancelAdvertiseName) },
        { alljoynIntf->GetMember("FindAdvertisedName"),       static_cast<MessageReceiver::MethodHandler>(&AllJoynObj::FindAdvertisedName) },
        { alljoynIntf->GetMember("CancelFindAdvertisedName"), static_cast<MessageReceiver::MethodHandler>(&AllJoynObj::CancelFindAdvertisedName) },
        { alljoynIntf->GetMember("CreateSession"),            static_cast<MessageReceiver::MethodHandler>(&AllJoynObj::CreateSession) },
        { alljoynIntf->GetMember("JoinSession"),              static_cast<MessageReceiver::MethodHandler>(&AllJoynObj::JoinSession) },
        { alljoynIntf->GetMember("LeaveSession"),             static_cast<MessageReceiver::MethodHandler>(&AllJoynObj::LeaveSession) }
    };

    AddInterface(*alljoynIntf);
    status = AddMethodHandlers(methodEntries, ArraySize(methodEntries));
    if (ER_OK != status) {
        QCC_LogError(status, ("AddMethods for %s failed", org::alljoyn::Bus::InterfaceName));
    }

    foundNameSignal = alljoynIntf->GetMember("FoundAdvertisedName");
    lostAdvNameSignal = alljoynIntf->GetMember("LostAdvertisedName");
    busConnLostSignal = alljoynIntf->GetMember("BusConnectionLost");

    /* Make this object implement org.alljoyn.Daemon */
    daemonIface = bus.GetInterface(org::alljoyn::Daemon::InterfaceName);
    if (!daemonIface) {
        status = ER_BUS_NO_SUCH_INTERFACE;
        QCC_LogError(status, ("Failed to get %s interface", org::alljoyn::Daemon::InterfaceName));
        return status;
    }

    /* Hook up the methods to their handlers */
    const MethodEntry daemonMethodEntries[] = {
        { daemonIface->GetMember("AttachSession"),     static_cast<MessageReceiver::MethodHandler>(&AllJoynObj::AttachSession) }
    };
    AddInterface(*daemonIface);
    status = AddMethodHandlers(daemonMethodEntries, ArraySize(daemonMethodEntries));
    if (ER_OK != status) {
        QCC_LogError(status, ("AddMethods for %s failed", org::alljoyn::Daemon::InterfaceName));
    }

    exchangeNamesSignal = daemonIface->GetMember("ExchangeNames");
    assert(exchangeNamesSignal);
    detachSessionSignal = daemonIface->GetMember("DetachSession");
    assert(detachSessionSignal);

    /* Register a signal handler for ExchangeNames */
    if (ER_OK == status) {
        status = bus.RegisterSignalHandler(this,
                                           static_cast<MessageReceiver::SignalHandler>(&AllJoynObj::ExchangeNamesSignalHandler),
                                           daemonIface->GetMember("ExchangeNames"),
                                           NULL);
    } else {
        QCC_LogError(status, ("Failed to register ExchangeNamesSignalHandler"));
    }

    /* Register a signal handler for NameChanged bus-to-bus signal */
    if (ER_OK == status) {
        status = bus.RegisterSignalHandler(this,
                                           static_cast<MessageReceiver::SignalHandler>(&AllJoynObj::NameChangedSignalHandler),
                                           daemonIface->GetMember("NameChanged"),
                                           NULL);
    } else {
        QCC_LogError(status, ("Failed to register NameChangedSignalHandler"));
    }

    /* Register a signal handler for DetachSession bus-to-bus signal */
    if (ER_OK == status) {
        status = bus.RegisterSignalHandler(this,
                                           static_cast<MessageReceiver::SignalHandler>(&AllJoynObj::DetachSessionSignalHandler),
                                           daemonIface->GetMember("DetachSession"),
                                           NULL);
    } else {
        QCC_LogError(status, ("Failed to register DetachSessionSignalHandler"));
    }

    /* Register a name table listener */
    router.AddBusNameListener(this);

    /* Register as a listener for all the remote transports */
    if (ER_OK == status) {
        TransportList& transList = bus.GetInternal().GetTransportList();
        status = transList.RegisterListener(this);
    }

    /* Start the name reaper */
    if (ER_OK == status) {
        status = nameMapReaper.Start();
    }

    if (ER_OK == status) {
        status = bus.RegisterBusObject(*this);
    }

    return status;
}

void AllJoynObj::ObjectRegistered(void)
{
    QStatus status;

    /* Must call base class */
    BusObject::ObjectRegistered();

    /* Acquire org.alljoyn.Bus name */
    uint32_t disposition = DBUS_REQUEST_NAME_REPLY_EXISTS;
    status = router.AddAlias(org::alljoyn::Bus::WellKnownName,
                             bus.GetInternal().GetLocalEndpoint().GetUniqueName(),
                             DBUS_NAME_FLAG_DO_NOT_QUEUE,
                             disposition,
                             NULL,
                             NULL);
    if ((ER_OK != status) || (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != disposition)) {
        status = (ER_OK == status) ? ER_FAIL : status;
        QCC_LogError(status, ("Failed to register well-known name \"%s\" (disposition=%d)", org::alljoyn::Bus::WellKnownName, disposition));
    }

    /* Acquire org.alljoyn.Daemon name */
    disposition = DBUS_REQUEST_NAME_REPLY_EXISTS;
    status = router.AddAlias(org::alljoyn::Daemon::WellKnownName,
                             bus.GetInternal().GetLocalEndpoint().GetUniqueName(),
                             DBUS_NAME_FLAG_DO_NOT_QUEUE,
                             disposition,
                             NULL,
                             NULL);
    if ((ER_OK != status) || (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != disposition)) {
        status = (ER_OK == status) ? ER_FAIL : status;
        QCC_LogError(status, ("Failed to register well-known name \"%s\" (disposition=%d)", org::alljoyn::Daemon::WellKnownName, disposition));
    }
}

#if 0
void AllJoynObj::Connect(const InterfaceDescription::Member* member, Message& msg)
{
    QStatus status;
    uint32_t replyCode;
    size_t numArgs;
    const MsgArg* args;
    qcc::String normSpec;
    map<qcc::String, qcc::String> argMap;

    /* Normalize the connect string */
    msg->GetArgs(numArgs, args);
    assert((numArgs == 1) && (args[0].typeId == ALLJOYN_STRING));
    qcc::String origSpec = args[0].v_string.str;
    status = NormalizeTransportSpec(origSpec.c_str(), normSpec, argMap);

    if (ER_OK == status) {
        /* Connect */
        status = ProcConnect(msg->GetSender(), normSpec);
        replyCode = (status == ER_OK) ? ALLJOYN_CONNECT_REPLY_SUCCESS : ALLJOYN_CONNECT_REPLY_FAILED;
    } else {
        /* invalid connect spec */
        replyCode = ALLJOYN_CONNECT_REPLY_INVALID_SPEC;
    }

    /* Reply to request */
    MsgArg replyArg("u", replyCode);
    status = MethodReply(msg, &replyArg, 1);
    QCC_DbgPrintf(("AllJoynObj::Connect(%s) returned %d (status=%s)", origSpec.c_str(), replyCode, QCC_StatusText(status)));

    /* Log error if reply could not be sent */
    if (ER_OK != status) {
        QCC_LogError(status, ("Failed to respond to org.alljoyn.Bus.Connect"));
    }
}

QStatus AllJoynObj::ProcConnect(const qcc::String& uniqueName, const qcc::String& normConnectSpec)
{
    QCC_DbgTrace(("AllJoynObj::ProcConnect(uniqueName = \"%s\", normConnectSpec = \"%s\")",
                  uniqueName.c_str(), normConnectSpec.c_str()));

    /* Check to see if this connection already exists */
    connectMapLock.Lock();
    multimap<qcc::String, qcc::String>::const_iterator it = connectMap.find(normConnectSpec);
    bool doConnect = (it == connectMap.end());

    /* Add to connect map */
    connectMap.insert(pair<qcc::String, qcc::String>(normConnectSpec, uniqueName));
    connectMapLock.Unlock();

    QStatus status(ER_OK);
    if (doConnect) {
        /* Attempt to connect to external bus */
        status =  bus.Connect(normConnectSpec.c_str());

        /* If the connect failed, remove entry from connectMap */
        if (ER_OK != status) {
            connectMapLock.Lock();
            multimap<qcc::String, qcc::String>::iterator it = connectMap.find(normConnectSpec);
            while ((it != connectMap.end()) && (it->first == normConnectSpec)) {
                if (it->second == uniqueName) {
                    connectMap.erase(it);
                    break;
                }
                ++it;
            }
            connectMapLock.Unlock();
        }
    } else {
        QCC_DbgPrintf(("Found \"%s\" in connectMap", normConnectSpec.c_str()));
    }

    return status;
}

void AllJoynObj::Disconnect(const InterfaceDescription::Member* member, Message& msg)
{
    QStatus status;
    uint32_t replyCode = ALLJOYN_DISCONNECT_REPLY_FAILED;
    size_t numArgs;
    const MsgArg* args;
    qcc::String normSpec;
    map<qcc::String, qcc::String> argMap;

    /* Normalize the disconnect string */
    msg->GetArgs(numArgs, args);
    assert((numArgs == 1) && (args[0].typeId == ALLJOYN_STRING));
    qcc::String origSpec = args[0].v_string.str;
    status = NormalizeTransportSpec(origSpec.c_str(), normSpec, argMap);

    if (ER_OK == status) {
        /* Disconnect */
        status = ProcDisconnect(msg->GetSender(), normSpec);

        /* Set reply code */
        replyCode = (ER_OK == status) ? ALLJOYN_DISCONNECT_REPLY_SUCCESS : ALLJOYN_DISCONNECT_REPLY_FAILED;
    }
    if (ER_OK != status) {
        QCC_LogError(status, ("AllJoynObj::Disconnect (spec=%s) failed", origSpec.c_str()));
    }

    /* Reply to request */
    MsgArg replyArg("u", replyCode);
    status = MethodReply(msg, &replyArg, 1);
    QCC_DbgPrintf(("AllJoynObj::Disconnect(%s) returned %d (status=%s)", origSpec.c_str(), replyCode, QCC_StatusText(status)));

    /* Log error if reply could not be sent */
    if (ER_OK != status) {
        QCC_LogError(status, ("Failed to respond to org.alljoyn.Bus.Disconnect"));
    }
}

QStatus AllJoynObj::ProcDisconnect(const qcc::String& sender, const qcc::String& normConnectSpec)
{
    QCC_DbgTrace(("AllJoynObj::ProcDisconnect(sender = \"%s\", normConnectSpec = \"%s\")",
                  sender.c_str(), normConnectSpec.c_str()));
    QStatus status;

    /* Check to see if this connection exists */
    bool foundConn = false;
    bool connHasRefs = false;

    connectMapLock.Lock();
    pair<multimap<qcc::String, qcc::String>::iterator,
         multimap<qcc::String, qcc::String>::iterator> range = connectMap.equal_range(normConnectSpec);

    /* Find the first entry in the connectMap and delete it */
    multimap<qcc::String, qcc::String>::iterator it = range.first;
    while (it != range.second) {
        if (!foundConn && it->second == sender) {
            multimap<qcc::String, qcc::String>::iterator lastIt = it++;
            connectMap.erase(lastIt);
            foundConn = true;
        } else {
            connHasRefs = true;
            ++it;
        }
    }
    connectMapLock.Unlock();

    /* Disconnect connection if no other refs exist */
    if (foundConn && !connHasRefs) {
        status =  bus.Disconnect(normConnectSpec.c_str());
    } else if (foundConn) {
        status = ER_OK;
    } else {
        status = ER_FAIL;
    }

    return status;
}

#endif

void AllJoynObj::CreateSession(const InterfaceDescription::Member* member, Message& msg)
{
    uint32_t replyCode = ALLJOYN_CREATESESSION_REPLY_SUCCESS;
    SessionId id = 0;
    size_t numArgs;
    const MsgArg* args;
    MsgArg replyArgs[2];

    msg->GetArgs(numArgs, args);
    assert(numArgs == 2);
    String sessionName = args[0].v_string.str;
    QCC_DbgTrace(("AllJoynObj::CreateSession(%s)", sessionName.c_str()));

    /* Get the sender */
    String sender = msg->GetSender();

    /* Check to make sure session name has been successfully requested from the bus and owned by the sender */
    if (router.FindEndpoint(sessionName) != router.FindEndpoint(sender)) {
        replyCode = ALLJOYN_CREATESESSION_REPLY_NOT_OWNER;
    } else {
        /* Assign a session id and store the session information */
        Crypto_BigNum val;
        SessionMapEntry entry;

        entry.name = sessionName;
        args[1].Get("(qqq)", &entry.qos.proximity, &entry.qos.traffic, &entry.qos.transports);
        val.GenerateRandomValue(8 * sizeof(SessionId));
        val.RenderBinary(reinterpret_cast<uint8_t*>(&id), sizeof(SessionId));
        entry.id = id;
        entry.endpointName = msg->GetSender();
        sessionMapLock.Lock();
        sessionMap[id] = entry;
        sessionMapLock.Unlock();
    }

    /* Reply to request */
    replyArgs[0].Set("u", replyCode);
    replyArgs[1].Set("u", id);
    QStatus status = MethodReply(msg, replyArgs, ArraySize(replyArgs));
    QCC_DbgPrintf(("AllJoynObj::CreateSession(%s) returned (%d,%u) (status=%s)", sessionName.c_str(), replyCode, id, QCC_StatusText(status)));

    /* Log error if reply could not be sent */
    if (ER_OK != status) {
        QCC_LogError(status, ("Failed to respond to org.alljoyn.Bus.Advertise"));
    }
}

ThreadReturn STDCALL AllJoynObj::JoinSessionThread::Run(void* arg)
{
    uint32_t replyCode = ALLJOYN_JOINSESSION_REPLY_SUCCESS;
    SessionId id = 0;
    QosInfo qosOut = { 0, QosInfo::TRAFFIC_ANY, 0 };
    QStatus status = ER_OK;
    size_t numArgs;
    const MsgArg* args;

    /* Parse the message args */
    msg->GetArgs(numArgs, args);
    assert(numArgs == 2);
    String sessionName = args[0].v_string.str;
    QosInfo qosIn;
    args[1].Get("(qqq)", &qosIn.proximity, &qosIn.traffic, &qosIn.transports);
    QCC_DbgTrace(("JoinSession(%s, <0x%x, %d, 0x%x>)", sessionName.c_str(), qosIn.proximity, qosIn.traffic, qosIn.transports));

    /* Step 1: If there is a busAddr from advertsement use it to (possibly) create a physical connection */
    ajObj.router.LockNameTable();
    ajObj.discoverMapLock.Lock();
    ajObj.virtualEndpointsLock.Lock();
    NameMapEntry* nme = NULL;
    multimap<String, NameMapEntry>::iterator nmit = ajObj.nameMap.lower_bound(sessionName);
    while (nmit != ajObj.nameMap.end() && (nmit->first == sessionName)) {
        if (nmit->second.qos.IsCompatible(qosIn)) {
            nme = &nmit->second;
            break;
        }
        ++nmit;
    }

    RemoteEndpoint* b2bEp = NULL;
    BusEndpoint* ep = ajObj.router.FindEndpoint(sessionName);
    VirtualEndpoint* sessionEp = (ep && (ep->GetEndpointType() == BusEndpoint::ENDPOINT_TYPE_VIRTUAL)) ? static_cast<VirtualEndpoint*>(ep) : NULL;

    if ((sessionEp == NULL) && (nme == NULL)) {
        /* No advertisment or existing route to session creator */
        replyCode = ALLJOYN_JOINSESSION_REPLY_NO_SESSION;
    } else if (nme != NULL) {
        /* Ask the transport that provided the advertisement for an endpoint (may be reused) */
        TransportList& transList = ajObj.bus.GetInternal().GetTransportList();
        Transport* trans = transList.GetTransport(nme->busAddr);
        if (trans == NULL) {
            replyCode = ALLJOYN_JOINSESSION_REPLY_UNREACHABLE;
        } else {
            /* Give up locks around connect because it takes so long */
            ajObj.virtualEndpointsLock.Unlock();
            ajObj.discoverMapLock.Unlock();
            ajObj.router.UnlockNameTable();
            status = trans->Connect(nme->busAddr.c_str(), &b2bEp);
            if (status != ER_OK) {
                QCC_LogError(status, ("trans->Connect(%s) failed", nme->busAddr.c_str()));
                replyCode = ALLJOYN_JOINSESSION_REPLY_CONNECT_FAILED;
            }
            ajObj.router.LockNameTable();
            ajObj.discoverMapLock.Lock();
            ajObj.virtualEndpointsLock.Lock();

            /* Re-acquire sessionEp since it may have gone away or moved while we didn't have the lock */
            ep = ajObj.router.FindEndpoint(sessionName);
            sessionEp = (ep && (ep->GetEndpointType() == BusEndpoint::ENDPOINT_TYPE_VIRTUAL)) ? static_cast<VirtualEndpoint*>(ep) : NULL;
        }
    }

    /* If there was a physical connect, wait for the new b2b endpoint to have a virtual ep for our destination */
    uint32_t startTime = GetTimestamp();
    String b2bEpName = b2bEp ? b2bEp->GetUniqueName() : "";

    if (b2bEp) {
        while (replyCode == ALLJOYN_JOINSESSION_REPLY_SUCCESS) {

            /* Does sessionEp route through b2bEp? If so, we're done */
            if (sessionEp && sessionEp->CanUseRoute(*b2bEp)) {
                break;
            }

            /* Otherwise wait */
            uint32_t now = GetTimestamp();
            if (now > (startTime + 10000)) {
                replyCode = ALLJOYN_JOINSESSION_REPLY_FAILED;
                break;
            } else {
                ajObj.virtualEndpointsLock.Unlock();
                ajObj.discoverMapLock.Unlock();
                ajObj.router.UnlockNameTable();
                qcc::Sleep(50);
                ajObj.router.LockNameTable();
                ajObj.discoverMapLock.Lock();
                ajObj.virtualEndpointsLock.Lock();

                /* Must re-find b2bEp since we gave up the lock */
                b2bEp = static_cast<RemoteEndpoint*>(ajObj.router.FindEndpoint(b2bEpName));
                if (!b2bEp) {
                    replyCode = ALLJOYN_JOINSESSION_REPLY_FAILED;
                }
                /* Try to re-find sessionEp */
                ep = ajObj.router.FindEndpoint(sessionName);
                sessionEp = (ep && (ep->GetEndpointType() == BusEndpoint::ENDPOINT_TYPE_VIRTUAL)) ? static_cast<VirtualEndpoint*>(ep) : NULL;
            }
        }
    }

    /* Step 2: Send a session attach */
    /* Get B2B endpoint if not specified during connect */
    bool reusedB2b = (b2bEp == NULL);
    if (replyCode == ALLJOYN_JOINSESSION_REPLY_SUCCESS) {
        /* Get the B2B ep if not already known */
        if (!b2bEp) {
            b2bEp = sessionEp->GetQosCompatibleB2B(qosIn);
            if (!b2bEp) {
                replyCode = ALLJOYN_JOINSESSION_REPLY_UNREACHABLE;
            }
        }
    }

    /* Send AttachSession */
    if (replyCode == ALLJOYN_JOINSESSION_REPLY_SUCCESS) {
        Message reply(ajObj.bus);
        String endControllerName = sessionEp->GetControllerUniqueName();
        MsgArg attachArgs[5];
        attachArgs[0].Set("s", sessionName.c_str());
        attachArgs[1].Set("s", msg->GetSender());
        attachArgs[2].Set("s", sessionEp->GetUniqueName().c_str());
        attachArgs[3].Set("s", b2bEp->GetUniqueName().c_str());
        attachArgs[4].Set("(qqq)", qosIn.proximity, qosIn.traffic, qosIn.transports);
        ProxyBusObject controllerObj(ajObj.bus, endControllerName.c_str(), org::alljoyn::Daemon::ObjectPath, 0);
        controllerObj.AddInterface(*ajObj.daemonIface);
        QCC_DbgPrintf(("Sending AttachSession(%s, %s, %s, %s, <%x, %x, %x>) to %s",
                       attachArgs[0].v_string.str,
                       attachArgs[1].v_string.str,
                       attachArgs[2].v_string.str,
                       attachArgs[3].v_string.str,
                       qosIn.proximity, qosIn.traffic, qosIn.transports,
                       endControllerName.c_str()));

        /* Give up the locks during the sync method call */
        ajObj.virtualEndpointsLock.Unlock();
        ajObj.discoverMapLock.Unlock();
        ajObj.router.UnlockNameTable();
        status = controllerObj.MethodCall(org::alljoyn::Daemon::InterfaceName,
                                          "AttachSession",
                                          attachArgs,
                                          ArraySize(attachArgs),
                                          reply);
        if ((status != ER_OK) || (reply->GetType() != MESSAGE_METHOD_RET)) {
            if (status == ER_OK) {
                status = ER_BUS_REPLY_IS_ERROR_MESSAGE;
            }
            replyCode = ALLJOYN_JOINSESSION_REPLY_FAILED;
            QCC_LogError(status, ("AttachSession failed"));

        } else {
            /* Parse the response to AttachSession */
            size_t na;
            const MsgArg* attachSessionReplyArgs;
            reply->GetArgs(na, attachSessionReplyArgs);
            assert(na == 3);
            attachSessionReplyArgs[0].Get("u", &replyCode);
            attachSessionReplyArgs[1].Get("u", &id);
            attachSessionReplyArgs[2].Get("(qqq)", &qosOut.proximity, &qosOut.traffic, &qosOut.transports);
            QCC_DbgPrintf(("Received AttachSession response: replyCode=%d, sessionId=0x%x, qos=<%x, %x, %x>",
                           replyCode, id, qosOut.proximity, qosOut.traffic, qosOut.transports));
        }

        /* Re-lock and re-acquire b2bEp */
        ajObj.router.LockNameTable();
        ajObj.discoverMapLock.Lock();
        ajObj.virtualEndpointsLock.Lock();

        if (!b2bEpName.empty()) {
            b2bEp = static_cast<RemoteEndpoint*>(ajObj.router.FindEndpoint(b2bEpName));
        }
        sessionEp = static_cast<VirtualEndpoint*>(ajObj.router.FindEndpoint(sessionName));
        if (!sessionEp) {
            QCC_LogError(ER_FAIL, ("Session destination unexpectedly left the bus"));
            replyCode = ALLJOYN_JOINSESSION_REPLY_FAILED;
        }
    }

    /* If session was unsuccessful, we need to cleanup any b2b ep that was created */
    if (b2bEp && !reusedB2b && (replyCode != ALLJOYN_JOINSESSION_REPLY_SUCCESS)) {
        b2bEp->DecrementRef();
    }

    /* If session was successful, Add a session to the table */
    if (b2bEp && (replyCode == ALLJOYN_JOINSESSION_REPLY_SUCCESS)) {
        status = ajObj.router.AddSessionRoute(msg->GetSender(), id, *sessionEp, b2bEp, b2bEp ? NULL : &qosIn);
        if (status != ER_OK) {
            replyCode = ALLJOYN_JOINSESSION_REPLY_FAILED;
        }
    }

    ajObj.virtualEndpointsLock.Unlock();
    ajObj.discoverMapLock.Unlock();
    ajObj.router.UnlockNameTable();

    /* Reply to request */
    MsgArg replyArgs[3];
    replyArgs[0].Set("u", replyCode);
    replyArgs[1].Set("u", id);
    replyArgs[2].Set("(qqq)", qosOut.proximity, qosOut.traffic, qosOut.transports);
    status = ajObj.MethodReply(msg, replyArgs, ArraySize(replyArgs));
    QCC_DbgPrintf(("AllJoynObj::JoinSession(%s) returned (%d,%u) (status=%s)", sessionName.c_str(), replyCode, id, QCC_StatusText(status)));

    /* Log error if reply could not be sent */
    if (ER_OK != status) {
        QCC_LogError(status, ("Failed to respond to org.alljoyn.Bus.JoinSession"));
    }
    return 0;
}

void AllJoynObj::JoinSessionThread::ThreadExit(Thread* thread)
{
    ajObj.joinSessionThreadsLock.Lock();
    vector<JoinSessionThread*>::iterator it = ajObj.joinSessionThreads.begin();
    bool isFound = false;
    while (it != ajObj.joinSessionThreads.end()) {
        if (*it == thread) {
            ajObj.joinSessionThreads.erase(it);
            isFound = true;
            break;
        }
        ++it;
    }
    if (!isFound) {
        QCC_LogError(ER_FAIL, ("Internal error: JoinSessionThread not found on list"));
    }
    ajObj.joinSessionThreadsLock.Unlock();
}

void AllJoynObj::JoinSession(const InterfaceDescription::Member* member, Message& msg)
{
    /* Handle JoinSession on another thread since JoinThread can block waiting for NameOwnerChanged */
    joinSessionThreadsLock.Lock();
    if (!isStopping) {
        JoinSessionThread* jst = new JoinSessionThread(*this, msg);
        QStatus status = jst->Start(NULL, jst);
        if (status == ER_OK) {
            joinSessionThreads.push_back(jst);
        } else {
            QCC_LogError(status, ("Failed to start JoinSessionThread"));
        }
    }
    joinSessionThreadsLock.Unlock();
}

void AllJoynObj::LeaveSession(const InterfaceDescription::Member* member, Message& msg)
{
    uint32_t replyCode = ALLJOYN_LEAVESESSION_REPLY_FAILED;

    size_t numArgs;
    const MsgArg* args;

    /* Parse the message args */
    msg->GetArgs(numArgs, args);
    assert(numArgs == 1);
    SessionId id = static_cast<SessionId>(args[0].v_uint32);

    QCC_DbgTrace(("AllJoynObj::LeaveSession(%d)", id));

    /* Find the session with that id */
    sessionMapLock.Lock();
    map<SessionId, SessionMapEntry>::iterator it = sessionMap.find(id);
    if ((it == sessionMap.end()) || (it->second.endpointName != msg->GetSender())) {
        sessionMapLock.Unlock();
        replyCode = ALLJOYN_LEAVESESSION_REPLY_NO_SESSION;
    } else {
        /* Remove entry from sessionMap */
        sessionMap.erase(it);
        sessionMapLock.Unlock();

        /* Send DetachSession signal */
        MsgArg detachSessionArgs[2];
        detachSessionArgs[0].Set("u", id);
        detachSessionArgs[1].Set("s", msg->GetSender());
        QStatus status = Signal(NULL, id, *detachSessionSignal, detachSessionArgs, ArraySize(detachSessionArgs));
        if (status != ER_OK) {
            QCC_LogError(status, ("Error sending org.alljoyn.Daemon.DetachSession signal"));
        }

        /* Decrement local ref counts */
        // TODO
    }

    /* Reply to request */
    MsgArg replyArgs[1];
    replyArgs[0].Set("u", replyCode);
    QStatus status = MethodReply(msg, replyArgs, ArraySize(replyArgs));
    QCC_DbgPrintf(("AllJoynObj::LeaveSession(%d) returned (%u) (status=%s)", id, replyCode, QCC_StatusText(status)));

    /* Log error if reply could not be sent */
    if (ER_OK != status) {
        QCC_LogError(status, ("Failed to respond to org.alljoyn.Bus.LeaveSession"));
    }
}

void AllJoynObj::AttachSession(const InterfaceDescription::Member* member, Message& msg)
{
    QStatus status;
    SessionId id = 0;
    QosInfo qosOut = { QosInfo::PROXIMITY_ANY, QosInfo::TRAFFIC_ANY, QosInfo::TRANSPORT_ANY };
    uint32_t replyCode = ALLJOYN_JOINSESSION_REPLY_FAILED;

    /* Received a daemon request to establish a session route */

    /* Parse message args */
    const char* sessionName;
    const char* src;
    const char* dest;
    const char* srcB2B;
    QosInfo inQos;

    size_t na;
    const MsgArg* args;
    msg->GetArgs(na, args);
    assert(na == 5);

    args[0].Get("s", &sessionName);
    args[1].Get("s", &src);
    args[2].Get("s", &dest);
    args[3].Get("s", &srcB2B);
    args[4].Get("(qqq)", &inQos.proximity, &inQos.traffic, &inQos.transports);

    router.LockNameTable();
    discoverMapLock.Lock();
    BusEndpoint* destEp = router.FindEndpoint(dest);

    /* Determine if the dest is local to this daemon */
    if (!destEp) {
        /* no route */
        replyCode = ALLJOYN_JOINSESSION_REPLY_UNREACHABLE;
    } else if ((destEp->GetEndpointType() == BusEndpoint::ENDPOINT_TYPE_REMOTE) ||
               (destEp->GetEndpointType() == BusEndpoint::ENDPOINT_TYPE_LOCAL)) {
        /* This daemon serves dest directly */

        /* Check for a session in the session map */
        map<SessionId, SessionMapEntry>::const_iterator sit = sessionMap.begin();
        while (sit != sessionMap.end()) {
            if (sit->second.name == sessionName) {
                break;
            }
        }
        if (sit == sessionMap.end()) {
            /* No such session */
            replyCode = ALLJOYN_JOINSESSION_REPLY_NO_SESSION;
        } else if (!sit->second.qos.IsCompatible(inQos)) {
            replyCode = ALLJOYN_JOINSESSION_REPLY_BAD_QOS;
            qosOut = sit->second.qos;
        } else {
            /* Give the endpoint a chance to accept or reject the new member */
            Message reply(bus);
            MsgArg acceptArgs[4];
            acceptArgs[0].Set("s", sessionName);
            acceptArgs[1].Set("s", src);
            acceptArgs[2].Set("s", dest);
            acceptArgs[3].Set("(qqq)", inQos.proximity, inQos.traffic, inQos.transports);
            ProxyBusObject peerObj(bus, dest, org::alljoyn::Bus::Peer::ObjectPath, 0);
            const InterfaceDescription* sessionIntf = bus.GetInterface(org::alljoyn::Bus::Peer::Session::InterfaceName);
            assert(sessionIntf);
            peerObj.AddInterface(*sessionIntf);
            QCC_DbgPrintf(("Calling AcceptSession(%s, %s, %s, <%x, %x, %x> on %s",
                           acceptArgs[0].v_string.str,
                           acceptArgs[1].v_string.str,
                           acceptArgs[2].v_string.str,
                           inQos.proximity, inQos.traffic, inQos.transports,
                           dest));
            discoverMapLock.Unlock();
            router.UnlockNameTable();
            status = peerObj.MethodCall(org::alljoyn::Bus::Peer::Session::InterfaceName,
                                        "AcceptSession",
                                        acceptArgs,
                                        ArraySize(acceptArgs),
                                        reply);
            router.LockNameTable();
            discoverMapLock.Lock();
            if ((status == ER_OK) && (reply->GetType() == MESSAGE_METHOD_RET)) {
                size_t na;
                const MsgArg* replyArgs;
                reply->GetArgs(na, replyArgs);
                bool isAccepted;
                replyArgs[0].Get("b", &isAccepted);

                BusEndpoint* ep = router.FindEndpoint(srcB2B);
                RemoteEndpoint* srcB2BEp = (ep && (ep->GetEndpointType() == BusEndpoint::ENDPOINT_TYPE_BUS2BUS)) ? static_cast<RemoteEndpoint*>(ep) : NULL;
                ep = router.FindEndpoint(src);
                VirtualEndpoint* srcEp = (ep && (ep->GetEndpointType() == BusEndpoint::ENDPOINT_TYPE_VIRTUAL)) ? static_cast<VirtualEndpoint*>(ep) : NULL;
                if (srcEp && srcB2BEp && isAccepted) {
                    id = sit->second.id;
                    qosOut = sit->second.qos;
                    replyCode = ALLJOYN_JOINSESSION_REPLY_SUCCESS;
                    sessionMap[id].memberNames.push_back(src);
                    router.AddSessionRoute(dest, id, *srcEp, srcB2BEp);
                } else {
                    replyCode = (srcB2BEp && srcEp) ? ALLJOYN_JOINSESSION_REPLY_REJECTED : ALLJOYN_JOINSESSION_REPLY_FAILED;
                }
            } else {
                if (status == ER_OK) {
                    status = ER_BUS_REPLY_IS_ERROR_MESSAGE;
                }
                replyCode = ALLJOYN_JOINSESSION_REPLY_FAILED;
                QCC_LogError(status, ("AcceptSession failed"));
            }
        }
    } else if (destEp->GetEndpointType() == BusEndpoint::ENDPOINT_TYPE_VIRTUAL) {
        /* This daemon routes indirectly to dest */

        /* Get a QoS compatible b2b ep for dest */
        VirtualEndpoint* vDestEp = static_cast<VirtualEndpoint*>(destEp);
        RemoteEndpoint* destB2B = vDestEp->GetQosCompatibleB2B(inQos);
        if (destB2B) {
            /* Forward AttachSession to next hop */
            Message reply(bus);
            String endControllerName = vDestEp->GetControllerUniqueName();
            MsgArg attachArgs[5];
            attachArgs[0].Set("s", sessionName);
            attachArgs[1].Set("s", src);
            attachArgs[2].Set("s", dest);
            attachArgs[3].Set("s", destB2B->GetUniqueName().c_str());
            attachArgs[4].Set("(qqq)", inQos.proximity, inQos.traffic, inQos.transports);
            ProxyBusObject controllerObj(bus, endControllerName.c_str(), org::alljoyn::Daemon::ObjectPath, 0);
            controllerObj.AddInterface(*daemonIface);
            QCC_DbgPrintf(("Forwarding AttachSession(%s, %s, %s, %s, <%x, %x, %x>) to %s",
                           attachArgs[0].v_string.str,
                           attachArgs[1].v_string.str,
                           attachArgs[2].v_string.str,
                           attachArgs[3].v_string.str,
                           inQos.proximity, inQos.traffic, inQos.transports,
                           endControllerName.c_str()));
            discoverMapLock.Unlock();
            router.UnlockNameTable();
            status = controllerObj.MethodCall(org::alljoyn::Daemon::InterfaceName, "AttachSession", attachArgs, ArraySize(attachArgs), reply);
            router.LockNameTable();
            discoverMapLock.Lock();


            if ((status == ER_OK) && (reply->GetType() == MESSAGE_METHOD_RET)) {
                size_t na;
                const MsgArg* replyArgs;
                reply->GetArgs(na, replyArgs);
                SessionId tempId;
                QosInfo tempQos;

                replyArgs[0].Get("u", &replyCode);
                replyArgs[1].Get("u", &tempId);
                replyArgs[2].Get("(qqq)", &tempQos.proximity, &tempQos.traffic, &tempQos.transports);

                if (replyCode == ALLJOYN_JOINSESSION_REPLY_SUCCESS) {
                    BusEndpoint* ep = router.FindEndpoint(srcB2B);
                    RemoteEndpoint* srcB2BEp = (ep && (ep->GetEndpointType() == BusEndpoint::ENDPOINT_TYPE_REMOTE)) ? static_cast<RemoteEndpoint*>(ep) : NULL;
                    ep = router.FindEndpoint(src);
                    VirtualEndpoint* srcEp = (ep && (ep->GetEndpointType() == BusEndpoint::ENDPOINT_TYPE_VIRTUAL)) ? static_cast<VirtualEndpoint*>(ep) : NULL;

                    if (srcB2BEp && srcEp) {
                        router.AddSessionRoute(dest, id, *srcEp, srcB2BEp);
                        router.AddSessionRoute(src, id, *vDestEp, destB2B);
                        id = tempId;
                        qosOut = tempQos;
                    } else {
                        // TODO: Need to cleanup partially setup session
                        replyCode = ALLJOYN_JOINSESSION_REPLY_FAILED;
                    }
                } else {
                    if (status == ER_OK) {
                        status = ER_BUS_REPLY_IS_ERROR_MESSAGE;
                    }
                    replyCode = ALLJOYN_JOINSESSION_REPLY_FAILED;
                    QCC_LogError(status, ("AttachSession failed"));
                }
            }
        }
    }

    discoverMapLock.Unlock();
    router.UnlockNameTable();

    /* Reply to request */
    MsgArg replyArgs[3];
    replyArgs[0].Set("u", replyCode);
    replyArgs[1].Set("u", id);
    replyArgs[2].Set("(qqq)", qosOut.proximity, qosOut.traffic, qosOut.transports);
    status = MethodReply(msg, replyArgs, ArraySize(replyArgs));
    QCC_DbgPrintf(("AllJoynObj::AttachSession(%s) returned (%d,%u) (status=%s)", sessionName, replyCode, id, QCC_StatusText(status)));

    /* Log error if reply could not be sent */
    if (ER_OK != status) {
        QCC_LogError(status, ("Failed to respond to org.alljoyn.Daemon.AttachSession"));
    }
}

void AllJoynObj::DetachSessionSignalHandler(const InterfaceDescription::Member* member, const char* sourcePath, Message& msg)
{
}

void AllJoynObj::AdvertiseName(const InterfaceDescription::Member* member, Message& msg)
{
    uint32_t replyCode = ALLJOYN_ADVERTISENAME_REPLY_SUCCESS;
    size_t numArgs;
    const MsgArg* args;
    MsgArg replyArg;

    /* Get the name being advertised */
    msg->GetArgs(numArgs, args);
    assert((numArgs == 1) && (args[0].typeId == ALLJOYN_STRING));
    qcc::String advertiseName = args[0].v_string.str;

    QCC_DbgTrace(("AllJoynObj::AdvertiseName(%s)", advertiseName.c_str()));

    /* Get the sender name */
    qcc::String sender = msg->GetSender();

    /* Check to see if the advertise name is valid and well formed */
    if (IsLegalBusName(advertiseName.c_str())) {

        /* Check to see if advertiseName is already being advertised */
        advertiseMapLock.Lock();
        multimap<qcc::String, qcc::String>::const_iterator it = advertiseMap.find(advertiseName);

        while (it != advertiseMap.end() && (it->first == advertiseName)) {
            if (it->second == sender) {
                replyCode = ALLJOYN_ADVERTISENAME_REPLY_ALREADY_ADVERTISING;
                break;
            }
            ++it;
        }

        if (ALLJOYN_ADVERTISENAME_REPLY_SUCCESS == replyCode) {
            /* Add to advertise map */
            advertiseMap.insert(pair<qcc::String, qcc::String>(advertiseName, sender));

            /* Advertise on all transports */
            TransportList& transList = bus.GetInternal().GetTransportList();
            for (size_t i = 0; i < transList.GetNumTransports(); ++i) {
                Transport* trans = transList.GetTransport(i);
                if (trans) {
                    trans->EnableAdvertisement(advertiseName);
                } else {
                    QCC_LogError(ER_BUS_TRANSPORT_NOT_AVAILABLE, ("NULL transport pointer found in transportList"));
                }
            }
        }
        advertiseMapLock.Unlock();

    } else {
        replyCode = ALLJOYN_ADVERTISENAME_REPLY_FAILED;
    }

    QCC_DbgPrintf(("Advertise: sender = \"%s\", advertiseName = \"%s\", replyCode= %u",
                   sender.c_str(), advertiseName.c_str(), replyCode));

    /* Reply to request */
    replyArg.Set("u", replyCode);
    QStatus status = MethodReply(msg, &replyArg, 1);
    QCC_DbgPrintf(("AllJoynObj::Advertise(%s) returned %d (status=%s)", advertiseName.c_str(), replyCode, QCC_StatusText(status)));

    /* Log error if reply could not be sent */
    if (ER_OK != status) {
        QCC_LogError(status, ("Failed to respond to org.alljoyn.Bus.Advertise"));
    }
}

void AllJoynObj::CancelAdvertiseName(const InterfaceDescription::Member* member, Message& msg)
{
    QCC_DbgTrace(("AllJoynObj::CancelAdvertise()"));

    size_t numArgs;
    const MsgArg* args;

    /* Get the name being advertised */
    msg->GetArgs(numArgs, args);
    assert((numArgs == 1) && (args[0].typeId == ALLJOYN_STRING));

    /* Cancel advertisement */
    QStatus status = ProcCancelAdvertise(msg->GetSender(), args[0].v_string.str);
    uint32_t replyCode = (ER_OK == status) ? ALLJOYN_CANCELADVERTISENAME_REPLY_SUCCESS : ALLJOYN_CANCELADVERTISENAME_REPLY_FAILED;

    /* Reply to request */
    MsgArg replyArg("u", replyCode);
    status = MethodReply(msg, &replyArg, 1);
    // QCC_DbgPrintf(("AllJoynObj::CancelAdvertise(%s) returned %d (status=%s)", args[0].v_string.str, replyCode, QCC_StatusText(status)));

    /* Log error if reply could not be sent */
    if (ER_OK != status) {
        QCC_LogError(status, ("Failed to respond to org.alljoyn.Bus.CancelAdvertise"));
    }
}

QStatus AllJoynObj::ProcCancelAdvertise(const qcc::String& sender, const qcc::String& advertiseName)
{
    QCC_DbgTrace(("AllJoynObj::ProcCancelAdvertise(sender = \"%s\", advertiseName = \"%s\")",
                  sender.c_str(), advertiseName.c_str()));

    QStatus status = ER_OK;

    /* Check to see if this advertised name exists and delete it */
    bool foundAdvert = false;
    bool advertHasRefs = false;

    advertiseMapLock.Lock();
    multimap<qcc::String, qcc::String>::iterator it = advertiseMap.find(advertiseName);

    while ((it != advertiseMap.end()) && (it->first == advertiseName)) {
        if (it->second == sender) {
            advertiseMap.erase(it++);
            foundAdvert = true;
        } else {
            advertHasRefs = true;
            ++it;
        }
    }

    /* Cancel transport advertisement if no other refs exist */
    if (foundAdvert && !advertHasRefs) {
        TransportList& transList = bus.GetInternal().GetTransportList();
        for (size_t i = 0; i < transList.GetNumTransports(); ++i) {
            Transport* trans = transList.GetTransport(i);
            if (trans) {
                trans->DisableAdvertisement(advertiseName, advertiseMap.empty());
            } else {
                QCC_LogError(ER_BUS_TRANSPORT_NOT_AVAILABLE, ("NULL transport pointer found in transportList"));
            }
        }
    } else if (!foundAdvert) {
        status = ER_FAIL;
    }
    advertiseMapLock.Unlock();
    return status;
}

#if 0
void AllJoynObj::ListAdvertisedNames(const InterfaceDescription::Member* member, Message& msg)
{
    QCC_DbgTrace(("AllJoynObj::ListAdvertisedNames()"));
    QStatus status;

    advertiseMapLock.Lock();

    MsgArg* names(new MsgArg[advertiseMap.size()]);
    size_t count(0);
    multimap<qcc::String, qcc::String>::const_iterator it(advertiseMap.begin());

    while (it != advertiseMap.end()) {
        const qcc::String& name(it->first);
        QCC_DbgPrintf(("AllJoynObj::ListAdvertisedNames - Name[%u] = %s", count, name.c_str()));
        names[count++].Set("s", name.c_str());

        // skip to next name
        it = advertiseMap.upper_bound(name);
    }

    advertiseMapLock.Unlock();

    MsgArg replyArg;

    if (count > 0) {
        replyArg.Set("a*", count, names);
        replyArg.SetOwnershipFlags(MsgArg::OwnsArgs);
    } else {
        replyArg.Set("as", 0);
    }

    status = MethodReply(msg, &replyArg, 1);
    QCC_DbgPrintf(("AllJoynObj::ListAdvertisedNames() returned %u names (status=%s)",
                   count, QCC_StatusText(status)));

    /* Log error if reply could not be sent */
    if (status != ER_OK) {
        QCC_LogError(status, ("Failed to respond to org.alljoyn.Bus.ListAdvertisedNames"));
    }
}
#endif

void AllJoynObj::GetAdvertisedNames(std::vector<qcc::String>& names)
{
    advertiseMapLock.Lock();
    multimap<qcc::String, qcc::String>::const_iterator it(advertiseMap.begin());
    while (it != advertiseMap.end()) {
        const qcc::String& name(it->first);
        QCC_DbgPrintf(("AllJoynObj::GetAdvertisedNames - Name[%u] = %s", names.size(), name.c_str()));
        names.push_back(name);
        // skip to next name
        it = advertiseMap.upper_bound(name);
    }
    advertiseMapLock.Unlock();
}

void AllJoynObj::FindAdvertisedName(const InterfaceDescription::Member* member, Message& msg)
{
    uint32_t replyCode;
    size_t numArgs;
    const MsgArg* args;

    /* Get the name prefix */
    msg->GetArgs(numArgs, args);
    assert((numArgs == 1) && (args[0].typeId == ALLJOYN_STRING));
    String namePrefix = args[0].v_string.str;

    QCC_DbgTrace(("AllJoynObj::FindAdvertisedName( <namePrefix = \"%s\"> )", namePrefix.c_str()));


    /* Check to see if this endpoint is already discovering this prefix */
    qcc::String sender = msg->GetSender();
    replyCode = ALLJOYN_FINDADVERTISEDNAME_REPLY_SUCCESS;
    router.LockNameTable();
    discoverMapLock.Lock();
    multimap<qcc::String, qcc::String>::const_iterator it = discoverMap.find(namePrefix);
    while ((it != discoverMap.end()) && (it->first == namePrefix)) {
        if (it->second == sender) {
            replyCode = ALLJOYN_FINDADVERTISEDNAME_REPLY_ALREADY_DISCOVERING;
            break;
        }
        ++it;
    }
    if (ALLJOYN_FINDADVERTISEDNAME_REPLY_SUCCESS == replyCode) {
        /* Add to discover map */
        discoverMap.insert(pair<String, String>(namePrefix, sender));

        /* Find name  on all remote transports */
        TransportList& transList = bus.GetInternal().GetTransportList();
        for (size_t i = 0; i < transList.GetNumTransports(); ++i) {
            Transport* trans = transList.GetTransport(i);
            if (trans) {
                trans->EnableDiscovery(namePrefix.c_str());
            } else {
                QCC_LogError(ER_BUS_TRANSPORT_NOT_AVAILABLE, ("NULL transport pointer found in transportList"));
            }
        }
    }

    /* Reply to request */
    MsgArg replyArg("u", replyCode);
    QStatus status = MethodReply(msg, &replyArg, 1);
    QCC_DbgPrintf(("AllJoynObj::Discover(%s) returned %d (status=%s)", namePrefix.c_str(), replyCode, QCC_StatusText(status)));

    /* Log error if reply could not be sent */
    if (ER_OK != status) {
        QCC_LogError(status, ("Failed to respond to org.alljoyn.Bus.Discover"));
    }

    /* Send FoundAdvertisedName signals if there are existing matches for namePrefix */
    if (ALLJOYN_FINDADVERTISEDNAME_REPLY_SUCCESS == replyCode) {
        multimap<String, NameMapEntry>::iterator it = nameMap.lower_bound(namePrefix);
        while ((it != nameMap.end()) && (0 == strncmp(it->first.c_str(), namePrefix.c_str(), namePrefix.size()))) {
            status = SendFoundAdvertisedName(sender, it->first, it->second.qos, namePrefix);
            if (ER_OK != status) {
                QCC_LogError(status, ("Cannot send FoundAdvertisedName to %s for name=%s", sender.c_str(), it->first.c_str()));
            }
            ++it;
        }
    }
    discoverMapLock.Unlock();
    router.UnlockNameTable();
}

void AllJoynObj::CancelFindAdvertisedName(const InterfaceDescription::Member* member, Message& msg)
{
    size_t numArgs;
    const MsgArg* args;

    /* Get the name prefix to be removed from discovery */
    msg->GetArgs(numArgs, args);
    assert((numArgs == 1) && (args[0].typeId == ALLJOYN_STRING));

    /* Cancel advertisement */
    QCC_DbgPrintf(("Calling ProcCancelFindName from CancelFindAdvertisedName [%s]", Thread::GetThread()->GetName().c_str()));
    QStatus status = ProcCancelFindName(msg->GetSender(), args[0].v_string.str);
    uint32_t replyCode = (ER_OK == status) ? ALLJOYN_CANCELFINDADVERTISEDNAME_REPLY_SUCCESS : ALLJOYN_CANCELFINDADVERTISEDNAME_REPLY_FAILED;

    /* Reply to request */
    MsgArg replyArg("u", replyCode);
    status = MethodReply(msg, &replyArg, 1);
    // QCC_DbgPrintf(("AllJoynObj::CancelDiscover(%s) returned %d (status=%s)", args[0].v_string.str, replyCode, QCC_StatusText(status)));

    /* Log error if reply could not be sent */
    if (ER_OK != status) {
        QCC_LogError(status, ("Failed to respond to org.alljoyn.Bus.CancelDiscover"));
    }
}

QStatus AllJoynObj::ProcCancelFindName(const qcc::String& sender, const qcc::String& namePrefix)
{
    QCC_DbgTrace(("AllJoynObj::ProcCancelFindName(sender = %s, namePrefix = %s)", sender.c_str(), namePrefix.c_str()));
    QStatus status = ER_OK;

    /* Check to see if this prefix exists and delete it */
    bool foundNamePrefix = false;
    discoverMapLock.Lock();
    multimap<qcc::String, qcc::String>::iterator it = discoverMap.find(namePrefix);
    while ((it != discoverMap.end()) && (it->first == namePrefix)) {
        if (it->second == sender) {
            discoverMap.erase(it);
            foundNamePrefix = true;
            break;
        }
        ++it;
    }

    /* Disable discovery if we found a name */
    if (foundNamePrefix) {
        TransportList& transList = bus.GetInternal().GetTransportList();
        for (size_t i = 0; i < transList.GetNumTransports(); ++i) {
            Transport* trans =  transList.GetTransport(i);
            if (trans) {
                trans->DisableDiscovery(namePrefix.c_str());
            } else {
                QCC_LogError(ER_BUS_TRANSPORT_NOT_AVAILABLE, ("NULL transport pointer found in transportList"));
            }
        }
    } else if (!foundNamePrefix) {
        status = ER_FAIL;
    }
    discoverMapLock.Unlock();
    return status;
}

QStatus AllJoynObj::AddBusToBusEndpoint(RemoteEndpoint& endpoint)
{
    QCC_DbgTrace(("AllJoynObj::AddBusToBusEndpoint(%s)", endpoint.GetUniqueName().c_str()));

    const qcc::String& shortGuidStr = endpoint.GetRemoteGUID().ToShortString();

    /* Add b2b endpoint */
    b2bEndpointsLock.Lock();
    b2bEndpoints[endpoint.GetUniqueName()] = &endpoint;
    b2bEndpointsLock.Unlock();

    /* Create a virtual endpoint for talking to the remote bus control object */
    /* This endpoint will also carry broadcast messages for the remote bus */
    String remoteControllerName(":", 1, 16);
    remoteControllerName.append(shortGuidStr);
    remoteControllerName.append(".1");
    AddVirtualEndpoint(remoteControllerName, endpoint);

    /* Exchange existing bus names if connected to another daemon */
    return ExchangeNames(endpoint);
}

void AllJoynObj::RemoveBusToBusEndpoint(RemoteEndpoint& endpoint)
{
    QCC_DbgTrace(("AllJoynObj::RemoveBusToBusEndpoint(%s)", endpoint.GetUniqueName().c_str()));

    /* Remove any virtual endpoints associated with a removed bus-to-bus endpoint */

    /* Be careful to lock the name table before locking the virtual endpoints since both locks are needed
     * and doing it in the opposite order invites deadlock
     */
    router.LockNameTable();
    virtualEndpointsLock.Lock();
    map<qcc::String, VirtualEndpoint*>::iterator it = virtualEndpoints.begin();
    while (it != virtualEndpoints.end()) {
        if (it->second->RemoveBusToBusEndpoint(endpoint)) {

            /* Remove virtual endpoint with no more b2b eps */
            String exitingEpName = it->second->GetUniqueName();
            RemoveVirtualEndpoint(*(it++->second));

            /* Let directly connected daemons know that this virtual endpoint is gone. */
            map<qcc::StringMapKey, RemoteEndpoint*>::iterator it2 = b2bEndpoints.begin();
            while (it2 != b2bEndpoints.end()) {
                if (it2->second != &endpoint) {
                    Message sigMsg(bus);
                    MsgArg args[3];
                    args[0].Set("s", exitingEpName.c_str());
                    args[1].Set("s", exitingEpName.c_str());
                    args[2].Set("s", "");

                    QStatus status = sigMsg->SignalMsg("sss",
                                                       org::alljoyn::Daemon::WellKnownName,
                                                       0,
                                                       org::alljoyn::Daemon::ObjectPath,
                                                       org::alljoyn::Daemon::InterfaceName,
                                                       "NameChanged",
                                                       args,
                                                       ArraySize(args),
                                                       0,
                                                       0);
                    if (ER_OK == status) {
                        status = it2->second->PushMessage(sigMsg);
                    }
                    if (ER_OK != status) {
                        QCC_LogError(status, ("Failed to send NameChanged to %s", it2->second->GetUniqueName().c_str()));
                    }
                }
                ++it2;
            }
        } else {
            ++it;
        }
    }
    virtualEndpointsLock.Unlock();
    router.UnlockNameTable();

    /* Remove the B2B endpoint itself */
    b2bEndpointsLock.Lock();
    b2bEndpoints.erase(endpoint.GetUniqueName());
    b2bEndpointsLock.Unlock();
}

QStatus AllJoynObj::ExchangeNames(RemoteEndpoint& endpoint)
{
    vector<pair<qcc::String, vector<qcc::String> > > names;
    QStatus status;
    const qcc::String& shortGuidStr = endpoint.GetRemoteGUID().ToShortString();
    const size_t shortGuidLen = shortGuidStr.size();

    /* Send local name table info to remote bus controller */
    router.LockNameTable();
    router.GetUniqueNamesAndAliases(names);

    MsgArg argArray(ALLJOYN_ARRAY);
    MsgArg* entries = new MsgArg[names.size()];
    size_t numEntries = 0;
    vector<pair<qcc::String, vector<qcc::String> > >::const_iterator it = names.begin();

    /* Send all endpoint info except for endpoints related to destination */
    while (it != names.end()) {
        if ((it->first.size() <= shortGuidLen) || (0 != it->first.compare(1, shortGuidLen, shortGuidStr))) {
            MsgArg* aliasNames = new MsgArg[it->second.size()];
            vector<qcc::String>::const_iterator ait = it->second.begin();
            size_t numAliases = 0;
            while (ait != it->second.end()) {
                /* Send exportable endpoints */
                // if ((ait->size() > guidLen) && (0 == ait->compare(ait->size() - guidLen, guidLen, guidStr))) {
                if (true) {
                    aliasNames[numAliases++].Set("s", ait->c_str());
                }
                ++ait;
            }
            if (0 < numAliases) {
                entries[numEntries].Set("(sa*)", it->first.c_str(), numAliases, aliasNames);
                /*
                 * Set ownwership flag so entries array destructor will free inner message args.
                 */
                entries[numEntries].SetOwnershipFlags(MsgArg::OwnsArgs, true);
            } else {
                entries[numEntries].Set("(sas)", it->first.c_str(), 0, NULL);
                delete[] aliasNames;
            }
            ++numEntries;
        }
        ++it;
    }
    status = argArray.Set("a(sas)", numEntries, entries);
    if (ER_OK == status) {
        Message exchangeMsg(bus);
        status = exchangeMsg->SignalMsg("a(sas)",
                                        org::alljoyn::Daemon::WellKnownName,
                                        0,
                                        org::alljoyn::Daemon::ObjectPath,
                                        org::alljoyn::Daemon::InterfaceName,
                                        "ExchangeNames",
                                        &argArray,
                                        1,
                                        0,
                                        0);
        if (ER_OK == status) {
            status = endpoint.PushMessage(exchangeMsg);
        }
    }
    /*
     * This will also free the inner MsgArgs.
     */
    delete [] entries;

    router.UnlockNameTable();

    return status;
}

void AllJoynObj::ExchangeNamesSignalHandler(const InterfaceDescription::Member* member, const char* sourcePath, Message& msg)
{
    QCC_DbgTrace(("AllJoynObj::ExchangeNamesSignalHandler(msg sender = \"%s\")", msg->GetSender()));

    bool madeChanges = false;
    size_t numArgs;
    const MsgArg* args;
    msg->GetArgs(numArgs, args);
    assert((1 == numArgs) && (ALLJOYN_ARRAY == args[0].typeId));
    const MsgArg* items = args[0].v_array.GetElements();
    const String& shortGuidStr = guid.ToShortString();

    /* Create a virtual endpoint for each unique name in args */
    /* Be careful to lock the name table before locking the virtual endpoints since both locks are needed
     * and doing it in the opposite order invites deadlock
     */
    router.LockNameTable();
    virtualEndpointsLock.Lock();
    b2bEndpointsLock.Lock();
    map<qcc::StringMapKey, RemoteEndpoint*>::iterator bit = b2bEndpoints.find(msg->GetRcvEndpointName());
    const size_t numItems = args[0].v_array.GetNumElements();
    if (bit != b2bEndpoints.end()) {
        for (size_t i = 0; i < numItems; ++i) {
            assert(items[i].typeId == ALLJOYN_STRUCT);
            qcc::String uniqueName = items[i].v_struct.members[0].v_string.str;
            if (!IsLegalUniqueName(uniqueName.c_str())) {
                QCC_LogError(ER_FAIL, ("Invalid unique name \"%s\" in ExchangeNames message", uniqueName.c_str()));
                continue;
            } else if (0 == ::strncmp(uniqueName.c_str() + 1, shortGuidStr.c_str(), shortGuidStr.size())) {
                /* Cant accept a request to change a local name */
                continue;
            }

            bool madeChange;
            VirtualEndpoint& vep = AddVirtualEndpoint(uniqueName, *(bit->second), &madeChange);
            if (madeChange) {
                madeChanges = true;
            }

            /* Add virtual aliases (remote well-known names) */
            const MsgArg* aliasItems = items[i].v_struct.members[1].v_array.GetElements();
            const size_t numAliases = items[i].v_struct.members[1].v_array.GetNumElements();
            for (size_t j = 0; j < numAliases; ++j) {
                assert(ALLJOYN_STRING == aliasItems[j].typeId);
                bool madeChange = router.SetVirtualAlias(aliasItems[j].v_string.str, &vep, vep);
                if (madeChange) {
                    madeChanges = true;
                }
            }
        }
        b2bEndpointsLock.Unlock();
        virtualEndpointsLock.Unlock();
        router.UnlockNameTable();
    } else {
        b2bEndpointsLock.Unlock();
        virtualEndpointsLock.Unlock();
        router.UnlockNameTable();
        QCC_LogError(ER_BUS_NO_ENDPOINT, ("Cannot find b2b endpoint %s", msg->GetRcvEndpointName()));
    }

    /* If there were changes, forward message to all directly connected controllers except the one that
     * sent us this ExchangeNames
     */
    if (madeChanges) {
        b2bEndpointsLock.Lock();
        bool isRemarshaled = false;
        map<qcc::StringMapKey, RemoteEndpoint*>::const_iterator bit = b2bEndpoints.find(msg->GetRcvEndpointName());
        map<qcc::StringMapKey, RemoteEndpoint*>::iterator it = b2bEndpoints.begin();
        while (it != b2bEndpoints.end()) {
            if ((bit == b2bEndpoints.end()) || (bit->second->GetRemoteGUID() != it->second->GetRemoteGUID())) {
                if (!isRemarshaled) {
                    msg->ReMarshal(bus.GetInternal().GetLocalEndpoint().GetUniqueName().c_str(), true);
                    isRemarshaled = true;
                }
                QStatus status = it->second->PushMessage(msg);
                if (ER_OK != status) {
                    QCC_LogError(status, ("Failed to forward NameChanged to %s", it->second->GetUniqueName().c_str()));
                }
            }
            ++it;
        }
        b2bEndpointsLock.Unlock();
    }
}

void AllJoynObj::NameChangedSignalHandler(const InterfaceDescription::Member* member, const char* sourcePath, Message& msg)
{
    size_t numArgs;
    const MsgArg* args;
    msg->GetArgs(numArgs, args);

    assert(daemonIface);
    const InterfaceDescription::Member* nameChangedMember = daemonIface->GetMember("NameChanged");
    assert(nameChangedMember);

    const qcc::String alias = args[0].v_string.str;
    const qcc::String oldOwner = args[1].v_string.str;
    const qcc::String newOwner = args[2].v_string.str;

    const String& shortGuidStr = guid.ToShortString();
    bool madeChanges = false;

    QCC_DbgPrintf(("AllJoynObj::NameChangedSignalHandler: alias = \"%s\"   oldOwner = \"%s\"   newOwner = \"%s\"  sent from \"%s\"",
                   alias.c_str(), oldOwner.c_str(), newOwner.c_str(), msg->GetSender()));

    /* Don't allow a NameChange that attempts to change a local name */
    if ((!oldOwner.empty() && (0 == ::strncmp(oldOwner.c_str() + 1, shortGuidStr.c_str(), shortGuidStr.size()))) ||
        (!newOwner.empty() && (0 == ::strncmp(newOwner.c_str() + 1, shortGuidStr.c_str(), shortGuidStr.size())))) {
        return;
    }

    if (alias[0] == ':') {
        b2bEndpointsLock.Lock();
        map<qcc::StringMapKey, RemoteEndpoint*>::iterator bit = b2bEndpoints.find(msg->GetRcvEndpointName());
        if (bit != b2bEndpoints.end()) {
            /* Change affects a remote unique name (i.e. a VirtualEndpoint) */
            if (newOwner.empty()) {
                VirtualEndpoint* vep = FindVirtualEndpoint(oldOwner.c_str());
                if (vep) {
                    madeChanges = vep->CanUseRoute(*(bit->second));
                    if (vep->RemoveBusToBusEndpoint(*(bit->second))) {
                        RemoveVirtualEndpoint(*vep);
                    }
                }
            } else {
                /* Add a new virtual endpoint */
                if (bit != b2bEndpoints.end()) {
                    AddVirtualEndpoint(alias, *(bit->second), &madeChanges);
                }
            }
        } else {
            QCC_LogError(ER_BUS_NO_ENDPOINT, ("Cannot find bus-to-bus endpoint %s", msg->GetRcvEndpointName()));
        }
        b2bEndpointsLock.Unlock();
    } else {
        /* Change affects a well-known name (name table only) */
        VirtualEndpoint* remoteController = FindVirtualEndpoint(msg->GetSender());
        if (remoteController) {
            VirtualEndpoint* newOwnerEp = newOwner.empty() ? NULL : FindVirtualEndpoint(newOwner.c_str());
            madeChanges = router.SetVirtualAlias(alias, newOwnerEp, *remoteController);
        } else {
            QCC_LogError(ER_BUS_NO_ENDPOINT, ("Cannot find virtual endpoint %s", msg->GetSender()));
        }
    }

    if (madeChanges) {
        /* Forward message to all directly connected controllers except the one that sent us this NameChanged */
        b2bEndpointsLock.Lock();
        map<qcc::StringMapKey, RemoteEndpoint*>::const_iterator bit = b2bEndpoints.find(msg->GetRcvEndpointName());
        map<qcc::StringMapKey, RemoteEndpoint*>::iterator it = b2bEndpoints.begin();
        bool isReMarshaled = false;
        while (it != b2bEndpoints.end()) {
            if ((bit == b2bEndpoints.end()) || (bit->second->GetRemoteGUID() != it->second->GetRemoteGUID())) {
                if (!isReMarshaled) {
                    isReMarshaled = true;
                    msg->ReMarshal(bus.GetInternal().GetLocalEndpoint().GetUniqueName().c_str(), true);
                }
                QStatus status = it->second->PushMessage(msg);
                if (ER_OK != status) {
                    QCC_LogError(status, ("Failed to forward NameChanged to %s", it->second->GetUniqueName().c_str()));
                }
            }
            ++it;
        }
        b2bEndpointsLock.Unlock();
    }

}

VirtualEndpoint& AllJoynObj::AddVirtualEndpoint(const qcc::String& uniqueName, RemoteEndpoint& busToBusEndpoint, bool* wasAdded)
{
    QCC_DbgTrace(("AllJoynObj::AddVirtualEndpoint(name=%s, b2b=%s)", uniqueName.c_str(), busToBusEndpoint.GetUniqueName().c_str()));

    bool added = false;
    VirtualEndpoint* vep = NULL;

    virtualEndpointsLock.Lock();
    map<qcc::String, VirtualEndpoint*>::iterator it = virtualEndpoints.find(uniqueName);
    if (it == virtualEndpoints.end()) {
        /* Add new virtual endpoint */
        pair<map<qcc::String, VirtualEndpoint*>::iterator, bool> ret =

            virtualEndpoints.insert(pair<qcc::String, VirtualEndpoint*>(uniqueName,
                                                                        new VirtualEndpoint(uniqueName.c_str(), busToBusEndpoint)));
        vep = ret.first->second;
        added = true;
    } else {
        /* Add the busToBus endpoint to the existing virtual endpoint */
        vep = it->second;
        added = vep->AddBusToBusEndpoint(busToBusEndpoint);
    }
    virtualEndpointsLock.Unlock();

    /* Register the endpoint with the router */
    router.RegisterEndpoint(*vep, false);

    if (wasAdded) {
        *wasAdded = added;
    }

    return *vep;
}

void AllJoynObj::RemoveVirtualEndpoint(VirtualEndpoint& vep)
{
    QCC_DbgTrace(("RemoveVirtualEndpoint: %s", vep.GetUniqueName().c_str()));

    /* Remove virtual endpoint along with any aliases that exist for this uniqueName */
    /* Be careful to lock the name table before locking the virtual endpoints since both locks are needed
     * and doing it in the opposite order invites deadlock
     */
    router.LockNameTable();
    virtualEndpointsLock.Lock();
    router.RemoveVirtualAliases(vep);
    router.UnregisterEndpoint(vep);
    virtualEndpoints.erase(vep.GetUniqueName());
    delete &vep;
    virtualEndpointsLock.Unlock();
    router.UnlockNameTable();
}

VirtualEndpoint* AllJoynObj::FindVirtualEndpoint(const qcc::String& uniqueName)
{
    VirtualEndpoint* ret = NULL;
    virtualEndpointsLock.Lock();
    map<qcc::String, VirtualEndpoint*>::iterator it = virtualEndpoints.find(uniqueName);
    if (it != virtualEndpoints.end()) {
        ret = it->second;
    }
    virtualEndpointsLock.Unlock();
    return ret;
}

void AllJoynObj::NameOwnerChanged(const qcc::String& alias, const qcc::String* oldOwner, const qcc::String* newOwner)
{
    QStatus status;
    const String& shortGuidStr = guid.ToShortString();

    /* Validate that there is either a new owner or an old owner */
    const qcc::String* un = oldOwner ? oldOwner : newOwner;
    if (NULL == un) {
        QCC_LogError(ER_BUS_NO_ENDPOINT, ("Invalid NameOwnerChanged without oldOwner or newOwner"));
        return;
    }

    /* Validate format of unique name */
    size_t guidLen = un->find_first_of('.');
    if ((qcc::String::npos == guidLen) || (guidLen < 3)) {
        QCC_LogError(ER_FAIL, ("Invalid unique name \"%s\"", un->c_str()));
    }

    /* Ignore name changes that involve any bus controller endpoint */
    if (0 == ::strcmp(un->c_str() + guidLen, ".1")) {
        return;
    }

    /* Only if local name */
    if (0 == ::strncmp(shortGuidStr.c_str(), un->c_str() + 1, shortGuidStr.size())) {

        /* Send NameChanged to all directly connected controllers */
        b2bEndpointsLock.Lock();
        map<qcc::StringMapKey, RemoteEndpoint*>::iterator it = b2bEndpoints.begin();
        while (it != b2bEndpoints.end()) {
            const qcc::String& un = it->second->GetUniqueName();
            Message sigMsg(bus);
            MsgArg args[3];
            args[0].Set("s", alias.c_str());
            args[1].Set("s", oldOwner ? oldOwner->c_str() : "");
            args[2].Set("s", newOwner ? newOwner->c_str() : "");

            status = sigMsg->SignalMsg("sss",
                                       org::alljoyn::Daemon::WellKnownName,
                                       0,
                                       org::alljoyn::Daemon::ObjectPath,
                                       org::alljoyn::Daemon::InterfaceName,
                                       "NameChanged",
                                       args,
                                       ArraySize(args),
                                       0,
                                       0);
            if (ER_OK == status) {
                status = it->second->PushMessage(sigMsg);
            }
            if (ER_OK != status) {
                QCC_LogError(status, ("Failed to send NameChanged to %s", un.c_str()));
            }
            ++it;
        }
        b2bEndpointsLock.Unlock();


        /* If a local unique name dropped, then remove any refs it had in the connnect, advertise and discover maps */
        if ((NULL == newOwner) && (alias[0] == ':')) {
            /* Remove endpoint refs from connect map */
            qcc::String last;
            router.LockNameTable();
            connectMapLock.Lock();
            multimap<qcc::String, qcc::String>::iterator it = connectMap.begin();
            while (it != connectMap.end()) {
                if (it->second == *oldOwner) {
                    bool isFirstSpec = (last != it->first);
                    qcc::String lastOwner;
                    do {
                        last = it->first;
                        connectMap.erase(it++);
                    } while ((connectMap.end() != it) && (last == it->first) && (*oldOwner == it->second));
                    if (isFirstSpec && ((connectMap.end() == it) || (last != it->first))) {
                        QStatus status = bus.Disconnect(last.c_str());
                        if (ER_OK != status) {
                            QCC_LogError(status, ("Failed to disconnect connect spec %s", last.c_str()));
                        }
                    }
                } else {
                    last = it->first;
                    ++it;
                }
            }
            connectMapLock.Unlock();

            /* Remove endpoint refs from advertise map */
            advertiseMapLock.Lock();
            it = advertiseMap.begin();
            while (it != advertiseMap.end()) {
                if (it->second == *oldOwner) {
                    last = it++->first;
                    QStatus status = ProcCancelAdvertise(*oldOwner, last);
                    if (ER_OK != status) {
                        QCC_LogError(status, ("Failed to cancel advertise for name \"%s\"", last.c_str()));
                    }
                } else {
                    ++it;
                }
            }
            advertiseMapLock.Unlock();

            /* Remove endpoint refs from discover map */
            discoverMapLock.Lock();
            it = discoverMap.begin();
            while (it != discoverMap.end()) {
                if (it->second == *oldOwner) {
                    last = it++->first;
                    QCC_DbgPrintf(("Calling ProcCancelFindName from NameOwnerChanged [%s]", Thread::GetThread()->GetName().c_str()));
                    QStatus status = ProcCancelFindName(*oldOwner, last);
                    if (ER_OK != status) {
                        QCC_LogError(status, ("Failed to cancel discover for name \"%s\"", last.c_str()));
                    }
                } else {
                    ++it;
                }
            }
            discoverMapLock.Unlock();
            router.UnlockNameTable();
        }
    }
}

void AllJoynObj::FoundNames(const qcc::String& busAddr,
                            const qcc::String& guid,
                            const QosInfo& qos,
                            const vector<qcc::String>* names,
                            uint8_t ttl)
{
    QCC_DbgTrace(("AllJoynObj::FoundNames(busAddr = \"%s\", guid = \"%s\", *names = %p, ttl = %d)",
                  busAddr.c_str(), guid.c_str(), names, ttl));

    if (NULL == foundNameSignal) {
        return;
    }
    /* If name is NULL expire all names for the given bus address. */
    if (names == NULL) {
        if (ttl == 0) {
            discoverMapLock.Lock();
            multimap<String, NameMapEntry>::iterator it = nameMap.begin();
            while (it != nameMap.end()) {
                NameMapEntry& nme = it->second;
                if ((nme.guid == guid) && (nme.busAddr == busAddr)) {
                    SendLostAdvertisedName(it->first, nme.qos);
                    nameMap.erase(it++);
                } else {
                    it++;
                }
            }
            discoverMapLock.Unlock();
        }
        return;
    }

    /* Generate a list of name deltas */
    discoverMapLock.Lock();
    vector<String>::const_iterator nit = names->begin();
    while (nit != names->end()) {
        multimap<String, NameMapEntry>::iterator it = nameMap.find(*nit);
        bool isNew = true;
        while ((it != nameMap.end()) && (*nit == it->first)) {
            if ((it->second.guid == guid) && it->second.qos.IsCompatible(qos)) {
                isNew = false;
                break;
            }
            ++it;
        }
        if (0 < ttl) {
            if (isNew) {
                /* Add new name to map */
                nameMap.insert(pair<String, NameMapEntry>(*nit, NameMapEntry(busAddr, guid, qos, (1000 * ttl))));

                /* Send FoundAdvertisedName to anyone who is discovering *nit */
                if (0 < discoverMap.size()) {
                    multimap<String, String>::const_iterator dit = discoverMap.lower_bound(*nit);
                    if ((dit == discoverMap.end()) || (0 > ::strcmp(nit->c_str(), dit->first.c_str()))) {
                        --dit;
                    }
                    while (true) {
                        bool match = false;
                        if (0 == ::strncmp(dit->first.c_str(), nit->c_str(), dit->first.size())) {
                            match = true;
                            QStatus status = SendFoundAdvertisedName(dit->second, *nit, qos, dit->first);
                            if (ER_OK != status) {
                                QCC_LogError(status, ("Failed to send FoundAdvertisedName to %s (name=%s)", dit->second.c_str(), nit->c_str()));
                            }
                        }
                        if (!match || (dit == discoverMap.begin())) {
                            break;
                        }
                        --dit;
                    }
                }
            } else {
                /*
                 * If the busAddr doesn't match, then this is actually a new but redundant advertsement.
                 * Don't track it. Don't updated the TTL for the existing advertisment with the same name
                 * and don't tell clients about this alternate way to connect to the name
                 * since it will look like a duplicate to the client (that doesn't receive busAddr).
                 */
                if (busAddr == it->second.busAddr) {
                    it->second.timestamp = GetTimestamp();
                }
            }
            nameMapReaper.Alert();
        } else {
            /* 0 == ttl means flush the record */
            if (!isNew) {
                SendLostAdvertisedName(it->first, it->second.qos);
                nameMap.erase(it);
            }
        }
        ++nit;
    }
    discoverMapLock.Unlock();
}

QStatus AllJoynObj::SendFoundAdvertisedName(const String& dest,
                                            const String& name,
                                            const QosInfo& qos,
                                            const String& namePrefix)
{
    MsgArg args[3];
    args[0].Set("s", name.c_str());
    args[1].Set("(qqq)", qos.proximity, qos.traffic, qos.transports);
    args[2].Set("s", namePrefix.c_str());
    return Signal(dest.c_str(), 0, *foundNameSignal, args, ArraySize(args));
}

QStatus AllJoynObj::SendLostAdvertisedName(const String& name,
                                           const QosInfo& qos)
{
    QCC_DbgTrace(("AllJoynObj::SendLostAdvertisdName(%s, <%x, %x, %x>)", name.c_str(), qos.proximity, qos.traffic, qos.transports));

    QStatus status = ER_OK;

    /* Send LostAdvertisedName to anyone who is discovering name */
    discoverMapLock.Lock();
    if (0 < discoverMap.size()) {
        multimap<String, String>::const_iterator dit = discoverMap.lower_bound(name);
        if ((dit == discoverMap.end()) || (0 > ::strcmp(name.c_str(), dit->first.c_str()))) {
            --dit;
        }
        while (true) {
            bool match = false;
            if (0 == ::strncmp(dit->first.c_str(), name.c_str(), dit->first.size())) {
                MsgArg args[3];
                args[0].Set("s", name.c_str());
                args[1].Set("(qqq)", qos.proximity, qos.traffic, qos.transports);
                args[2].Set("s", dit->first.c_str());
                match = true;
                QCC_DbgPrintf(("Sending LostAdvertisedName(%s, <>, %s) to %s", name.c_str(), dit->first.c_str(), dit->second.c_str()));
                QStatus tStatus = Signal(dit->second.c_str(), 0, *lostAdvNameSignal, args, ArraySize(args));
                if (ER_OK != tStatus) {
                    status = (ER_OK == status) ? tStatus : status;
                    QCC_LogError(tStatus, ("Failed to send LostAdvertisedName to %s (name=%s)", dit->second.c_str(), name.c_str()));
                }
            }
            if (!match || (dit == discoverMap.begin())) {
                break;
            }
            --dit;
        }
    }
    discoverMapLock.Unlock();
    return status;
}

ThreadReturn STDCALL AllJoynObj::NameMapReaperThread::Run(void* arg)
{
    uint32_t waitTime(Event::WAIT_FOREVER);
    Event evt(waitTime);
    while (!IsStopping()) {
        ajnObj->discoverMapLock.Lock();
        set<qcc::String> expiredBuses;
        multimap<String, NameMapEntry>::iterator it = ajnObj->nameMap.begin();
        uint32_t now = GetTimestamp();
        waitTime = Event::WAIT_FOREVER;
        while (it != ajnObj->nameMap.end()) {
            if ((now - it->second.timestamp) >= it->second.ttl) {
                QCC_DbgPrintf(("Expiring discovered name %s for guid %s", it->first.c_str(), it->second.guid.c_str()));
                expiredBuses.insert(it->second.busAddr);
                ajnObj->SendLostAdvertisedName(it->first, it->second.qos);
                ajnObj->nameMap.erase(it++);
            } else {
                uint32_t nextTime(it->second.ttl - (now - it->second.timestamp));
                if (nextTime < waitTime) {
                    waitTime = nextTime;
                }
                ++it;
            }
        }
        ajnObj->discoverMapLock.Unlock();

        while (expiredBuses.begin() != expiredBuses.end()) {
            expiredBuses.erase(expiredBuses.begin());
        }

        evt.ResetTime(waitTime, 0);
        QStatus status = Event::Wait(evt);
        if (status == ER_ALERTED_THREAD) {
            stopEvent.ResetEvent();
        }
    }
    return 0;
}

void AllJoynObj::BusConnectionLost(const qcc::String& busAddr)
{
    /* Clear the connection map of this busAddress */
    connectMapLock.Lock();
    bool foundName = false;
    multimap<String, String>::iterator it = connectMap.lower_bound(busAddr);
    while ((it != connectMap.end()) && (0 == busAddr.compare(it->first))) {
        foundName = true;
        connectMap.erase(it++);
    }
    connectMapLock.Unlock();

    /* Send a signal to interested local clients */
    if (foundName && busConnLostSignal) {
        MsgArg arg;
        arg.Set("s", busAddr.c_str());
        QStatus status = Signal(NULL, 0, *busConnLostSignal, &arg, 1);
        if (ER_OK != status) {
            QCC_LogError(status, ("Failed to send BusConnectionLost signal"));
        }
    }
}

}
