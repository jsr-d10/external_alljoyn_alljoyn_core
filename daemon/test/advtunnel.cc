/**
 * @file
 * Bi-directional tunnel for forwarding alljoyn advertisements between subnets via TCP
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

#include <assert.h>

#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include <vector>
#include <map>

#include <qcc/platform.h>
#include <qcc/Debug.h>
#include <qcc/String.h>
#include <qcc/Socket.h>
#include <qcc/SocketStream.h>
#include <qcc/IPAddress.h>
#include <qcc/StringUtil.h>

#include <Callback.h>
#include <NameService.h>
#include <Transport.h>

#include <Status.h>

#define QCC_MODULE "ALLJOYN"

using namespace ajn;

static NameService* g_ns;

/*
 * Name service configuration parameters. These need to match up with the ones used by AllJoyn.
 */
const char* IPV4_MULTICAST_GROUP = "239.255.37.41";
const uint16_t MULTICAST_PORT = 9956;
const char* IPV6_MULTICAST_GROUP = "ff03::efff:2529";

/* Default tunnel port, override with the -p option */
const uint16_t TUNNEL_PORT = 9973;

/** Signal handler */
static void SigIntHandler(int sig)
{
    if (g_ns) {
        NameService* ns = g_ns;
        g_ns = NULL;
        ns->Stop();
    }
}

class AdvTunnel {
  public:

    AdvTunnel() : stream(NULL) { }

    static const uint16_t ADV_VERSION = 1;
    static const uint32_t ADV_ID = 0xBEBE0000;

    QStatus VersionExchange();

    QStatus Connect(qcc::String address, uint16_t port);

    QStatus Listen(uint16_t port);

    QStatus RelayAdv();

    void Found(const qcc::String& busAddr, const qcc::String& guid, std::vector<qcc::String>& nameList, uint8_t timer);

    QStatus PullString(qcc::String& str)
    {
        char buffer[256];
        char* buf = buffer;
        size_t pulled;
        uint8_t len;

        QStatus status = stream->PullBytes(&len, 1, pulled);
        while ((status == ER_OK) && len) {
            status = stream->PullBytes(buf, len, pulled);
            if (status == ER_OK) {
                str.append(buf, pulled);
                buf += pulled;
                len -= pulled;
            }
        }
        return status;
    }

    QStatus PullInt(uint32_t& num)
    {
        qcc::String val;
        QStatus status = PullString(val);
        if (status == ER_OK) {
            num = qcc::StringToU32(val, 10, -1);
            if (num == (uint32_t)-1) {
                status = ER_INVALID_DATA;
            }
        }
        return status;
    }

    QStatus PushString(const qcc::String& str)
    {
        size_t pushed;
        uint8_t len = str.size();

        QStatus status = stream->PushBytes(&len, 1, pushed);
        if (status == ER_OK) {
            status = stream->PushBytes(str.c_str(), len, pushed);
        }
        return status;
    }

    QStatus PushInt(uint32_t num)
    {
        qcc::String val = qcc::U32ToString(num, 10);
        return PushString(val);
    }

    qcc::SocketStream* stream;
    /*
     * Maps from guid to name service
     */
    std::map<qcc::String, NameService*> nsRelay;
};

QStatus AdvTunnel::VersionExchange()
{
    QStatus status = PushInt(ADV_VERSION | ADV_ID);
    if (status == ER_OK) {
        uint32_t version;
        status = PullInt(version);
        if (status == ER_OK) {
            if (version != (ADV_VERSION | ADV_ID)) {
                printf("version mismatch expected %d got %d\n", version, version & ~ADV_ID);
                status = ER_INVALID_DATA;
            }
        }
    }
    return status;
}

QStatus AdvTunnel::Connect(qcc::String address, uint16_t port)
{
    qcc::IPAddress addr(address);
    qcc::SocketFd sock;
    QStatus status = qcc::Socket(qcc::QCC_AF_INET, qcc::QCC_SOCK_STREAM, sock);
    if (status != ER_OK) {
        QCC_LogError(status, ("Failed to create connect socket"));
        return status;
    }
    while (status == ER_OK) {
        status = qcc::Connect(sock, addr, port);
        if (status == ER_OK) {
            break;
        }
        if (status == ER_CONN_REFUSED) {
            qcc::Sleep(5);
            status = ER_OK;
        }
    }
    if (status != ER_OK) {
        qcc::Close(sock);
    } else {
        printf("Connected to advertisement relay\n");
        stream = new qcc::SocketStream(sock);
        status = VersionExchange();
    }
    return status;
}

QStatus AdvTunnel::Listen(uint16_t port)
{
    qcc::IPAddress wildcard("0.0.0.0");
    qcc::SocketFd listenSock;
    qcc::SocketFd sock;
    QStatus status = qcc::Socket(qcc::QCC_AF_INET, qcc::QCC_SOCK_STREAM, listenSock);
    if (status != ER_OK) {
        QCC_LogError(status, ("Failed to create listen socket"));
        return status;
    }
    status = qcc::Bind(listenSock, wildcard, port);
    if (status != ER_OK) {
        QCC_LogError(status, ("Failed bind listen socket"));
        qcc::Close(listenSock);
        return status;
    }
    status = qcc::Listen(listenSock, 0);
    if (status == ER_OK) {
        status = qcc::SetBlocking(listenSock, false);
    }
    if (status == ER_OK) {
        qcc::IPAddress addr;
        status = qcc::Accept(listenSock, addr, port, sock);
        if (status == ER_WOULDBLOCK) {
            qcc::Event ev(listenSock, qcc::Event::IO_READ, false);
            status = qcc::Event::Wait(ev, qcc::Event::WAIT_FOREVER);
            if (ER_OK == status) {
                status = qcc::Accept(listenSock, addr, port, sock);
            }
        }
        if (status == ER_OK) {
            printf("Accepted advertisement relay\n");
            stream = new qcc::SocketStream(sock);
            status = VersionExchange();
        }
    }
    qcc::Close(listenSock);
    return status;
}

QStatus AdvTunnel::RelayAdv()
{
    QStatus status;

    qcc::String busAddr;
    status = PullString(busAddr);
    if (status != ER_OK) {
        return status;
    }

    qcc::String guid;
    status = PullString(guid);
    if (status != ER_OK) {
        return status;
    }

    uint32_t count;
    status = PullInt(count);
    std::vector<qcc::String> nameList;
    for (size_t i = 0; (status == ER_OK) && (i < count); ++i) {
        qcc::String name;
        status = PullString(name);
        if (status == ER_OK) {
            nameList.push_back(name);
        }
    }
    if (status != ER_OK) {
        return status;
    }

    uint32_t timer;
    status = PullInt(timer);
    if (status != ER_OK) {
        return status;
    }

    printf("Relaying %d names at %s timer=%d\n", (int)nameList.size(), busAddr.c_str(), timer);
    for (size_t i = 0; i < nameList.size(); ++i) {
        printf("   %s\n", nameList[i].c_str());
    }
    /*
     * Lookup or create a name service for relaying advertisements for this quid
     */
    NameService* ns;
    if (nsRelay.count(guid) == 0) {
        ns = new NameService();
        status = ns->Init(guid, true, true);
        if (status != ER_OK) {
            delete ns;
            return status;
        }
        nsRelay[guid] = ns;
        /*
         * Parse out address and set it on the name service
         */
        std::map<qcc::String, qcc::String> argMap;
        QStatus status = Transport::ParseArguments("tcp", busAddr.c_str(), argMap);
        if (status == ER_OK) {
            qcc::IPAddress addr(argMap["addr"]);
            uint16_t port = static_cast<uint16_t>(qcc::StringToU32(argMap["port"]));
            if (addr.IsIPv4()) {
                status = ns->SetEndpoints(addr.ToString(), "", (uint16_t)port);
            } else {
                status = ns->SetEndpoints("", addr.ToString(), (uint16_t)port);
            }
            if (status == ER_OK) {
                ns->OpenInterface("*");
            }
        }
    } else {
        ns = nsRelay[guid];
    }
    if (timer) {
        status = ns->Advertise(nameList);
        if (status != ER_OK) {
            QCC_LogError(status, ("Failed to advertise relayed names"));
        }
    } else {
        status = ns->Cancel(nameList);
        if (status != ER_OK) {
            QCC_LogError(status, ("Failed to cancel relayed names"));
        }
    }
    /*
     * If nothing is being advertised for this guid we don't need this name service any more.
     */
    if (ns->NumAdvertisements() == 0) {
        printf("Removing unused name server\n");
        nsRelay.erase(guid);
        delete ns;
    }

    return status;
}

void AdvTunnel::Found(const qcc::String& busAddr, const qcc::String& guid, std::vector<qcc::String>& nameList, uint8_t timer)
{
    QStatus status;

    /*
     * We don't want to re-relay names that we are advertising.
     */
    if (nsRelay.count(guid) != 0) {
        return;
    }

    printf("Found %d names at %s timer=%d\n", (int)nameList.size(), busAddr.c_str(), timer);
    for (size_t i = 0; i < nameList.size(); ++i) {
        printf("   %s\n", nameList[i].c_str());
    }
    status = PushString(busAddr);
    if (status != ER_OK) {
        goto Exit;
    }
    status = PushString(guid);
    if (status != ER_OK) {
        goto Exit;
    }
    status = PushInt((uint32_t)nameList.size());
    for (size_t i = 0; i < nameList.size(); ++i) {
        status = PushString(nameList[i]);
    }
    if (status != ER_OK) {
        goto Exit;
    }
    status = PushInt(timer);
Exit:
    if (status != ER_OK) {
        printf("Failed to push found names into socket stream\n");
        /*
         * TODO - May need a way to shut things down
         */
    }
}

static void usage(void)
{
    printf("Usage: advtunnel [-p <port>] ([-h] -l | -c <addr>)\n\n");
    printf("Options:\n");
    printf("   -h                    = Print this help message\n");
    printf("   -p                    = Port to connect or listen on\n");
    printf("   -l                    = Listen mode\n");
    printf("   -c <addr>             = Connect mode and address to connect to\n");
}

int main(int argc, char** argv)
{
    QStatus status;
    NameService ns;
    AdvTunnel tunnel;
    bool listen = false;
    qcc::String addr;
    uint16_t port =  TUNNEL_PORT;

    /* Install SIGINT handler */
    signal(SIGINT, SigIntHandler);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-l") == 0) {
            listen = true;
            continue;
        }
        if (strcmp(argv[i], "-p") == 0) {
            if (++i == argc) {
                printf("Missing port number\n");
                usage();
                exit(1);
            }
            port = (uint16_t) qcc::StringToU32(argv[i]);
            continue;
        }
        if (strcmp(argv[i], "-c") == 0) {
            if (++i == argc) {
                printf("Missing connect address\n");
                usage();
                exit(1);
            }
            addr = argv[i];
            continue;
        }
        if (strcmp(argv[i], "-h") == 0) {
            usage();
            continue;
        }
        printf("Unknown option\n");
        usage();
        exit(1);
    }
    if ((!listen && (addr.size() == 0)) || (listen && (addr.size() != 0))) {
        usage();
        exit(1);
    }

    g_ns = &ns;

    ns.SetCallback(new CallbackImpl<AdvTunnel, void, const qcc::String&, const qcc::String&, std::vector<qcc::String>&, uint8_t>
                       (&tunnel, &AdvTunnel::Found));

    while (g_ns) {
        if (listen) {
            status = tunnel.Listen(port);
        } else {
            status = tunnel.Connect(addr, port);
        }
        if (status != ER_OK) {
            printf("Failed to establish relay: %s\n", QCC_StatusText(status));
            ns.Stop();
            break;
        }

        printf("Relay established\n");

        qcc::String guid = "0000000000000000000000000000";
        ns.Init(guid, true, true);

        ns.OpenInterface("*");
        ns.Locate("");

        printf("Start relay\n");

        /*
         * Loop reading and rebroadcasting advertisements.
         */
        while (status == ER_OK) {
            status = tunnel.RelayAdv();
        }
    }
    ns.Join();

    return 0;
}
