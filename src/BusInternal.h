#ifndef _ALLJOYN_INTERNAL_H
#define _ALLJOYN_INTERNAL_H
/**
 * @file
 *
 * This file defines internal state for a BusAttachment
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

#ifndef __cplusplus
#error Only include BusInternal.h in C++ code.
#endif

#include <qcc/platform.h>
#include <qcc/String.h>
#include <qcc/Event.h>
#include <qcc/atomic.h>

#include <alljoyn/BusAttachment.h>
#include <alljoyn/InterfaceDescription.h>
#include <alljoyn/BusListener.h>

#include "AuthMechanism.h"
#include "ClientRouter.h"
#include "KeyStore.h"
#include "PeerState.h"
#include "Transport.h"
#include "TransportList.h"
#include "CompressionRules.h"

#include <Status.h>

namespace ajn {

class BusAttachment::Internal : public MessageReceiver, public qcc::AlarmListener, public qcc::ThreadListener {
    friend class BusAttachment;

  public:

    /**
     * Get a reference to the internal store object.
     *
     * @return A reference to the bus's key store.
     */
    KeyStore& GetKeyStore() { return keyStore; }

    /**
     * Return the next available serial number. Note 0 is an invalid serial number.
     *
     * @return   The next available serial number.
     */
    uint32_t NextSerial() {
        uint32_t sn = (uint32_t) qcc::IncrementAndFetch(&msgSerial);
        return sn ? sn : NextSerial();
    }

    /**
     * Get a reference to the authentication manager object.
     *
     * @return A pointer to the bus's authentication manager
     */
    AuthManager& GetAuthManager() { return authManager; }

    /**
     * Get a reference to the internal transport list.
     *
     * @return A reference to the bus's transport list.
     */
    TransportList& GetTransportList() { return transportList; }

    /**
     * Get a pointer to the internal peer state table.
     *
     * @return  The peer state table.
     */
    PeerStateTable* GetPeerStateTable() { return &peerStateTable; }

    /**
     * Get the global GUID for this bus.
     *
     * @return Global GUID
     */
    const qcc::GUID& GetGlobalGUID(void) const { return globalGuid; }

    /**
     * Return the local endpoint for this bus.
     *
     * @return  Returns the local endpoint.
     */
    LocalEndpoint& GetLocalEndpoint() { return localEndpoint; }

    /**
     * Get the router.
     *
     * @return  The router
     */
    Router& GetRouter(void) { return *router; }

    /**
     * Get the router.
     *
     * @return  The router
     */
    const Router& GetRouter(void) const { return *router; }

    /**
     * Get the header compression rules
     *
     * @return The header compression rules.
     */
    CompressionRules& GetCompressionRules() { return compressionRules; };

    /**
     * Get the shared timer.
     */
    qcc::Timer& GetTimer() { return timer; }

    /**
     * Get the dispatcher
     */
    qcc::Timer& GetDispatcher() { return dispatcher; }

    /**
     * Constructor called by BusAttachment.
     */
    Internal(const char* appName,
             BusAttachment& bus,
             TransportFactoryContainer& factories,
             Router* router,
             bool allowRemoteMessages,
             const char* listenAddresses);

    /*
     * Destructor also called by BusAttachment
     */
    ~Internal();

    /**
     * Filter out authentication mechanisms not present in the list.
     *
     * @param list  The list of authentication mechanisms to filter on.
     */
    size_t FilterAuthMechanisms(const qcc::String& list) { return authManager.FilterMechanisms(list); }

    /**
     * A generic signal handler for AllJoyn signals
     */
    void AllJoynSignalHandler(const InterfaceDescription::Member* member, const char* srcPath, Message& message);

    /**
     * Function called when an alarm is triggered.
     */
    void AlarmTriggered(const qcc::Alarm& alarm, QStatus reason);

    /**
     * Indicate whether endpoints of this attachment are allowed to receive messages
     * from remote devices.
     */
    bool AllowRemoteMessages() const { return allowRemoteMessages; }

    /**
     * Get the bus addresses that this daemon uses to listen on.
     * For clients, this list is empty since clients dont listen.
     *
     * @return The bus addresses that this bus instance uses to listen on.
     */
    const qcc::String& GetListenAddresses() const { return listenAddresses; }

    /**
     * Inform BusListeners of incoming JoinSession attempt.
     *
     * @param sessionPort    SessionPort specified in join request.
     * @param joiner         Unique name of potential joiner.
     * @param opts           Session options requested by joiner.
     * @return   Return true if JoinSession request is accepted. false if rejected.
     */
    bool CallAcceptListeners(SessionPort sessionPort, const char* joiner, const SessionOpts& opts);

    /**
     * Inform BusListeners of a successful JoinSession.
     *
     * @param sessionPort    SessionPort specified by joiner.
     * @param id             Session id.
     * @param joiner         Unique name of sucessful joiner.
     */
    void CallJoinedListeners(SessionPort sessionPort, SessionId id, const char* joiner);

    /**
     * This function puts a message on the dispatch thread and deliver it to the specified alarm listener.
     *
     * @param listener  The alarm listener to receive the message.
     * @param msg       The message to queue
     * @param delay     Time to delay before delivering the message.
     */
    QStatus DispatchMessage(AlarmListener& listener, Message& msg, uint32_t delay = 0);

  private:

    void ThreadExit(qcc::Thread* thread);

    qcc::String application;              /* Name of the that owns the BusAttachment application */
    BusAttachment& bus;                   /* Reference back to the bus attachment that owns this state */
    qcc::Mutex listenersLock;             /* Mutex that protects BusListeners vector */
    std::list<BusListener*> listeners;     /* List of registered BusListeners */
    TransportList transportList;          /* List of active transports */
    KeyStore keyStore;                    /* The key store for the bus attachment */
    AuthManager authManager;              /* The authentication manager for the bus attachment */
    qcc::GUID globalGuid;                 /* Global GUID for this BusAttachment */
    int32_t msgSerial;                    /* Serial number is updated for every message sent by this bus */
    Router* router;                       /* Message bus router */
    PeerStateTable peerStateTable;        /* Table that maintains state information about remote peers */
    LocalEndpoint& localEndpoint;         /* The local endpoint */
    CompressionRules compressionRules;    /* Rules for compresssing headers */
    std::map<qcc::StringMapKey, InterfaceDescription> ifaceDescriptions;

    qcc::Timer timer;                     /* Timer used for various timeouts such as method replies */
    qcc::Timer dispatcher;                /* Dispatcher used for org.alljoyn.Bus signal triggered bus listener callbacks */
    bool allowRemoteMessages;             /* true iff endpoints of this attachment can receive messages from remote devices */
    qcc::String listenAddresses;          /* The set of bus addresses that this bus can listen on. (empty for clients) */
    qcc::Mutex stopLock;                  /* Protects BusAttachement::Stop from being reentered */
    int32_t stopCount;                    /* Number of caller's blocked in BusAttachment::Stop() */
};


}

#endif
