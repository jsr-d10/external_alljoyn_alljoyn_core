/**
 * @file
 * LocalTransport is a special type of Transport that is responsible
 * for all communication of all endpoints that terminate at registered
 * AllJoynObjects residing within this BusAttachment instance.
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

#include <list>

#include <qcc/Debug.h>
#include <qcc/GUID.h>
#include <qcc/String.h>
#include <qcc/StringUtil.h>
#include <qcc/Thread.h>
#include <qcc/atomic.h>

#include <alljoyn/DBusStd.h>
#include <alljoyn/AllJoynStd.h>
#include <alljoyn/Message.h>
#include <alljoyn/BusObject.h>
#include <alljoyn/ProxyBusObject.h>

#include "LocalTransport.h"
#include "Router.h"
#include "MethodTable.h"
#include "SignalTable.h"
#include "CompressionRules.h"
#include "AllJoynPeerObj.h"
#include "BusUtil.h"
#include "BusInternal.h"


#define QCC_MODULE "ALLJOYN"

using namespace std;
using namespace qcc;

namespace ajn {

LocalTransport::~LocalTransport()
{
    Stop();
    Join();
}

QStatus LocalTransport::Start()
{
    isStoppedEvent.ResetEvent();
    return localEndpoint.Start();
}

QStatus LocalTransport::Stop()
{
    QStatus status = localEndpoint.Stop();
    isStoppedEvent.SetEvent();
    return status;
}

QStatus LocalTransport::Join()
{
    QStatus status = localEndpoint.Join();
    /* Pend caller until transport is stopped */
    Event::Wait(isStoppedEvent);
    return status;
}

bool LocalTransport::IsRunning()
{
    return !isStoppedEvent.IsSet();
}

LocalEndpoint::LocalEndpoint(BusAttachment& bus) :
    BusEndpoint(BusEndpoint::ENDPOINT_TYPE_LOCAL),
    running(false),
    refCount(1),
    bus(bus),
    objectsLock(),
    replyMapLock(),
    dbusObj(NULL),
    alljoynObj(NULL),
    peerObj(NULL)
{
}

LocalEndpoint::~LocalEndpoint()
{
    QCC_DbgHLPrintf(("LocalEndpoint~LocalEndpoint"));

    running = false;

    assert(refCount > 0);
    /*
     * We can't complete the destruction if we have calls out the application.
     */
    if (DecrementAndFetch(&refCount) != 0) {
        while (refCount) {
            qcc::Sleep(1);
        }
    }
    if (dbusObj) {
        delete dbusObj;
        dbusObj = NULL;
    }
    if (alljoynObj) {
        delete alljoynObj;
        alljoynObj = NULL;
    }
    if (peerObj) {
        delete peerObj;
        peerObj = NULL;
    }
}

QStatus LocalEndpoint::Start()
{
    QStatus status = ER_OK;

    /* Set the local endpoint's unique name */
    SetUniqueName(bus.GetInternal().GetRouter().GenerateUniqueName());

    if (!dbusObj) {
        /* Register well known org.freedesktop.DBus remote object */
        const InterfaceDescription* intf = bus.GetInterface(org::freedesktop::DBus::InterfaceName);
        if (intf) {
            dbusObj = new ProxyBusObject(bus, org::freedesktop::DBus::WellKnownName, org::freedesktop::DBus::ObjectPath, 0);
            dbusObj->AddInterface(*intf);
        } else {
            status = ER_BUS_NO_SUCH_INTERFACE;
        }
    }

    if (!alljoynObj && (ER_OK == status)) {
        /* Register well known org.alljoyn.Bus remote object */
        const InterfaceDescription* mintf = bus.GetInterface(org::alljoyn::Bus::InterfaceName);
        if (mintf) {
            alljoynObj = new ProxyBusObject(bus, org::alljoyn::Bus::WellKnownName, org::alljoyn::Bus::ObjectPath, 0);
            alljoynObj->AddInterface(*mintf);
        } else {
            status = ER_BUS_NO_SUCH_INTERFACE;
        }
    }

    /* Initialize the peer object */
    if (!peerObj && (ER_OK == status)) {
        peerObj = new AllJoynPeerObj(bus);
        status = peerObj->Init();
    }

    /* Start the peer object */
    if (peerObj && (ER_OK == status)) {
        status = peerObj->Start();
    }

    /* Local endpoint is up and running, register with router */
    if (ER_OK == status) {
        running = true;
        bus.GetInternal().GetRouter().RegisterEndpoint(*this, true);
    }

    return status;
}

QStatus LocalEndpoint::Stop(void)
{
    QCC_DbgTrace(("LocalEndpoint::Stop"));

    /* Local endpoint not longer running */
    running = false;

    IncrementAndFetch(&refCount);
    /*
     * Deregister all registered bus objects
     */
    objectsLock.Lock();
    hash_map<const char*, BusObject*, hash<const char*>, PathEq>::iterator it = localObjects.begin();
    while (it != localObjects.end()) {
        BusObject* obj = it->second;
        objectsLock.Unlock();
        DeregisterBusObject(*obj);
        objectsLock.Lock();
        it = localObjects.begin();
    }
    if (peerObj) {
        peerObj->Stop();
    }
    objectsLock.Unlock();
    DecrementAndFetch(&refCount);

    return ER_OK;
}

QStatus LocalEndpoint::Join(void)
{
    if (peerObj) {
        peerObj->Join();
    }
    return ER_OK;
}

QStatus LocalEndpoint::Diagnose(Message& message)
{
    QStatus status;
    BusObject* obj = FindLocalObject(message->GetObjectPath());

    /*
     * Try to figure out what went wrong
     */
    if (obj == NULL) {
        status = ER_BUS_NO_SUCH_OBJECT;
        QCC_LogError(status, ("No such object %s", message->GetObjectPath()));
    } else if (!obj->ImplementsInterface(message->GetInterface())) {
        status = ER_BUS_OBJECT_NO_SUCH_INTERFACE;
        QCC_LogError(status, ("Object %s has no interface %s (member=%s)", message->GetObjectPath(), message->GetInterface(), message->GetMemberName()));
    } else {
        status = ER_BUS_OBJECT_NO_SUCH_MEMBER;
        QCC_LogError(status, ("Object %s has no member %s", message->GetObjectPath(), message->GetMemberName()));
    }
    return status;
}

QStatus LocalEndpoint::PeerInterface(Message& message)
{
    if (strcmp(message->GetMemberName(), "Ping") == 0) {
        QStatus status = message->UnmarshalArgs("", "");
        if (ER_OK != status) {
            return status;
        }
        message->ReplyMsg(NULL, 0);
        return bus.GetInternal().GetRouter().PushMessage(message, *this);
    }
    if (strcmp(message->GetMemberName(), "GetMachineId") == 0) {
        QStatus status = message->UnmarshalArgs("", "s");
        if (ER_OK != status) {
            return status;
        }
        MsgArg replyArg(ALLJOYN_STRING);
        // @@TODO Need OS specific support for returning a machine id GUID use the bus id for now
        qcc::String guidStr = bus.GetInternal().GetGlobalGUID().ToString();
        replyArg.v_string.str = guidStr.c_str();
        replyArg.v_string.len = guidStr.size();
        message->ReplyMsg(&replyArg, 1);
        return bus.GetInternal().GetRouter().PushMessage(message, *this);
    }
    return ER_BUS_OBJECT_NO_SUCH_MEMBER;
}

QStatus LocalEndpoint::PushMessage(Message& message)
{
    QStatus status = ER_OK;

    if (!running) {
        status = ER_BUS_STOPPING;
        QCC_LogError(status, ("Local transport not running discarding %s", message->Description().c_str()));
    } else {
        if (IncrementAndFetch(&refCount) > 1) {
            Thread* thread = Thread::GetThread();

            QCC_DbgPrintf(("Pushing %s into local endpoint", message->Description().c_str()));

            /* Cannot allow synchronous method calls on this thread. */
            thread->SetNoBlock(&bus);

            switch (message->GetType()) {
            case MESSAGE_METHOD_CALL:
                status = HandleMethodCall(message);
                break;

            case MESSAGE_SIGNAL:
                status = HandleSignal(message);
                break;

            case MESSAGE_METHOD_RET:
            case MESSAGE_ERROR:
                status = HandleMethodReply(message);
                break;

            default:
                status = ER_FAIL;
                break;
            }

            thread->SetNoBlock(NULL);
        }
        DecrementAndFetch(&refCount);
    }
    return status;
}

QStatus LocalEndpoint::RegisterBusObject(BusObject& object)
{
    QStatus status = ER_OK;

    const char* objPath = object.GetPath();

    QCC_DbgPrintf(("RegisterObject %s", objPath));

    if (!IsLegalObjectPath(objPath)) {
        status = ER_BUS_BAD_OBJ_PATH;
        QCC_LogError(status, ("Illegal object path \"%s\" specified", objPath));
        return status;
    }

    objectsLock.Lock();

    /* Register placeholder parents as needed */
    size_t off = 0;
    qcc::String pathStr(objPath);
    BusObject* lastParent = NULL;
    if (1 < pathStr.size()) {
        while (qcc::String::npos != (off = pathStr.find_first_of('/', off))) {
            qcc::String parentPath = pathStr.substr(0, max((size_t)1, off));
            off++;
            BusObject* parent = FindLocalObject(parentPath.c_str());
            if (!parent) {
                parent = new BusObject(bus, parentPath.c_str(), true);
                QStatus status = DoRegisterBusObject(*parent, lastParent, true);
                if (ER_OK != status) {
                    delete parent;
                    QCC_LogError(status, ("Failed to register default object for path %s", parentPath.c_str()));
                    break;
                }
                defaultObjects.push_back(parent);
            }
            lastParent = parent;
        }
    }

    /* Now register the object itself */
    if (ER_OK == status) {
        status = DoRegisterBusObject(object, lastParent, false);
    }

    objectsLock.Unlock();

    return status;
}

QStatus LocalEndpoint::DoRegisterBusObject(BusObject& object, BusObject* parent, bool isPlaceholder)
{
    QCC_DbgPrintf(("RegisterBusObject %s", object.GetPath()));
    const char* objPath = object.GetPath();

    /* objectsLock is already obtained */

    /* If an object with this path already exists, replace it */
    BusObject* existingObj = FindLocalObject(objPath);
    if (NULL != existingObj) {
        existingObj->Replace(object);
        DeregisterBusObject(*existingObj);
    }

    /* Register object. */
    QStatus status = object.DoRegistration();
    if (ER_OK == status) {
        /* Link new object to its parent */
        if (parent) {
            parent->AddChild(object);
        }
        /* Add object to list of objects */
        localObjects[object.GetPath()] = &object;

        /* Register handler for the object's methods */
        methodTable.AddAll(&object);

        /* Notify object of registration. Defer if we are not connected yet. */
        if (bus.GetInternal().GetRouter().IsBusRunning()) {
            BusIsConnected();
        }
    }

    return status;
}

void LocalEndpoint::DeregisterBusObject(BusObject& object)
{
    QCC_DbgPrintf(("DeregisterBusObject %s", object.GetPath()));

    /* Remove members */
    methodTable.RemoveAll(&object);

    /* Remove from object list */
    objectsLock.Lock();
    localObjects.erase(object.GetPath());
    objectsLock.Unlock();

    /* Notify object and detach from bus*/
    object.ObjectDeregistered();

    /* Detach object from parent */
    objectsLock.Lock();
    if (NULL != object.parent) {
        object.parent->RemoveChild(object);
    }

    /* If object has children, deregister them as well */
    while (true) {
        BusObject* child = object.RemoveChild();
        if (!child) {
            break;
        }
        DeregisterBusObject(*child);
    }
    /* Delete the object if it was a default object */
    vector<BusObject*>::iterator dit = defaultObjects.begin();
    while (dit != defaultObjects.end()) {
        if (*dit == &object) {
            defaultObjects.erase(dit);
            delete &object;
            break;
        } else {
            ++dit;
        }
    }
    objectsLock.Unlock();
}

BusObject* LocalEndpoint::FindLocalObject(const char* objectPath) {
    objectsLock.Lock();
    hash_map<const char*, BusObject*, hash<const char*>, PathEq>::iterator iter = localObjects.find(objectPath);
    BusObject* ret = (iter == localObjects.end()) ? NULL : iter->second;
    objectsLock.Unlock();
    return ret;
}

QStatus LocalEndpoint::RegisterReplyHandler(MessageReceiver* receiver,
                                            MessageReceiver::ReplyHandler replyHandler,
                                            const InterfaceDescription::Member& method,
                                            uint32_t serial,
                                            bool secure,
                                            void* context,
                                            uint32_t timeout)
{
    QStatus status = ER_OK;
    if (!running) {
        status = ER_BUS_STOPPING;
        QCC_LogError(status, ("Local transport not running"));
    } else {
        ReplyContext reply = {
            receiver,
            replyHandler,
            &method,
            secure,
            context,
            Alarm(timeout, this, 0, (void*)serial)
        };
        QCC_DbgPrintf(("LocalEndpoint::RegisterReplyHandler - Adding serial=%u", serial));
        replyMapLock.Lock();
        replyMap.insert(pair<uint32_t, ReplyContext>(serial, reply));
        replyMapLock.Unlock();

        /* Set a timeout */
        status = bus.GetInternal().GetTimer().AddAlarm(reply.alarm);
        if (status != ER_OK) {
            UnRegisterReplyHandler(serial);
        }
    }
    return status;
}

void LocalEndpoint::UnRegisterReplyHandler(uint32_t serial)
{
    replyMapLock.Lock();
    map<uint32_t, ReplyContext>::iterator iter = replyMap.find(serial);
    if (iter != replyMap.end()) {
        QCC_DbgPrintf(("LocalEndpoint::UnRegisterReplyHandler - Removing serial=%u", serial));
        ReplyContext rc = iter->second;
        replyMap.erase(iter);
        replyMapLock.Unlock();
        bus.GetInternal().GetTimer().RemoveAlarm(rc.alarm);
    } else {
        replyMapLock.Unlock();
    }
}

QStatus LocalEndpoint::RegisterSignalHandler(MessageReceiver* receiver,
                                             MessageReceiver::SignalHandler signalHandler,
                                             const InterfaceDescription::Member* member,
                                             const char* srcPath)
{
    if (!receiver) {
        return ER_BAD_ARG_1;
    }
    if (!signalHandler) {
        return ER_BAD_ARG_2;
    }
    if (!member) {
        return ER_BAD_ARG_3;
    }
    signalTable.Add(receiver, signalHandler, member, srcPath ? srcPath : "");
    return ER_OK;
}

QStatus LocalEndpoint::UnRegisterSignalHandler(MessageReceiver* receiver,
                                               MessageReceiver::SignalHandler signalHandler,
                                               const InterfaceDescription::Member* member,
                                               const char* srcPath)
{
    if (!receiver) {
        return ER_BAD_ARG_1;
    }
    if (!signalHandler) {
        return ER_BAD_ARG_2;
    }
    if (!member) {
        return ER_BAD_ARG_3;
    }
    signalTable.Remove(receiver, signalHandler, member, srcPath ? srcPath : "");
    return ER_OK;
}

void LocalEndpoint::AlarmTriggered(const Alarm& alarm, QStatus reason)
{
    /*
     * Alarms are used for two unrelated purposes within LocalEnpoint:
     *
     * When context is non-NULL, the alarm indicates that a method call
     * with serial == *context has timed out.
     *
     * When context is NULL, the alarm indicates that the BusAttachment that this
     * LocalEndpoint is a part of is connected to a daemon and any previously
     * unregistered BusObjects should be registered
     */
    if (NULL != alarm.GetContext()) {
        uint32_t serial = reinterpret_cast<uintptr_t>(alarm.GetContext());
        Message msg(bus);
        QCC_DbgPrintf(("Timed out waiting for METHOD_REPLY with serial %d", serial));

        if (reason == ER_TIMER_EXITING) {
            msg->ErrorMsg("org.alljoyn.Bus.Exiting", serial);
        } else {
            msg->ErrorMsg("org.alljoyn.Bus.Timeout", serial);
        }
        HandleMethodReply(msg);
    } else {
        /* Call ObjectRegistered for any unregistered bus object */
        objectsLock.Lock();
        hash_map<const char*, BusObject*, hash<const char*>, PathEq>::iterator iter = localObjects.begin();
        while (iter != localObjects.end()) {
            if (!iter->second->isRegistered) {
                BusObject* bo = iter->second;
                bo->isRegistered = true;
                objectsLock.Unlock();
                bo->ObjectRegistered();
                objectsLock.Lock();
                iter = localObjects.begin();
            } else {
                ++iter;
            }
        }
        objectsLock.Unlock();

        /* Decrement refcount to indicate we are done calling out */
        DecrementAndFetch(&refCount);
    }
}

QStatus LocalEndpoint::HandleMethodCall(Message& message)
{
    QStatus status = ER_OK;

    /* Look up the member */
    const MethodTable::Entry* entry = methodTable.Find(message->GetObjectPath(),
                                                       message->GetInterface(),
                                                       message->GetMemberName());
    if (entry == NULL) {
        if (strcmp(message->GetInterface(), org::freedesktop::DBus::Peer::InterfaceName) == 0) {
            /*
             * Special case the Peer interface
             */
            status = PeerInterface(message);
        } else {
            /*
             * Figure out what error to report
             */
            status = Diagnose(message);
        }
    } else if (entry->member->iface->IsSecure() && !message->IsEncrypted()) {
        status = ER_BUS_MESSAGE_NOT_ENCRYPTED;
        QCC_LogError(status, ("Method call to secure interface was not encrypted"));
    } else {
        status = message->UnmarshalArgs(entry->member->signature, entry->member->returnSignature.c_str());
    }
    if (status == ER_OK) {
        /* Call the method handler */
        if (entry) {
            (entry->object->*entry->handler)(entry->member, message);
        }
    } else if (message->GetType() == MESSAGE_METHOD_CALL && !(message->GetFlags() & ALLJOYN_FLAG_NO_REPLY_EXPECTED)) {
        /* We are rejecting a method call that expects a response so reply with an error message. */
        qcc::String errStr;
        qcc::String errMsg;
        switch (status) {
        case ER_BUS_MESSAGE_NOT_ENCRYPTED:
            errStr = "org.alljoyn.Bus.SecurityViolation";
            errMsg = "Expected secure method call";
            peerObj->HandleSecurityViolation(message, status);
            status = ER_OK;
            break;

        case ER_BUS_MESSAGE_DECRYPTION_FAILED:
            errStr = "org.alljoyn.Bus.SecurityViolation";
            errMsg = "Unable to authenticate method call";
            peerObj->HandleSecurityViolation(message, status);
            status = ER_OK;
            break;

        case ER_BUS_NO_SUCH_OBJECT:
            errStr = "org.freedesktop.DBus.Error.ServiceUnknown";
            errMsg = QCC_StatusText(status);
            break;

        default:
            errStr += "org.alljoyn.Bus.";
            errStr += QCC_StatusText(status);
            errMsg = message->Description();
            break;
        }
        message->ErrorMsg(errStr.c_str(), errMsg.c_str());
        status = bus.GetInternal().GetRouter().PushMessage(message, *this);
    } else {
        QCC_LogError(status, ("Ignoring message %s", message->Description().c_str()));
        status = ER_OK;
    }
    return status;
}

QStatus LocalEndpoint::HandleSignal(Message& message)
{
    QStatus status = ER_OK;

    signalTable.Lock();

    /* Look up the signal */
    pair<SignalTable::const_iterator, SignalTable::const_iterator> range =
        signalTable.Find(message->GetObjectPath(), message->GetInterface(), message->GetMemberName());

    /*
     * Quick exit if there are no handlers for this signal
     */
    if (range.first == range.second) {
        signalTable.Unlock();
        return ER_OK;
    }
    /*
     * Build a list of all signal handlers for this signal
     */
    list<SignalTable::Entry> callList;
    const InterfaceDescription::Member* signal = range.first->second.member;
    do {
        callList.push_back(range.first->second);
    } while (++range.first != range.second);
    /*
     * We have our callback list so we can unlock the signal table.
     */
    signalTable.Unlock();
    /*
     * Validate and unmarshal the signal
     */
    if (signal->iface->IsSecure() && !message->IsEncrypted()) {
        status = ER_BUS_MESSAGE_NOT_ENCRYPTED;
        QCC_LogError(status, ("Signal from secure interface was not encrypted"));
    } else {
        status = message->UnmarshalArgs(signal->signature);
    }
    if (status != ER_OK) {
        if ((status == ER_BUS_MESSAGE_DECRYPTION_FAILED) || (status == ER_BUS_MESSAGE_NOT_ENCRYPTED)) {
            peerObj->HandleSecurityViolation(message, status);
            status = ER_OK;
        }
    } else {
        list<SignalTable::Entry>::const_iterator callit;
        for (callit = callList.begin(); callit != callList.end(); ++callit) {
            (callit->object->*callit->handler)(callit->member, message->GetObjectPath(), message);
        }
    }
    return status;
}

QStatus LocalEndpoint::HandleMethodReply(Message& message)
{
    QStatus status = ER_OK;

    replyMapLock.Lock();
    map<uint32_t, ReplyContext>::iterator iter = replyMap.find(message->GetReplySerial());
    if (iter != replyMap.end()) {
        ReplyContext rc = iter->second;
        replyMap.erase(iter);
        replyMapLock.Unlock();
        bus.GetInternal().GetTimer().RemoveAlarm(rc.alarm);
        if (rc.secure && !message->IsEncrypted()) {
            /*
             * If the reply was not encrypted but should have been return an error to the caller.
             */
            status = ER_BUS_MESSAGE_NOT_ENCRYPTED;
        } else {
            QCC_DbgPrintf(("Matched reply for serial #%d", message->GetReplySerial()));
            if (message->GetType() == MESSAGE_METHOD_RET) {
                status = message->UnmarshalArgs(rc.method->returnSignature);
            } else {
                status = message->UnmarshalArgs("*");
            }
        }
        if (status != ER_OK) {
            switch (status) {
            case ER_BUS_MESSAGE_DECRYPTION_FAILED:
            case ER_BUS_MESSAGE_NOT_ENCRYPTED:
                message->ErrorMsg(status, message->GetReplySerial());
                peerObj->HandleSecurityViolation(message, status);
                break;

            default:
                message->ErrorMsg(status, message->GetReplySerial());
                break;
            }
            QCC_LogError(status, ("Reply message replaced with an internally generated error"));
            status = ER_OK;
        }
        ((rc.object)->*(rc.handler))(message, rc.context);
    } else {
        replyMapLock.Unlock();
        status = ER_BUS_UNMATCHED_REPLY_SERIAL;
        QCC_LogError(status, ("%s does not match any current method calls", message->Description().c_str()));
    }
    return status;
}

void LocalEndpoint::BusIsConnected()
{
    if (IncrementAndFetch(&refCount) > 1) {
        /* Call ObjectRegistered callbacks on another thread */
        bus.GetInternal().GetTimer().AddAlarm(Alarm(0, this));
    }
}

}
