/**
 * @file
 * Implementation of Stream class for reading and writing data to the
 * Windows Bluetooth driver.
 */

/******************************************************************************
 * Copyright 2011, Qualcomm Innovation Center, Inc.
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

#include "WindowsBTStream.h"

#define QCC_MODULE "ALLJOYN_BT"

using namespace qcc;
using namespace ajn;

namespace ajn {

QStatus WindowsBTStream::PushBytes(const void* buf, size_t numBytes, size_t& numSent)
{
    QStatus returnValue = ER_OK;

    QCC_DbgPrintf(("PushBytes() %ld bytes to channel %p.", numBytes, GetChannelHandle()));

    if (!btAccessor) {
        returnValue = ER_INIT_FAILED;
    } else {
        USER_KERNEL_MESSAGE* messageIn;
        const size_t totalBytes = sizeof(*messageIn) + numBytes - 1;

        messageIn = (USER_KERNEL_MESSAGE*)malloc(totalBytes);

        if (!messageIn) {
            returnValue = ER_OUT_OF_MEMORY;
        } else {
            USER_KERNEL_MESSAGE messageOut;
            size_t bytesReturned = 0;

            messageIn->version = DRIVER_VERSION;
            messageIn->is64Bit = IS_64BIT;

            messageIn->commandStatus.command = USRKRNCMD_WRITE;
            messageIn->messageData.write.channelHandle = channelHandle;
            messageIn->messageData.write.bytesOfData = numBytes;

            memcpy_s(&messageIn->messageData.write.data[0], numBytes, buf, numBytes);

            bool result = btAccessor->DeviceIo(messageIn, totalBytes, &messageOut,
                                               sizeof(messageOut), &bytesReturned);
            free(messageIn);

            if (result) {
                returnValue = messageOut.commandStatus.status;

                if (ER_OK != returnValue) {
                    QCC_DbgPrintf(("PushBytes() USRKRNCMD_WRITE returned: QStatus = %s, NTSTATUS = 0x%08X",
                                   QCC_StatusText(returnValue),
                                   messageOut.messageData.write.ntStatus));
                }
            } else {
                QCC_LogError(returnValue,
                             ("PushBytes() was unable to contact the kernel! Error = 0x%08X",
                              ::GetLastError()));
                btAccessor->DebugDumpKernelState();

                if (bytesReturned != sizeof(messageOut)) {
                    returnValue = ER_OS_ERROR;
                }
            }
        }
    }

    numSent = returnValue == ER_OK ? numBytes : 0;

    return returnValue;
}

QStatus WindowsBTStream::PullBytes(void* buf,
                                   size_t reqBytes,
                                   size_t& actualBytes,
                                   uint32_t timeout)
{
    USER_KERNEL_MESSAGE message = { USRKRNCMD_READ };
    size_t bytesReturned = 0;
    QStatus returnValue = ER_OK;

    QCC_DbgPrintf(("PullBytes() expects %ld bytes in %d mS from handle %p.",
                   reqBytes, timeout, GetChannelHandle()));

    message.version = DRIVER_VERSION;
    message.is64Bit = IS_64BIT;
    actualBytes = 0;

    if (!btAccessor) {
        returnValue = ER_INIT_FAILED;
        goto Error;
    }

    if (NULL == buf) {
        returnValue = ER_BAD_ARG_1;
        goto Error;
    }

    if (0 == reqBytes) {
        goto Error;
    }

    // It is possible for this thread to have removed all the bytes from the buffer
    // in the kernel then come back for more and be waiting when the thread that gets
    // messages from the kernel just got around to processing the message that said
    // there was data waiting some time previously. The effect is that this thread could
    // call the kernel after the dataAvailable event has been set and find there is no
    // data available. Hence this thread ends up with a zero byte read and the timeout has
    // not expired.
    //
    // We therefore have a loop repeatedly waiting for the event to be set and calling
    // the kernel until either data becomes available or the time out has expired.
    uint32_t remainingTime = timeout;
    uint64_t t0 = GetTimestamp64();


    do {
        // Check for ER_SOCK_OTHER_END_CLOSED here so we don't wait
        // if the connection has been closed and no update is expected.
        if (ER_SOCK_OTHER_END_CLOSED == connectionStatus) {
            returnValue = ER_SOCK_OTHER_END_CLOSED;
            goto Error;
        }

        returnValue = Event::Wait(dataAvailable, remainingTime);

        uint64_t tNow = GetTimestamp64();
        uint64_t elapsed = tNow - t0;

        if (elapsed > timeout) {
            remainingTime = 0;
        } else {
            remainingTime -= elapsed;
        }

        t0 = tNow;

        if (ER_OK != returnValue) {
            QCC_DbgPrintf(("PullBytes() timed out (%d mS) on address 0x%012I64X, handle %p.",
                           timeout, remoteDeviceAddress, channelHandle));
        }

        // Check again here for ER_SOCK_OTHER_END_CLOSED because it could have changed during
        // our wait.
        if (ER_SOCK_OTHER_END_CLOSED == connectionStatus) {
            returnValue = ER_SOCK_OTHER_END_CLOSED;
        }

        if (ER_OK == returnValue) {
            message.messageData.read.channelHandle = channelHandle;
            message.messageData.read.bytesOfData = reqBytes;

            // This will be set after the read if there is more data
            dataAvailable.ResetEvent();

            bool result = btAccessor->DeviceIo(&message, sizeof(message), buf,
                                               reqBytes, &bytesReturned);

            if (result) {
                // NOTE: this->sourceBytesWaiting is ONLY changed by the kernel via
                // a message handled by HandleReadReady().
                actualBytes = bytesReturned;
            } else {
                // This should not be required. The caller should close this endpoint. If it does
                // not and the SetEvent() is not done then this endpoint is left in a bad state should
                // the kernel become available again for some reason. This is a very unlikely event
                // but it does no harm.
                dataAvailable.SetEvent();
                returnValue = connectionStatus = ER_OS_ERROR;
                QCC_LogError(returnValue,
                             ("PullBytes() was unable to contact the kernel! Error = 0x%08X",
                              ::GetLastError()));
                btAccessor->DebugDumpKernelState();
            }
        }
    } while (ER_OK == returnValue && 0 == actualBytes && remainingTime > 0);

Error:
    return returnValue;
}

void WindowsBTStream::SetSourceBytesWaiting(size_t bytesWaiting, QStatus status)
{
    sourceBytesWaiting = bytesWaiting;
    connectionStatus = status;

    // If the other end has closed then set the event so the daeman knows to get this info.
    if (bytesWaiting > 0 || ER_SOCK_OTHER_END_CLOSED == status) {
        QStatus status = dataAvailable.SetEvent();

        if (ER_OK != status) {
            QCC_LogError(status, ("SetEvent() failed Error = 0x%08X", ::GetLastError()));
        }
    } else {
        QStatus status = dataAvailable.ResetEvent();

        if (ER_OK != status) {
            QCC_LogError(status, ("ResetEvent() failed Error = 0x%08X", ::GetLastError()));
        }
    }
}

}; /* namespace */
