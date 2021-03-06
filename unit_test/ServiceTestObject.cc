/**
 * @file
 * Sample implementation of an AllJoyn service for test harness.
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

#include "ServiceTestObject.h"

#define QCC_MODULE "ALLJOYN"

using namespace std;
using namespace qcc;
using namespace ajn;


ServiceTestObject::ServiceTestObject(BusAttachment& bus, const char*path) : BusObject(bus, path)
{


}

void ServiceTestObject::RegisterForNameAcquiredSignals()
{
    QStatus status = ER_OK;
    const InterfaceDescription* intf = bus.GetInterface("org.freedesktop.DBus");
    assert(intf);
    /* register the signal handler for the the 'NameAcquired' signal */
    status =  bus.RegisterSignalHandler(this,
                                        static_cast<MessageReceiver::SignalHandler>(&ServiceTestObject::NameAcquiredSignalHandler),
                                        intf->GetMember("NameAcquired"),
                                        NULL);
    if (status != ER_OK) {
        QCC_LogError(status, ("Problem while registering name Acquired signal handler"));
    }

}

void ServiceTestObject::PopulateSignalMembers(const char*interface_name) {
    /* Register the signal handler with the bus */
    const InterfaceDescription* regTestIntf = bus.GetInterface(interface_name);
    assert(regTestIntf);
    my_signal_member = regTestIntf->GetMember("my_signal");
    assert(my_signal_member);
}

QStatus ServiceTestObject::InstallMethodHandlers(const char*interface_name) {
    const InterfaceDescription* regTestIntf = bus.GetInterface(interface_name);
    assert(regTestIntf);
    /* Register the method handlers with the object */
    const MethodEntry methodEntries[] = {
        { regTestIntf->GetMember("my_ping"), static_cast<MessageReceiver::MethodHandler>(&ServiceTestObject::Ping) },
        { regTestIntf->GetMember("my_sing"), static_cast<MessageReceiver::MethodHandler>(&ServiceTestObject::Sing) },
        { regTestIntf->GetMember("my_king"), static_cast<MessageReceiver::MethodHandler>(&ServiceTestObject::King) },
        { regTestIntf->GetMember("ByteArrayTest"), static_cast<MessageReceiver::MethodHandler>(&ServiceTestObject::ByteArrayTest) },
        { regTestIntf->GetMember("DoubleArrayTest"), static_cast<MessageReceiver::MethodHandler>(&ServiceTestObject::DoubleArrayTest) }

    };

    QStatus status = AddMethodHandlers(methodEntries, ArraySize(methodEntries));
    return status;
}


QStatus ServiceTestObject::AddInterfaceToObject(const InterfaceDescription* intf) {
    assert(intf);
    QStatus status = AddInterface(*intf);
    return status;
}


void ServiceTestObject::NameAcquiredSignalHandler(const InterfaceDescription::Member*member,
                                                  const char* sourcePath,
                                                  Message& msg) {
    //QCC_SyncPrintf("Inside the Name Acquired  signal handler\n");
    output1++;
}


void ServiceTestObject::ObjectRegistered(void) {
    BusObject::ObjectRegistered();
    output1++;
}


void ServiceTestObject::Ping(const InterfaceDescription::Member* member, Message& msg)
{

    MsgArg arg(*(msg->GetArg(0)));
    printf("Pinged with: %s\n", msg->GetArg(0)->ToString().c_str());
    QStatus status = MethodReply(msg, &arg, 1);
    if (ER_OK != status) {
        QCC_LogError(status, ("Ping: Error sending reply"));
    }

    //Emit a signal
    MsgArg arg1("s", "Signal Emitted");
    status = Signal(NULL, 0, *my_signal_member, &arg1, 1, 0, 1);
    printf("\n Signal Sent");

}

void ServiceTestObject::Sing(const InterfaceDescription::Member* member, Message& msg)
{
    /* Reply with same string that was sent to us */
    MsgArg arg(*(msg->GetArg(0)));
    printf("Sung with: %s\n", msg->GetArg(0)->ToString().c_str());
    QStatus status = MethodReply(msg, &arg, 1);
    if (ER_OK != status) {
        QCC_LogError(status, ("Sing: Error sending reply"));
    }

}

void ServiceTestObject::King(const InterfaceDescription::Member* member, Message& msg)
{
    /* Reply with same string that was sent to us */
    MsgArg arg(*(msg->GetArg(0)));
    printf("King with: %s\n", msg->GetArg(0)->ToString().c_str());
    QStatus status = MethodReply(msg, &arg, 1);
    if (ER_OK != status) {
        QCC_LogError(status, ("King: Error sending reply"));
    }

    uint8_t flags = 0;
    flags |= ALLJOYN_FLAG_GLOBAL_BROADCAST;

    //Emit a signal
    MsgArg arg1("s", "Signal1 Emitted : MethodCall King");
    status = Signal(NULL, 0, *my_signal_member, &arg1, 1, 0, flags);
    printf("\n Signal Sent : MethodCall King");

    arg1.Set("s", "Signal2 Emitted : MethodCall King");
    status = Signal(NULL, 0, *my_signal_member, &arg1, 1, 0);
    printf("\n Signal Sent : MethodCall King");

    arg1.Set("s", "Signal3 Emitted : MethodCall King");
    status = Signal(NULL, 0, *my_signal_member, &arg1, 1, 0, flags);
    printf("\n Signal Sent : MethodCall King");


}

void ServiceTestObject::ByteArrayTest(const InterfaceDescription::Member* member, Message& msg)
{
    MsgArg arg(*(msg->GetArg(0)));
    QStatus status = MethodReply(msg, &arg, 1);
    if (ER_OK != status) {
        QCC_LogError(status, ("Error sending reply"));
    }
}

void ServiceTestObject::DoubleArrayTest(const InterfaceDescription::Member* member, Message& msg)
{
    MsgArg arg(*(msg->GetArg(0)));
    QStatus status = MethodReply(msg, &arg, 1);
    if (ER_OK != status) {
        QCC_LogError(status, ("Error sending reply"));
    }
}




QStatus ServiceTestObject::Get(const char*ifcName, const char*propName, MsgArg& val)
{
    QStatus status = ER_OK;
    if (0 == strcmp("int_val", propName)) {
        //val.Set("i", prop_int_val);
        val.typeId = ALLJOYN_INT32;
        val.v_int32 = prop_int_val;
    } else if (0 == strcmp("str_val", propName)) {
        //val.Set("s", prop_str_val.c_str());
        val.typeId = ALLJOYN_STRING;
        val.v_string.str = prop_str_val.c_str();
        val.v_string.len = prop_str_val.size();
    } else if (0 == strcmp("ro_str", propName)) {
        //val.Set("s", prop_ro_str_val.c_str());
        val.typeId = ALLJOYN_STRING;
        val.v_string.str = prop_ro_str.c_str();
        val.v_string.len = prop_ro_str.size();
    } else {
        status = ER_BUS_NO_SUCH_PROPERTY;
    }
    return status;
}

QStatus ServiceTestObject::Set(const char*ifcName, const char*propName, MsgArg& val)
{
    QStatus status = ER_OK;
    if ((0 == strcmp("int_val", propName)) && (val.typeId == ALLJOYN_INT32)) {
        prop_int_val = val.v_int32;
    } else if ((0 == strcmp("str_val", propName)) && (val.typeId == ALLJOYN_STRING)) {
        prop_str_val = val.v_string.str;
    } else if (0 == strcmp("ro_str", propName)) {
        status = ER_BUS_PROPERTY_ACCESS_DENIED;
    } else {
        status = ER_BUS_NO_SUCH_PROPERTY;
    }
    return status;
}

