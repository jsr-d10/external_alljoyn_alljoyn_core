/**
 * @file
 * TransportList is a factory for Transports
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
#ifndef _ALLJOYN_TRANSPORTLIST_H
#define _ALLJOYN_TRANSPORTLIST_H

#include <qcc/platform.h>
#include <qcc/String.h>

#include <vector>

#include <alljoyn/BusAttachment.h>

#include "LocalTransport.h"
#include "Transport.h"
#include "TransportFactory.h"

namespace ajn {

/**
 * %TransportList is a factory for Transport implementations.
 */
class TransportList : public TransportListener {
  public:
    /**
     * Constructor
     *
     * @param bus               The bus associated with this transport list.
     * @param factory           TransportFactoryContainer telling the list how to create its Transports.
     */
    TransportList(BusAttachment& bus, TransportFactoryContainer& factories);

    /** Destructor  */
    virtual ~TransportList();

    /**
     * Return the local transport.
     *
     * @return The local transport.
     */
    LocalTransport* GetLocalTransport() { return localTransport; }

    /**
     * Validate and normalize a transport specification string.
     * Given a transport specification, convert it into a form which is guaranteed to have a one-to-one
     * relationship with a transport.
     *
     * @param inSpec    Input transport connect spec.
     * @param outSpec   [OUT] Normalized transport connect spec.
     * @param argMap    [OUT] Normalized parameter map.
     * @return ER_OK if successful.
     */
    QStatus NormalizeTransportSpec(const char* inSpec, qcc::String& outSpec, std::map<qcc::String, qcc::String>& argMap);

    /**
     * Get a Transport instance for a specified transport specification.
     * Transport specifications have the form:
     *   @c \<transportName\>:\<param1\>=\<value1\>,\<param2\>=\<value2\>[;]
     *
     * @param transportSpec  Either a connectSpec or a listenSpec. Must be a string that starts with one
     *                       of the known transport types: @c tcp, @c unix or @c bluetooth.
     * @return  A transport instance or NULL if no such transport exists
     */
    Transport* GetTransport(const qcc::String& transportSpec);

    /**
     * Get the transport list at the specified index.
     *
     * @param index   Zero based position within transportList. Must be less than GetNumTransports()
     * @return  A transport instance or NULL if no such transport exists.
     */
    Transport* GetTransport(size_t index) { return (index < transportList.size()) ? transportList[index] : NULL; }

    /**
     * Get the number of transports on the transport list.
     * (Does not include the local transport)
     *
     * @return  The number of transports (not including LocalTransport) on the transportList
     */
    size_t GetNumTransports() { return transportList.size(); }

    /**
     * Start all the transports.
     *
     * @param  transportSpecs   The list of busAddresses separated by semi-colon that this bus should create transports for.
     * @return
     *         - ER_OK if successful.
     */
    QStatus Start(const qcc::String& transportSpecs);

    /**
     * Stop all the transports.
     *
     * @return
     *         - ER_OK if successful.
     *         - an error status otherwise.
     */
    QStatus Stop(void);

    /**
     * Wait for all transports to stop.
     *
     * @return
     *         - ER_OK if successful.
     *         - an error status otherwise.
     */
    QStatus Join(void);

    /**
     * Register a transport listener.
     *
     * @param listener    Transport listener to add.
     * @return
     *          - ER_OK if successful
     *          - an error status otherwise.
     */
    QStatus RegisterListener(TransportListener* listener);

    /**
     * TransportListener helper.
     * This method is for internal use only.
     * @see TransportListener::FoundNames()
     */
    void FoundNames(const qcc::String& busAddr, const qcc::String& guid, TransportMask transport, const std::vector<qcc::String>* names, uint8_t ttl);

    /**
     * Called when a transport gets a surprise disconnect from a remote bus.
     *
     * @param busAddr       The address of the bus formatted as a string.
     */
    void BusConnectionLost(const qcc::String& busAddr);

    /**
     * Get a list of the currently advertised names for all transports
     *
     * @param names  A vector containing the advertised names.
     */
    void GetAdvertisedNames(std::vector<qcc::String>& names);

  private:

    /**
     * Copy constructor.
     *
     * @param other  Copy source.
     */
    TransportList(const TransportList& other);

    /**
     * Assignment operator.
     * 
     * @param other RHS of assignment.
     */
    TransportList operator=(const TransportList& other);

    BusAttachment& bus;                             /**< The bus */
    std::vector<Transport*> transportList;           /**< transport list */
    std::vector<TransportListener*> listeners;       /**< transport listeners */
    LocalTransport* localTransport;                 /**< local transport */
    TransportFactoryContainer& m_factories;         /**< container for transport factories */
    bool isStarted;                                 /**< true iff transports are running */
    bool isInitialized;                             /**< true iff transportlist is initialized */
};

}  /* namespace */

#endif
