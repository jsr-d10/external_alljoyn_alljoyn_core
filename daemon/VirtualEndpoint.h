/**
 * @file
 * A VirtualEndpoint is a representation of an AllJoyn endpoint that exists behind a remote
 * AllJoyn daemon.
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
#ifndef _ALLJOYN_VIRTUALENDPOINT_H
#define _ALLJOYN_VIRTUALENDPOINT_H

#include <qcc/platform.h>

#include <map>

#include "RemoteEndpoint.h"

#include <qcc/String.h>
#include <alljoyn/Message.h>

#include <Status.h>

namespace ajn {

/**
 * %VirtualEndpoint is an alias for a remote bus connection that exists
 * behind a remote AllJoyn daemon.
 */
class VirtualEndpoint : public BusEndpoint {
  public:
    /**
     * Constructor
     *
     * @param uniqueName      Unique name for this endpoint.
     * @param b2bEp           Initial Bus-to-bus endpoint for this virtual endpoint.
     */
    VirtualEndpoint(const char* uniqueName, RemoteEndpoint& b2bEp);

    /**
     * Send an outgoing message.
     *
     * @param msg   Message to be sent.
     * @return
     *      - ER_OK if successful.
     *      - An error status otherwise
     */
    QStatus PushMessage(Message& msg);

    /**
     * Send an outgoing message over a specific session.
     *
     * @param msg   Message to be sent.
     * @param id    SessionId to use for outgoing message.
     * @return
     *      - ER_OK if successful.
     *      - An error status otherwise
     */
    QStatus PushMessage(Message& msg, SessionId id);

    /**
     * Get unique bus name.
     *
     * @return
     *      - unique bus name
     *      - empty if server has not yet assigned one (client-side).
     */
    const qcc::String& GetUniqueName() const { return m_uniqueName; }

    /**
     * Return the user id of the endpoint.
     *
     * @return  User ID number.
     */
    uint32_t GetUserId() const { return 0; };

    /**
     * Return the group id of the endpoint.
     *
     * @return  Group ID number.
     */
    uint32_t GetGroupId() const { return 0; }

    /**
     * Return the process id of the endpoint.
     *
     * @return  Process ID number.
     */
    uint32_t GetProcessId() const { return 0; }

    /**
     * Indicates if the endpoint supports reporting UNIX style user, group, and process IDs.
     *
     * @return  'true' if UNIX IDs supported, 'false' if not supported.
     */
    bool SupportsUnixIDs() const { return false; }

    /**
     * Get the BusToBus endpoint associated with this virtual endpoint.
     *
     * @param sessionId   Id of session between src and dest.
     * @return The current (top of queue) bus-to-bus endpoint.
     */
    RemoteEndpoint* GetBusToBusEndpoint(SessionId sessionId = 0) const;

    /**
     * Add an alternate bus-to-bus endpoint that can route for this endpoint.
     *
     * @param endpoint   A bus-to-bus endpoint that can route to this virutual endpoint.
     * @return  true if endpoint was added.
     */
    bool AddBusToBusEndpoint(RemoteEndpoint& endpoint);

    /**
     * Remove a bus-to-bus endpoint that can route for thie virtual endpoint.
     *
     * @param endpoint   Bus-to-bus endpoint to remove from list of routes
     * @return  true iff virtual endpoint has no bus-to-bus endpoint and should be removed.
     */
    bool RemoveBusToBusEndpoint(RemoteEndpoint& endpoint);

    /**
     * Map a session id to one of this VirtualEndpoint's B2B endpoints.
     *
     * @param sessionId  The session id.
     * @param b2bEp      The bus-to-bus endpoint for the session.
     * @return  ER_OK if successful.
     */
    QStatus AddSessionRef(SessionId sessionId, RemoteEndpoint& b2bEp);

    /**
     * Map a session id to the best of this VirtualEndpoint's B2B endpoints that match qos.
     *
     * @param sessionId  The session id.
     * @param qos        Qualifying qos for B2B endpoint or NULL to indicate no constraints.
     * @param b2bEp      [OUT] Written with B2B chosen for session.
     * @return  ER_OK if successful.
     */
    QStatus AddSessionRef(SessionId sessionId, QosInfo* qos, RemoteEndpoint*& b2bEp);

    /**
     * Return the "best" matching B2B endpoint for qos.
     *
     * @param qos   QoS to use for measuring acceptibility of B2B endpoints.
     * @return      QoS compatibile B2B endpoint or NULL if none is found.
     */
    RemoteEndpoint* GetQosCompatibleB2B(const QosInfo& qos);

    /**
     * Remove (counted) mapping of sessionId to B2B endpoint.
     *
     * @param sessionId  The session id.
     */
    void RemoveSessionRef(SessionId sessionId);

    /**
     * Return true iff the given bus-to-bus endpoint can potentially be used to route
     * messages for this virtual endpoint.
     *
     * @param b2bEndpoint   B2B endpoint being checked for suitability as a route for this virtual endpoint.
     * @return true iff the B2B endpoint can be used to route messages for this virtual endpoint.
     */
    bool CanUseRoute(const RemoteEndpoint& b2bEndpoint) const;

    /**
     * Indicate whether this endpoint is allowed to receive messages from remote devices.
     * VirtualEndpoints are always allowed to receive remote messages.
     *
     * @return true
     */
    bool AllowRemoteMessages() { return true; }


  private:

    const qcc::String m_uniqueName;                             /**< The unique name for this endpoint */
    std::multimap<SessionId, RemoteEndpoint*> m_b2bEndpoints;   /**< Set of b2bs that can route for this virtual ep */

    /** B2BInfo is a data container that holds B2B endpoint selection criteria */
    struct B2BInfo {
        QosInfo qos;     /**< Qos for B2BEndpoint */
        uint32_t hops;   /**< Currently unused hop count from local daemon to final destination */
    };
    mutable qcc::Mutex m_b2bEndpointsLock;                     /**< Lock that protects m_b2bEndpoints */
};

}

#endif
