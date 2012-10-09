/******************************************************************************
 *
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
 *
 *****************************************************************************/

#include "SocketStream.h"

#include <algorithm>
#include <qcc/atomic.h>
#include <qcc/IPAddress.h>

#include <qcc/winrt/SocketWrapper.h>
#include <qcc/winrt/SocketsWrapper.h>
#include <qcc/winrt/utility.h>

using namespace Windows::Foundation;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;

/** @internal */
#define QCC_MODULE "SOCKETSTREAM"

namespace AllJoyn {

SocketStream::SocketStream() : _sockfd(nullptr)
{
}

SocketStream::SocketStream(qcc::winrt::SocketWrapper ^ sockfd) : _sockfd(sockfd)
{
    // Change to blocking mode
    sockfd->SetBlocking(true);
}

SocketStream::~SocketStream()
{
    // Cleanup the underlying sockfd
    if (nullptr != _sockfd) {
        _sockfd->Close();
        _sockfd = nullptr;
    }
}

void SocketStream::SocketDup(Platform::WriteOnlyArray<SocketStream ^> ^ dupSocket)
{
    ::QStatus result = ER_FAIL;

    while (true) {
        // Check dupSocket for invalid values
        if (nullptr == dupSocket || dupSocket->Length != 1) {
            result = ER_BAD_ARG_1;
            break;
        }
        if (nullptr != _sockfd) {
            // Allocate temporary SocketWrapper array
            Platform::Array<qcc::winrt::SocketWrapper ^> ^ dup = ref new Platform::Array<qcc::winrt::SocketWrapper ^>(1);
            if (nullptr == dup) {
                result = ER_OUT_OF_MEMORY;
                break;
            }
            // Call the SocketWrapper SocketDup API
            result = (::QStatus)_sockfd->SocketDup(dup);
            if (ER_OK == result) {
                // Create SocketStream
                SocketStream ^ ret = ref new SocketStream(dup[0]);
                if (nullptr == ret) {
                    result = ER_OUT_OF_MEMORY;
                    break;
                }
                // Store the result
                dupSocket[0] = ret;
            }
        }
        break;
    }

    // Bubble up any QStatus errors as an exception
    if (ER_OK != result) {
        QCC_THROW_EXCEPTION(result);
    }
}

void SocketStream::Send(const Platform::Array<uint8> ^ buf, int len, Platform::WriteOnlyArray<int> ^ sent)
{
    ::QStatus result = ER_FAIL;

    if (nullptr != _sockfd) {
        // Call the SocketWrapper Send API
        result = (::QStatus)_sockfd->Send(buf, len, sent);
    }

    // Bubble up any QStatus errors as an exception
    if (ER_OK != result) {
        QCC_THROW_EXCEPTION(result);
    }
}

void SocketStream::Recv(Platform::WriteOnlyArray<uint8> ^ buf, int len, Platform::WriteOnlyArray<int> ^ received)
{
    ::QStatus result = ER_FAIL;

    if (nullptr != _sockfd) {
        // Call teh SocketWrapper Recv API
        result = (::QStatus)_sockfd->Recv(buf, len, received);
    }

    // Bubble up any QStatus errors as an exception
    if (ER_OK != result) {
        QCC_THROW_EXCEPTION(result);
    }
}

bool SocketStream::CanRead()
{
    // Return if can read
    return (_sockfd->GetEvents() & (int)qcc::winrt::Events::Read) != 0;
}

bool SocketStream::CanWrite()
{
    // Return if can write
    return (_sockfd->GetEvents() & (int)qcc::winrt::Events::Write) != 0;
}

void SocketStream::SetBlocking(bool block)
{
    // Change the blocking mode of the sockfd
    _sockfd->SetBlocking(block);
}

}
