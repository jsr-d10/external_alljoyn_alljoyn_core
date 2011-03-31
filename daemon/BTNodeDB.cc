/**
 * @file
 *
 * Implements the BT node database.
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
#include <set>
#include <vector>

#include <qcc/String.h>

#include <alljoyn/MsgArg.h>

#include "BDAddress.h"
#include "BTNodeDB.h"

#define QCC_MODULE "ALLJOYN_BTC"


using namespace std;
using namespace qcc;


namespace ajn {


bool _BTNodeInfo::IsMinionOf(const BTNodeInfo& master) const
{
    BTNodeInfo* next = connectProxyNode;
    while (next) {
        if (*next == master) {
            return true;
        }
        next = (*next)->connectProxyNode;
    }
    return false;
}


const BTBusAddress& _BTNodeInfo::GetConnectAddress() const
{
    const _BTNodeInfo* next = this;
    while (next->connectProxyNode) {
        next = &(*(*(next->connectProxyNode)));
    }
    return next->nodeAddr;
}



const BTNodeInfo BTNodeDB::FindNode(const BTBusAddress& addr) const
{
    BTNodeInfo node;
    Lock();
    NodeAddrMap::const_iterator it = addrMap.find(addr);
    if (it != addrMap.end()) {
        node = it->second;
    }
    Unlock();
    return node;
}


const BTNodeInfo BTNodeDB::FindNode(const BDAddress& addr) const
{
    BTNodeInfo node;
    BTBusAddress busAddr(addr, BTBusAddress::INVALID_PSM);
    Lock();
    NodeAddrMap::const_iterator it = addrMap.lower_bound(busAddr);
    if (it != addrMap.end() && (it->second)->nodeAddr.addr == addr) {
        node = it->second;
    }
    Unlock();
    return node;
}


const BTNodeInfo BTNodeDB::FindNode(const String& uniqueName) const
{
    BTNodeInfo node;
    Lock();
    NodeNameMap::const_iterator it = nameMap.find(uniqueName);
    if (it != nameMap.end()) {
        node = it->second;
    }
    Unlock();
    return node;
}


BTNodeInfo BTNodeDB::FindDirectMinion(const BTNodeInfo& start, const BTNodeInfo& skip) const
{
    Lock();
    const_iterator next = nodes.find(start);
    assert(next != End());
    do {
        ++next;
        if (next == End()) {
            next = Begin();
        }
    } while ((!(*next)->directMinion || (*next == skip)) && ((*next) != start));
    BTNodeInfo node = *next;
    Unlock();
    assert(start->GetConnectAddress() == node->GetConnectAddress());
    return node;
}


void BTNodeDB::FillNodeStateMsgArgs(vector<MsgArg>& arg) const
{
    arg.reserve(nodes.size());
    const_iterator it;
    Lock();
    for (it = Begin(); it != End(); ++it) {
        const BTNodeInfo& node = *it;
        QCC_DbgPrintf(("    Node %s:", node->uniqueName.c_str()));
        NameSet::const_iterator nit;

        /*
         * It is acceptable to use a local vector<char*> here because when the
         * array is passed into MsgArg, it will copy the pointer value into
         * another array instead of using the address of the passed in array.
         * The char*, however, must remain valid for the lifetime of the
         * MsgArg since the strings themselves are _not_ copied.  In the case
         * here, the char* come from strings stored in the class's node state
         * table and thus have a lifetime that exceeds the generated MsgArg.
         */
        vector<const char*> nodeAdNames;
        nodeAdNames.reserve(node->AdvertiseNamesSize());
        for (nit = node->GetAdvertiseNamesBegin(); nit != node->GetAdvertiseNamesEnd(); ++nit) {
            QCC_DbgPrintf(("        Ad name: %s", nit->c_str()));
            nodeAdNames.push_back(nit->c_str());
        }

        vector<const char*> nodeFindNames;
        nodeFindNames.reserve(node->FindNamesSize());
        for (nit = node->GetFindNamesBegin(); nit != node->GetFindNamesEnd(); ++nit) {
            QCC_DbgPrintf(("        Find name: %s", nit->c_str()));
            nodeFindNames.push_back(nit->c_str());
        }

        arg.push_back(MsgArg("(sstqasas)", //SIG_NODE_STATE_ENTRY,
                             node->guid.c_str(),
                             node->uniqueName.c_str(),
                             node->nodeAddr.addr.GetRaw(),
                             node->nodeAddr.psm,
                             nodeAdNames.size(), &nodeAdNames.front(),
                             nodeFindNames.size(), &nodeFindNames.front()));
    }
    Unlock();
}


void BTNodeDB::Diff(const BTNodeDB& other, BTNodeDB* added, BTNodeDB* removed) const
{
    Lock();
    other.Lock();
    if (added) {
        added->Lock();
    }
    if (removed) {
        removed->Lock();
    }

    const_iterator nodeit;
    NodeAddrMap::const_iterator addrit;

    // Find removed names/nodes
    for (nodeit = Begin(); nodeit != End(); ++nodeit) {
        const BTNodeInfo& node = *nodeit;
        addrit = other.addrMap.find(node->nodeAddr);
        if (addrit == addrMap.end()) {
            if (removed) {
                removed->AddNode(node);
            }
        } else {
            BTNodeInfo diffNode(node->guid, node->uniqueName, node->nodeAddr);
            bool include = false;
            const BTNodeInfo& onode = addrit->second;
            NameSet::const_iterator nameit;
            NameSet::const_iterator onameit;
            for (nameit = node->GetAdvertiseNamesBegin(); nameit != node->GetAdvertiseNamesEnd(); ++nameit) {
                const String& name = *nameit;
                onameit = onode->FindAdvertiseName(name);
                if (onameit == onode->GetAdvertiseNamesEnd()) {
                    diffNode->AddAdvertiseName(name);
                    include = true;
                }
            }
            if (include) {
                removed->AddNode(diffNode);
            }
        }
    }

    // Find added names/nodes
    for (nodeit = other.Begin(); nodeit != other.End(); ++nodeit) {
        const BTNodeInfo& onode = *nodeit;
        addrit = addrMap.find(onode->nodeAddr);
        if (addrit == addrMap.end()) {
            if (added) {
                added->AddNode(onode);
            }
        } else {
            BTNodeInfo diffNode(onode->guid, onode->uniqueName, onode->nodeAddr);
            bool include = false;
            const BTNodeInfo& node = addrit->second;
            NameSet::const_iterator nameit;
            NameSet::const_iterator onameit;
            for (onameit = onode->GetAdvertiseNamesBegin(); onameit != onode->GetAdvertiseNamesEnd(); ++onameit) {
                const String& oname = *onameit;
                nameit = node->FindAdvertiseName(oname);
                if (nameit == node->GetAdvertiseNamesEnd()) {
                    diffNode->AddAdvertiseName(oname);
                    include = true;
                }
            }
            if (include) {
                added->AddNode(diffNode);
            }
        }
    }

    if (removed) {
        removed->Unlock();
    }
    if (added) {
        added->Unlock();
    }
    other.Unlock();
    Unlock();
}


void BTNodeDB::UpdateDB(const BTNodeDB* added, const BTNodeDB* removed, bool removeNodes)
{
    // Remove names/nodes
    if (removed) {
        const_iterator rit;
        for (rit = removed->Begin(); rit != removed->End(); ++rit) {
            const BTNodeInfo& rnode = *rit;
            NodeAddrMap::iterator it = addrMap.find(rnode->nodeAddr);
            if (it != addrMap.end()) {
                // Remove names from node
                BTNodeInfo& node = it->second;
                NameSet::const_iterator rnameit;
                for (rnameit = rnode->GetAdvertiseNamesBegin(); rnameit != rnode->GetAdvertiseNamesEnd(); ++rnameit) {
                    const String& rname = *rnameit;
                    node->RemoveAdvertiseName(rname);
                }
                if (removeNodes && (node->AdvertiseNamesEmpty())) {
                    // Remove node with no advertise names
                    RemoveNode(node);
                }
            } // else not in DB so ignore it.
        }
    }

    if (added) {
        // Add names/nodes
        const_iterator ait;
        for (ait = added->Begin(); ait != added->End(); ++ait) {
            const BTNodeInfo& anode = *ait;
            NodeAddrMap::iterator it = addrMap.find(anode->nodeAddr);
            if (it == addrMap.end()) {
                // New node
                AddNode(anode);
            } else {
                // Add names to existing node
                BTNodeInfo& node = it->second;
                NameSet::const_iterator anameit;
                for (anameit = anode->GetAdvertiseNamesBegin(); anameit != anode->GetAdvertiseNamesEnd(); ++anameit) {
                    const String& aname = *anameit;
                    node->AddAdvertiseName(aname);
                }
            }
        }
    }
}

}
