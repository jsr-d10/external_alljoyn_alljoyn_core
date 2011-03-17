/**
 * @file
 * BTAccessor declaration for BlueZ
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
#ifndef _ALLJOYN_BTENDPOINT_H
#define _ALLJOYN_BTENDPOINT_H

#include <qcc/platform.h>

#include <qcc/Socket.h>
#include <qcc/String.h>

#include <alljoyn/BusAttachment.h>

#include "BDAddress.h"
#include "BlueZUtils.h"
#include "RemoteEndpoint.h"


namespace ajn {

class BTEndpoint : public RemoteEndpoint {
  public:

    /**
     * Bluetooth endpoint constructor
     */
    BTEndpoint(BusAttachment& bus,
               bool incoming,
               const qcc::String& connectSpec,
               qcc::SocketFd sockFd,
               const BDAddress& addr) :
        RemoteEndpoint(bus, incoming, connectSpec, sockStream, "bluetooth"),
        sockStream(sockFd), addr(addr)
    { }

    const BDAddress& GetBDAddress() const { return addr; }

  private:
    bluez::BTSocketStream sockStream;
    BDAddress addr;
};

} // namespace ajn

#endif
