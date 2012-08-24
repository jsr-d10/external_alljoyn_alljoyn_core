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

#include "SocketStreamEvent.h"

/** @internal */
#define QCC_MODULE "SOCKETSTREAM_EVENT"

namespace AllJoyn {

SocketStreamEvent::SocketStreamEvent(SocketStream ^ sock)
{
    DataReceived += ref new SocketStreamDataReceivedHandler([&]() {
                                                                DefaultSocketStreamDataReceivedHandler();
                                                            });

    sock->_sockfd->SocketEventsChanged += ref new qcc::winrt::SocketWrapperEventsChangedHandler([&](Platform::Object ^ source, int events) {
                                                                                                    SocketEventsChangedHandler(source, events);
                                                                                                });
}

SocketStreamEvent::~SocketStreamEvent()
{
}

void SocketStreamEvent::DefaultSocketStreamDataReceivedHandler()
{
}

void SocketStreamEvent::SocketEventsChangedHandler(Platform::Object ^ source, int events)
{
    if (events & (int)qcc::winrt::Events::Read) {
        DataReceived();
    }
}

}
