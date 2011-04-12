/**
 * @file
 * BusObject responsible for controlling/handling Bluetooth delegations.
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
#ifndef _ALLJOYN_BTCONTROLLER_H
#define _ALLJOYN_BTCONTROLLER_H

#include <qcc/platform.h>

#include <list>
#include <map>
#include <set>
#include <vector>

#include <qcc/ManagedObj.h>
#include <qcc/String.h>
#include <qcc/Timer.h>

#include <alljoyn/BusObject.h>
#include <alljoyn/Message.h>
#include <alljoyn/MessageReceiver.h>

#include "BDAddress.h"
#include "BTNodeDB.h"
#include "Bus.h"
#include "NameTable.h"


namespace ajn {

typedef qcc::ManagedObj<std::set<BDAddress> > BDAddressSet;

class BluetoothDeviceInterface {
    friend class BTController;

  public:

    virtual ~BluetoothDeviceInterface() { }

  private:

    /**
     * Start the find operation for AllJoyn capable devices.  A duration may
     * be specified that will result in the find operation to automatically
     * stop after the specified number of seconds.  Exclude any results from
     * any device in the list of ignoreAddrs.
     *
     * @param ignoreAddrs   Set of BD addresses to ignore
     * @param duration      Find duration in seconds (0 = forever)
     *
     * @return  ER_OK if successful
     */
    virtual QStatus StartFind(const BDAddressSet& ignoreAddrs, uint32_t duration = 0) = 0;

    /**
     * Stop the find operation.
     *
     * @return  ER_OK if successful
     */
    virtual QStatus StopFind() = 0;

    /**
     * Start the advertise operation for the given list of names.  A duration
     * may be specified that will result in the advertise operation to
     * automatically stop after the specified number of seconds.  This
     * includes setting the SDP record to contain the information specified in
     * the parameters.
     *
     * @param uuidRev   AllJoyn Bluetooth service UUID revision
     * @param bdAddr    BD address of the connectable node
     * @param psm       The L2CAP PSM number for the AllJoyn service
     * @param adInfo    The complete list of names to advertise and their associated GUIDs
     * @param duration  Find duration in seconds (0 = forever)
     *
     * @return  ER_OK if successful
     */
    virtual QStatus StartAdvertise(uint32_t uuidRev,
                                   const BDAddress& bdAddr,
                                   uint16_t psm,
                                   const BTNodeDB& adInfo,
                                   uint32_t duration = 0) = 0;


    /**
     * Stop the advertise operation.
     *
     * @return  ER_OK if successful
     */
    virtual QStatus StopAdvertise() = 0;

    /**
     * This provides the Bluetooth transport with the information needed to
     * call AllJoynObj::FoundNames and to generate the connect spec.
     *
     * @param bdAddr    BD address of the connectable node
     * @param guid      Bus GUID of the discovered bus
     * @param names     The advertised names
     * @param psm       L2CAP PSM accepting connections
     * @param lost      Set to true if names are lost, false otherwise
     */
    virtual void FoundNamesChange(const qcc::String& guid,
                                  const std::vector<qcc::String>& names,
                                  const BDAddress& bdAddr,
                                  uint16_t psm,
                                  bool lost) = 0;

    /**
     * Tells the Bluetooth transport to start listening for incoming connections.
     *
     * @param addr      [OUT] BD Address of the adapter listening for connections
     * @param psm       [OUT] L2CAP PSM allocated
     *
     * @return  ER_OK if successful
     */
    virtual QStatus StartListen(BDAddress& addr,
                                uint16_t& psm) = 0;

    /**
     * Tells the Bluetooth transport to stop listening for incoming connections.
     */
    virtual void StopListen() = 0;

    /**
     * Retrieves the information from the specified device necessary to
     * establish a connection and get the list of advertised names.
     *
     * @param addr      BD address of the device of interest.
     * @param uuidRev   [OUT] UUID revision number.
     * @param connAddr  [OUT] BD address of the connectable device.
     * @param psm       [OUT] L2CAP PSM that is accepting AllJoyn connections.
     * @param adInfo    [OUT] Advertisement information.
     *
     * @return  ER_OK if successful
     */
    virtual QStatus GetDeviceInfo(const BDAddress& addr,
                                  uint32_t& uuidRev,
                                  BDAddress& connAddr,
                                  uint16_t& psm,
                                  BTNodeDB& adInfo) = 0;

    virtual QStatus Connect(const BTBusAddress& addr) = 0;

    virtual QStatus Disconnect(const BTBusAddress& addr) = 0;
};


/**
 * BusObject responsible for Bluetooth topology management.  This class is
 * used by the Bluetooth transport for the purposes of maintaining a sane
 * topology.
 */
class BTController : public BusObject, public NameListener, public qcc::AlarmListener {
  public:
    static const uint32_t INVALID_UUIDREV = 0;      /**< Invalid UUID Revsision number */

    /**
     * Constructor
     *
     * @param bus    Bus to associate with org.freedesktop.DBus message handler.
     * @param bt     Bluetooth transport to interact with.
     */
    BTController(BusAttachment& bus, BluetoothDeviceInterface& bt);

    /**
     * Destructor
     */
    ~BTController();

    /**
     * Called by the message bus when the object has been successfully registered. The object can
     * perform any initialization such as adding match rules at this time.
     */
    void ObjectRegistered();

    /**
     * Initialize and register this DBusObj instance.
     *
     * @return ER_OK if successful.
     */
    QStatus Init();

    /**
     * Send the SetState method call to the Master node we are connecting to.
     *
     * @param busName           Unique name of the endpoint with the
     *                          BTController object on the device just
     *                          connected to
     *
     * @return ER_OK if successful.
     */
    QStatus SendSetState(const qcc::String& busName);

    /**
     * Send the AdvertiseName signal to the node we believe is the Master node
     * (may actually be a drone node).
     *
     * @param name          Name to add to the list of advertise names.
     *
     * @return ER_OK if successful.
     */
    QStatus AddAdvertiseName(const qcc::String& name);

    /**
     * Send the CancelAdvertiseName signal to the node we believe is the
     * Master node (may actually be a drone node).
     *
     * @param name          Name to remove from the list of advertise names.
     *
     * @return ER_OK if successful.
     */
    QStatus RemoveAdvertiseName(const qcc::String& name);

    /**
     * Send the FindName signal to the node we believe is the Master node (may
     * actually be a drone node).
     *
     * @param name          Name to add to the list of find names.
     *
     * @return ER_OK if successful.
     */
    QStatus AddFindName(const qcc::String& name)
    {
        return DoNameOp(name, *org.alljoyn.Bus.BTController.FindName, true, find);
    }

    /**
     * Send the CancelFindName signal to the node we believe is the Master
     * node (may actually be a drone node).
     *
     * @param findName      Name to remove from the list of find names.
     *
     * @return ER_OK if successful.
     */
    QStatus RemoveFindName(const qcc::String& name)
    {
        return DoNameOp(name, *org.alljoyn.Bus.BTController.CancelFindName, false, find);
    }

    /**
     * Process the found or lost device or pass it up the chain to the master node if
     * we are not the master.
     *
     * @param bdAddr   BD Address from the SDP record.
     * @param uuidRev  UUID revsision of the found bus.
     * @param lost     Set to true when device change is lost, false if found or changed.
     *
     * @return ER_OK if successful.
     */
    void ProcessDeviceChange(const BDAddress& adBdAddr,
                             uint32_t newUUIDRev,
                             uint32_t oldUUIDRev,
                             bool lost);

    /**
     * Test whether it is ok to make outgoing connections or accept incoming
     * connections.
     *
     * @return  true if OK to make or accept a connection; false otherwise.
     */
    bool OKToConnect() const { return IsMaster() && (directMinions < maxConnections); }

    /**
     * Perform preparations for an outgoing connection.  For now, this just
     * turns off discovery and discoverability when there are no other
     * Bluetooth AllJoyn connections.
     */
    void PrepConnect();

    /**
     * Perform operations necessary based on the result of connect operation.
     * For now, this just restores the local discovery and discoverability
     * when the connect operation failed and there are no other Bluetooth
     * AllJoyn connections.
     *
     * @param status    Status result of creating the connection.
     * @param ep        Pointer to the newly created enpoint if status == ER_OK.
     */
    void PostConnect(QStatus status, const RemoteEndpoint* ep);

    /**
     * Function for the BT Transport to inform a change in the
     * power/availablity of the Bluetooth device.
     *
     * @param on    true if BT device is powered on and available, false otherwise.
     */
    void BTDeviceAvailable(bool on);

    /**
     * Check if it is OK to accept the incoming connection from the specified
     * address.
     *
     * @param addr  BT device address to check
     *
     * @return  true if OK to accept the connection, false otherwise.
     */
    bool CheckIncomingAddress(const BDAddress& addr) const;

    void NameOwnerChanged(const qcc::String& alias,
                          const qcc::String* oldOwner,
                          const qcc::String* newOwner);

  private:
    static const uint32_t DELEGATE_TIME = 30;   /**< Delegate ad/find operations to minion for 30 seconds. */

    struct NameArgInfo : public AlarmListener {
        BTController& bto;
        BTNodeInfo minion;
        MsgArg* args;
        const size_t argsSize;
        const InterfaceDescription::Member* delegateSignal;
        qcc::Timer& dispatcher;
        qcc::Alarm alarm;
        bool active;
        bool dirty;
        NameArgInfo(BTController& bto, size_t size, qcc::Timer& dispatcher) :
            bto(bto), argsSize(size), dispatcher(dispatcher), active(false)
        {
            args = new MsgArg[size];
            minion = bto.self;
        }
        virtual ~NameArgInfo() { delete[] args; }
        virtual void SetArgs() = 0;
        virtual void ClearArgs() = 0;
        virtual void AddName(const qcc::String& name, BTNodeInfo& node) = 0;
        virtual void RemoveName(const qcc::String& name, BTNodeInfo& node) = 0;
        virtual size_t Empty() const = 0;
        bool Changed() const { return dirty; }
        void AddName(const qcc::String& name)
        {
            AddName(name, bto.self);
        }
        void RemoveName(const qcc::String& name)
        {
            RemoveName(name, bto.self);
        }
        void StartAlarm()
        {
            alarm = qcc::Alarm(BTController::DELEGATE_TIME * 1000, this, 0, this);
            dispatcher.AddAlarm(alarm);
        }
        void StopAlarm() { dispatcher.RemoveAlarm(alarm); }
        virtual bool UseLocal() = 0;
        virtual void StartOp(bool restart = false);
        void RestartOp() { assert(active); StartOp(true); }
        virtual void StopOp();
        virtual QStatus StartLocal() = 0;
        virtual QStatus StopLocal() = 0;
        QStatus SendDelegateSignal();

      private:
        void AlarmTriggered(const qcc::Alarm& alarm, QStatus reason);
    };

    struct AdvertiseNameArgInfo : public NameArgInfo {
        std::vector<MsgArg> adInfoArgs;
        size_t count;
        AdvertiseNameArgInfo(BTController& bto, qcc::Timer& dispatcher) :
            NameArgInfo(bto, 5, dispatcher), count(0)
        { }
        void AddName(const qcc::String& name, BTNodeInfo& node);
        void RemoveName(const qcc::String& name, BTNodeInfo& node);
        size_t Empty() const { return count == 0; }
        void SetArgs();
        void ClearArgs();
        bool UseLocal() { return bto.UseLocalAdvertise(); }
        void StartOp(bool restart = false);
        void StopOp();
        QStatus StartLocal();
        QStatus StopLocal();
    };

    struct FindNameArgInfo : public NameArgInfo {
        qcc::String resultDest;
        BDAddressSet ignoreAddrs;
        NameSet names;
        FindNameArgInfo(BTController& bto, qcc::Timer& dispatcher) :
            NameArgInfo(bto, 4, dispatcher)
        { }
        void AddName(const qcc::String& name, BTNodeInfo& node);
        void RemoveName(const qcc::String& name, BTNodeInfo& node);
        size_t Empty() const { return names.empty(); }
        void SetArgs();
        void ClearArgs();
        bool UseLocal() { return bto.UseLocalFind(); }
        QStatus StartLocal() { return bto.bt.StartFind(ignoreAddrs); }
        QStatus StopLocal() { return bto.bt.StopFind(); }
    };


    struct UUIDRevCacheInfo {
        BDAddress adAddr;       /**< Advertising BDAddress */
        uint32_t uuidRev;       /**< Advertised UUID Revision */
        BTBusAddress connAddr;
        BTNodeDB* adInfo;
        UUIDRevCacheInfo() : uuidRev(INVALID_UUIDREV) { }
        UUIDRevCacheInfo(const BDAddress& adAddr, uint32_t uuidRev, const BTBusAddress& connAddr, BTNodeDB* adInfo) :
            adAddr(adAddr), uuidRev(uuidRev), connAddr(connAddr), adInfo(adInfo) { }
    };

    typedef std::multimap<uint32_t, UUIDRevCacheInfo> UUIDRevCacheMap;

    /**
     * Distribute the advertised name changes to all connected nodes.
     *
     * @param newAdInfo     Added advertised names
     * @param oldAdInfo     Removed advertiesd names
     *
     * @return ER_OK if successful.
     */
    QStatus DistributeAdvertisedNameChanges(const BTNodeDB& newAdInfo,
                                            const BTNodeDB& oldAdInfo);

    /**
     * Send the FoundNames signal to the node interested in one or more of the
     * names on that bus.
     *
     * @param destNode The minion that should receive the message.
     * @param adInfo   Advertise information to send.
     * @param lost     Set to true if names are lost, false otherwise.
     *
     * @return ER_OK if successful.
     */
    QStatus SendFoundNamesChange(const BTNodeInfo& destNode,
                                 const BTNodeDB& adInfo,
                                 bool lost);

    /**
     * Send the one of the following specified signals to the node we believe
     * is the Master node (may actually be a drone node): FindName,
     * CancelFindName, AdvertiseName, CancelAdvertiseName.
     *
     * @param name          Name to remove from the list of advertise names.
     * @param signal        Reference to the signal to be sent.
     * @param add           Flag indicating if adding or removing a name.
     * @param nameArgInfo   Advertise of Find name arg info structure to inform of name change.
     *
     * @return ER_OK if successful.
     */
    QStatus DoNameOp(const qcc::String& findName,
                     const InterfaceDescription::Member& signal,
                     bool add,
                     NameArgInfo& nameArgInfo);

    /**
     * Handle one of the following incoming signals: FindName, CancelFindName,
     * AdvertiseName, CancelAdvertiseName.
     *
     * @param member        Member.
     * @param sourcePath    Object path of signal sender.
     * @param msg           The incoming message.
     */
    void HandleNameSignal(const InterfaceDescription::Member* member,
                          const char* sourcePath,
                          Message& msg);

    /**
     * Handle the incoming SetState method call.
     *
     * @param member    Member.
     * @param msg       The incoming message - "y(ssasas)":
     *                    - Number of direct minions
     *                    - struct:
     *                        - The node's unique bus name
     *                        - The node's GUID
     *                        - List of names to advertise
     *                        - List of names to find
     */
    void HandleSetState(const InterfaceDescription::Member* member,
                        Message& msg);

    /**
     * Handle the incoming DelegateFind signal.
     *
     * @param member        Member.
     * @param sourcePath    Object path of signal sender.
     * @param msg           The incoming message - "sas":
     *                        - Master node's bus name
     *                        - List of names to find
     *                        - Bluetooth UUID revision to ignore
     */
    void HandleDelegateFind(const InterfaceDescription::Member* member,
                            const char* sourcePath,
                            Message& msg);

    /**
     * Handle the incoming DelegateAdvertise signal.
     *
     * @param member        Member.
     * @param sourcePath    Object path of signal sender.
     * @param msg           The incoming message - "ssqas":
     *                        - Bluetooth UUID
     *                        - BD Address
     *                        - L2CAP PSM
     *                        - List of names to advertise
     */
    void HandleDelegateAdvertise(const InterfaceDescription::Member* member,
                                 const char* sourcePath,
                                 Message& msg);

    /**
     * Handle the incoming FoundNames signal.
     *
     * @param member        Member.
     * @param sourcePath    Object path of signal sender.
     * @param msg           The incoming message - "assq":
     *                        - List of advertised names
     *                        - BD Address
     *                        - L2CAP PSM
     */
    void HandleFoundNamesChange(const InterfaceDescription::Member* member,
                                const char* sourcePath,
                                Message& msg);

    /**
     * Handle the incoming FoundDevice signal.
     *
     * @param member        Member.
     * @param sourcePath    Object path of signal sender.
     * @param msg           The incoming message - "su":
     *                        - BD Address
     *                        - UUID Revision number
     */
    void HandleFoundDeviceChange(const InterfaceDescription::Member* member,
                                 const char* sourcePath,
                                 Message& msg);

    /**
     * Update the internal state information for other nodes based on incoming
     * message args.
     *
     * @param nodeStateEntries  List of message args containing the remote
     *                          node's state information.
     * @param numNodeStates     Number of node state entries in the message arg
     *                          list.
     * @param addr              BT bus address of the newly connected minion
     *                          node.
     */
    void ImportState(MsgArg* nodeStateEntries,
                     size_t numNodeStates,
                     const BTBusAddress& addr);

    /**
     * Updates the find/advertise name information on the minion assigned to
     * perform the specified name discovery operation.  It will update the
     * delegation for find or advertise based on current activity, whether we
     * are the Master or not, if the name list changed, and if we can
     * participate in more connections.
     *
     * @param nameInfo  Reference to the advertise or find name info struct
     * @param allow     false = force stop; true = allow normal update
     */
    void UpdateDelegations(NameArgInfo& nameInfo, bool allow = true);

    /**
     * Extract advertisement information from a message arg into the internal
     * representation.
     *
     * @param entries   Array of MsgArgs all with type dict entry:
     *                  - Key: Bus GUID associated with advertise names
     *                  - Value: Array of bus names advertised by device with associated Bus GUID
     * @param size      Number of entries in the array
     * @param adInfo    Advertisement information to be filled
     *
     * @return  ER_OK if advertisment information successfully extracted.
     */
    static QStatus ExtractAdInfo(const MsgArg* entries, size_t size, BTNodeDB& adInfo);


    void AlarmTriggered(const qcc::Alarm& alarm, QStatus reason);

    bool IsMaster() const { return !master; }
    bool IsDrone() const { return (master && (NumMinions() > 0)); }
    bool IsMinion() const { return (master && (NumMinions() == 0)); }

    size_t NumMinions() const { return nodeDB.Size() - 1; }

    void NextDirectMinion(BTNodeInfo& minion)
    {
        BTNodeInfo& skip = (minion == find.minion) ? advertise.minion : find.minion;
        minion = nodeDB.FindDirectMinion(minion, skip);
    }

    bool UseLocalFind() { return !master && (directMinions == 0); }
    bool UseLocalAdvertise() { return !master && (directMinions <= 1); }
    bool RotateMinions() { return !master && (directMinions > 2); }

#ifndef NDEBUG
    void DumpNodeStateTable() const;
#endif

    BusAttachment& bus;
    BluetoothDeviceInterface& bt;

    ProxyBusObject* master;        // Bus Object we believe is our master
    BTNodeInfo masterNode;

    uint8_t maxConnects;           // Maximum number of direct connections
    uint32_t masterUUIDRev;        // Revision number for AllJoyn Bluetooth UUID
    uint8_t directMinions;         // Number of directly connected minions
    const uint8_t maxConnections;
    bool listening;
    bool devAvailable;


    BTNodeDB foundNodeDB;
    BTNodeDB nodeDB;
    BTNodeInfo self;

    mutable qcc::Mutex lock;

    BTBusAddress listenAddr;

    AdvertiseNameArgInfo advertise;
    FindNameArgInfo find;

    qcc::Alarm stopAd;

    UUIDRevCacheMap uuidRevCache;
    qcc::Mutex cacheLock;

    struct {
        struct {
            struct {
                struct {
                    const InterfaceDescription* interface;
                    // Methods
                    const InterfaceDescription::Member* SetState;
                    // Signals
                    const InterfaceDescription::Member* FindName;
                    const InterfaceDescription::Member* CancelFindName;
                    const InterfaceDescription::Member* AdvertiseName;
                    const InterfaceDescription::Member* CancelAdvertiseName;
                    const InterfaceDescription::Member* DelegateAdvertise;
                    const InterfaceDescription::Member* DelegateFind;
                    const InterfaceDescription::Member* FoundNames;
                    const InterfaceDescription::Member* LostNames;
                    const InterfaceDescription::Member* FoundDevice;
                    const InterfaceDescription::Member* LostDevice;
                } BTController;
            } Bus;
        } alljoyn;
    } org;

};

}

#endif
