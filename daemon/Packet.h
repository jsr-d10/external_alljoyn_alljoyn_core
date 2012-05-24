/**
 * @file
 * PacketEngine packet format.
 */

/******************************************************************************
 * Copyright 2011-2012, Qualcomm Innovation Center, Inc.
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
#ifndef _ALLJOYN_PACKET_H
#define _ALLJOYN_PACKET_H

#include <qcc/platform.h>
#include <qcc/String.h>

#include "Status.h"

namespace ajn {

/* Packet flag bit definitions */
#define PACKET_FLAG_CONTROL    0x01     /* Packet is a control (non data) packet */
#define PACKET_FLAG_BOM        0x02     /* Packet is the beginning of a potentially multi-packet message (data only) */
#define PACKET_FLAG_EOM        0x04     /* Packet is the end of a potentially multi-packet message (data only) */
#define PACKET_FLAG_DELAY_ACK  0x08     /* Data packet may be acked by the receiver in a delayed manner */
#define PACKET_FLAG_FLOW_OFF   0x10     /* Transmitter is XOFF (and will be expecting XON) */

/* Control packet command types (payload offset = 0, size = BYTE) */
#define PACKET_COMMAND_CONNECT_REQ         0x01
#define PACKET_COMMAND_CONNECT_RSP         0x02
#define PACKET_COMMAND_CONNECT_RSP_ACK     0x03
#define PACKET_COMMAND_DISCONNECT_REQ      0x04
#define PACKET_COMMAND_DISCONNECT_RSP      0x05
#define PACKET_COMMAND_DISCONNECT_RSP_ACK  0x06
#define PACKET_COMMAND_ACK                 0x07
#define PACKET_COMMAND_XON                 0x08
#define PACKET_COMMAND_XON_ACK             0x09

/* Forward Declarations */
class PacketSource;
class PacketEngineStream;

/* Types */
struct PacketDest {
    uint64_t data[4];   /** Must be large enough for sockaddr_t */
};

class Packet {
  public:
    static const size_t payloadOffset;

    uint32_t chanId;       /* Channel Id */
    uint16_t seqNum;       /* Incrementing packet sequence number */
    uint16_t gap;          /* # of missing packets prior to this packet */
    uint8_t flags;         /* Message flags */
    size_t payloadLen;     /* Payload length */
    uint32_t* payload;     /* Pointer to payload */
    uint32_t* buffer;      /* Pointer to beginning of packet */
    uint64_t expireTs;     /* Packet expiration timestamp */
    uint64_t sendTs;       /* Timestampe when packet was last sent */
    uint16_t sendAttempts; /* Number of times this packet has been sent */
    bool fastRetransmit;   /* true iff packet has been fast retransmitted */
    Packet(size_t mtu);

    ~Packet();

    size_t SetPayload(const void* payload, size_t payloadLen);
    void SetSender(const PacketDest& sender) { this->sender = sender; }
    const PacketDest& GetSender() const { return sender; }

    /**
     * Unmarshal serialized packet state into object form.
     *
     * @param source   Serial data source use to populate packet state.
     * @return ER_OK if successful.
     */
    QStatus Unmarshal(PacketSource& source);

    /**
     * Marshal packet state into serialized form.
     * After calling this method, the packet's object state will be serialized into the buffer member.
     */
    void Marshal();

    /**
     * Reinitialize state of packet.
     */
    void Clean();

  private:
    size_t mtu;
    uint16_t crc16;
    uint8_t version;
    PacketDest sender;

    Packet();
};

class PacketReceiver {
  public:
    virtual ~PacketReceiver() { }
    virtual QStatus PushPacket(const Packet& packet) = 0;
};

}

#endif
