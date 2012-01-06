/**
 * @file
 * Utility functions for tweaking Bluetooth behavior via BlueZ.
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

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <qcc/Socket.h>
#include <qcc/time.h>

#include "BlueZ.h"
#include "BlueZHCIUtils.h"
#include "BTTransportConsts.h"

#include <Status.h>


#define QCC_MODULE "ALLJOYN_BT"

using namespace ajn;
using namespace qcc;

namespace ajn {
namespace bluez {


const static uint16_t L2capDefaultMtu = (1 * 1021) + 1011; // 2 x 3DH5

/*
 * Compose the first two bytes of an HCI command from the OGF and OCF
 */
#define HCI_CMD(ogf, ocf, len)  0x1, (uint8_t)(((ogf) << 10) | (ocf)), (uint8_t)((((ogf) << 10) | (ocf)) >> 8), (uint8_t)(len)

#define CMD_LEN  4

/*
 * Set the L2CAP mtu to something better than the BT 1.0 default value.
 */
void ConfigL2capMTU(SocketFd sockFd)
{
    int ret;
    uint8_t secOpt = BT_SECURITY_LOW;
    socklen_t optLen = sizeof(secOpt);
    uint16_t outMtu = 672; // default BT 1.0 value
    ret = setsockopt(sockFd, SOL_BLUETOOTH, BT_SECURITY, &secOpt, optLen);
    if (ret < 0) {
        QCC_DbgPrintf(("Setting security low: %d: %s", errno, strerror(errno)));
    }

    struct l2cap_options opts;
    optLen = sizeof(opts);
    ret = getsockopt(sockFd, SOL_L2CAP, L2CAP_OPTIONS, &opts, &optLen);
    if (ret != -1) {
        opts.imtu = L2capDefaultMtu;
        opts.omtu = L2capDefaultMtu;
        ret = setsockopt(sockFd, SOL_L2CAP, L2CAP_OPTIONS, &opts, optLen);
        if (ret == -1) {
            QCC_LogError(ER_OS_ERROR, ("Failed to set in/out MTU for L2CAP socket (%d - %s)", errno, strerror(errno)));
        } else {
            outMtu = opts.omtu;
            QCC_DbgPrintf(("Set L2CAP mtu to %d", opts.omtu));
        }
    } else {
        QCC_LogError(ER_OS_ERROR, ("Failed to get in/out MTU for L2CAP socket (%d - %s)", errno, strerror(errno)));
    }

    // Only let the kernel buffer up 2 packets at a time.
    int sndbuf = 2 * outMtu;

    ret = setsockopt(sockFd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    if (ret == -1) {
        QCC_LogError(ER_OS_ERROR, ("Failed to set send buf to %d: %d - %s", sndbuf, errno, strerror(errno)));
    }
}

void ConfigL2capMaster(SocketFd sockFd)
{
    int ret;
    int lmOpt = 0;
    socklen_t optLen = sizeof(lmOpt);
    ret = getsockopt(sockFd, SOL_L2CAP, L2CAP_LM, &lmOpt, &optLen);
    if (ret == -1) {
        QCC_LogError(ER_OS_ERROR, ("Failed to get LM flags (%d - %s)", errno, strerror(errno)));
    } else {
        lmOpt |= L2CAP_LM_MASTER;
        ret = setsockopt(sockFd, SOL_L2CAP, L2CAP_LM, &lmOpt, optLen);
        if (ret == -1) {
            QCC_LogError(ER_OS_ERROR, ("Failed to set LM flags (%d - %s)", errno, strerror(errno)));
        }
    }
}

} // namespace bluez
} // namespace ajn
