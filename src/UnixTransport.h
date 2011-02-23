/**
 * @file
 * UnixTransport is an implementation of Transport for Unix domain sockets.
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
#ifndef _ALLJOYN_UNIXTRANSPORT_H
#define _ALLJOYN_UNIXTRANSPORT_H

#ifndef __cplusplus
#error Only include UnixTransport.h in C++ code.
#endif

#include <Status.h>

#include <qcc/platform.h>
#include <qcc/String.h>
#include <qcc/Mutex.h>
#include <qcc/Socket.h>
#include <qcc/Thread.h>
#include <qcc/SocketStream.h>
#include <qcc/time.h>

#include "Transport.h"
#include "RemoteEndpoint.h"

namespace ajn {

/**
 * @internal Forward Reference
 */
class UnixEndpoint;

/**
 * @brief A class for Unix Transports used in clients and services.
 *
 * The UnixTransport class has different incarnations depending on whether or
 * not an instantiated endpoint using the transport resides in a daemon, or on
 * a service or client.  The differences between these versions revolves around
 * whether or not a server thread is listening; and routing and discovery. This
 * class provides a specialization of class Transport for use by clients and
 * services.
 */
class UnixTransport : public Transport, public RemoteEndpoint::EndpointListener, public qcc::Thread {
    friend class UnixEndpoint;

  public:
    /**
     * Create a Unix domain socket based Transport.
     *
     * @param bus  The bus associated with this transport.
     */
    UnixTransport(BusAttachment& bus);

    /**
     * Destructor
     */
    ~UnixTransport();

    /**
     * Start the transport and associate it with a router.
     *
     * @return ER_OK if successful.
     */
    QStatus Start();

    /**
     * Stop the transport.
     *
     * @return ER_OK if successful.
     */
    QStatus Stop();

    /**
     * Pend the caller until the transport stops.
     * @return ER_OK if successful.
     */
    QStatus Join();

    /**
     * Determine if this transport is running. Running means Start() has been called.
     *
     * @return  Returns true if the transport is running.
     */
    bool IsRunning() { return m_running; }

    /**
     * Normalize a transport specification.
     * Given a transport specification, convert it into a form which is guaranteed to have a one-to-one
     * relationship with a transport.
     *
     * @param inSpec    Input transport connect spec.
     * @param outSpec   Output transport connect spec.
     * @param argMap    Parsed parameter map.
     *
     * @return ER_OK if successful.
     */
    QStatus NormalizeTransportSpec(const char* inSpec, qcc::String& outSpec, std::map<qcc::String, qcc::String>& argMap);

    /**
     * Connect to a specified remote AllJoyn/DBus address.
     *
     * @param connectSpec    Transport specific key/value args used to configure the client-side endpoint.
     *                       The form of this string is @c "<transport>:<key1>=<val1>,<key2>=<val2>..."
     *                             - Valid transport is @c "unix". All others ignored.
     *                             - Valid keys are:
     *                                 - @c path = Filesystem path name for AF_UNIX socket
     *                                 - @c abstract = Abstract (unadvertised) filesystem path for AF_UNIX socket.
     * @return
     *      - ER_OK if successful.
     *      - an error status otherwise.
     */
    QStatus Connect(const char* connectSpec, RemoteEndpoint** newep);

    /**
     * Disconnect from a specified AllJoyn/DBus address.
     *
     * @param connectSpec    The connectSpec used in Connect.
     *
     * @return
     *      - ER_OK if successful.
     *      - an error status otherwise.
     */
    QStatus Disconnect(const char* connectSpec);

    /**
     * @brief Provide an empty implementation of a daemon function not used
     * by clients or services.
     *
     * @param listenSpec Unused parameter.
     *
     * @return ER_FAIL.
     */
    QStatus StartListen(const char* listenSpec) { return ER_FAIL; }

    /**
     * @brief Provide an empty implementation of a daemon function not used
     * by clients or services.
     *
     * @param listenSpec Unused parameter.
     *
     * @return ER_FAIL.
     */
    QStatus StopListen(const char* listenSpec) { return ER_FAIL; }

    /**
     * Set a listener for transport related events.  There can only be one
     * listener set at a time. Setting a listener implicitly removes any
     * previously set listener.
     *
     * @param listener  Listener for transport related events.
     */
    void SetListener(TransportListener* listener) { m_listener = listener; }

    /**
     * @internal
     * @brief Provide an empty implementation of a discovery function not used
     * by clients or services.
     *
     * @param namePrefix unused parameter.
     */
    void EnableDiscovery(const char* namePrefix) { }

    /**
     * @internal
     * @brief Provide an empty implementation of a discovery function not used
     * by clients or services.
     *
     * @param namePrefix unused parameter.
     */
    void DisableDiscovery(const char* namePrefix) { }

    /**
     * @internal
     * @brief Provide an empty implementation of a discovery function not used
     * by clients or services.
     *
     * @param advertiseName unused parameter.
     */
    void EnableAdvertisement(const qcc::String& advertiseName) { }

    /**
     * @internal
     * @brief Provide an empty implementation of a discovery function not used
     * by clients or services.
     *
     * @param advertiseName unused parameter.
     * @param nameListEmpty unused parameter.
     */
    void DisableAdvertisement(const qcc::String& advertiseName, bool nameListEmpty) { }

    /**
     * Returns the name of this transport
     */
    const char* GetTransportName()  { return TransportName(); }

    /**
     * Indicates whether this transport may be used for a connection between
     * an application and the daemon on the same machine or not.
     *
     * @return  true indicates this transport may be used for local connections.
     */
    bool LocallyConnectable() const { return true; }

    /**
     * Indicates whether this transport may be used for a connection between
     * an application and the daemon on a different machine or not.
     *
     * @return  true indicates this transport may be used for external connections.
     */
    bool ExternallyConnectable() const { return false; }

    /**
     * Name of transport used in transport specs.
     *
     * @return name of transport: @c "unix".
     */
    static const char* TransportName() { return "unix"; }

    /**
     * Callback for UnixEndpoint exit.
     *
     * @param endpoint   UnixEndpoint instance that has exited.
     */
    void EndpointExit(RemoteEndpoint* endpoint);

  private:
    BusAttachment& m_bus;                        /**< The message bus for this transport */
    bool m_running;                              /**< True after Start() has been called, before Stop() */
    bool m_stopping;                             /**< True if Stop() has been called but endpoints still exist */
    TransportListener* m_listener;               /**< Registered TransportListener */
    std::vector<UnixEndpoint*> m_endpointList;   /**< List of active endpoints */
    qcc::Mutex m_endpointListLock;               /**< Mutex that protects the endpoint list */
};

}

#endif
