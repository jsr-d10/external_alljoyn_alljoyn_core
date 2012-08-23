/**
 * @file
 *
 * This file implements the InterfaceDescription class
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
#include <qcc/String.h>
#include <qcc/StringMapKey.h>
#include <map>
#include <alljoyn/AllJoynStd.h>
#include <Status.h>

#include "SignatureUtils.h"

#define QCC_MODULE "ALLJOYN"

using namespace std;
using namespace qcc;

namespace ajn {

static qcc::String NextArg(const char*& signature, qcc::String& argNames, bool inOut, size_t indent)
{
    qcc::String in(indent, ' ');
    qcc::String arg = in + "<arg";
    qcc::String argType;
    const char* start = signature;
    SignatureUtils::ParseCompleteType(signature);
    size_t len = signature - start;
    argType.append(start, len);

    if (!argNames.empty()) {
        size_t pos = argNames.find_first_of(',');
        arg += " name=\"" + argNames.substr(0, pos) + "\"";
        if (pos == qcc::String::npos) {
            argNames.clear();
        } else {
            argNames.erase(0, pos + 1);
        }
    }
    arg += " type=\"" + argType + "\" direction=\"";
    arg += inOut ? "in\"/>\n" : "out\"/>\n";
    return arg;
}


class InterfaceDescription::AnnotationsMap : public std::map<qcc::String, qcc::String> { };


InterfaceDescription::Member::Member(
    const InterfaceDescription* iface,
    AllJoynMessageType type,
    const char* name,
    const char* signature,
    const char* returnSignature,
    const char* argNames,
    uint8_t annotation,
    const char* accessPerms)
    : iface(iface),
    memberType(type),
    name(name),
    signature(signature ? signature : ""),
    returnSignature(returnSignature ? returnSignature : ""),
    argNames(argNames ? argNames : ""),
    annotations(),
    accessPerms(accessPerms ? accessPerms : "") {

    if (annotation & MEMBER_ANNOTATE_DEPRECATED) {
        (*annotations)[org::freedesktop::DBus::AnnotateDeprecated] = "true";
    }

    if (annotation & MEMBER_ANNOTATE_NO_REPLY) {
        (*annotations)[org::freedesktop::DBus::AnnotateNoReply] = "true";
    }
}

bool InterfaceDescription::Member::operator==(const Member& o) const {
    return ((memberType == o.memberType) && (name == o.name) && (signature == o.signature)
            && (returnSignature == o.returnSignature) && (annotations == o.annotations));
}


InterfaceDescription::Property::Property(const char* name, const char* signature, uint8_t access)
    : name(name), signature(signature ? signature : ""), access(access), annotations()
{
}

/** Equality */
bool InterfaceDescription::Property::operator==(const Property& o) const {
    return ((name == o.name) && (signature == o.signature) && (access == o.access) && (annotations == o.annotations));
}


struct InterfaceDescription::Definitions {
    typedef std::map<qcc::StringMapKey, Member> MemberMap;
    typedef std::map<qcc::StringMapKey, Property> PropertyMap;

    MemberMap members;              /**< Interface members */
    PropertyMap properties;         /**< Interface properties */
    AnnotationsMap annotations;     /**< Interface Annotations */

    Definitions() { }
    Definitions(const MemberMap& m, const PropertyMap& p, const AnnotationsMap& a) :
        members(m), properties(p), annotations(a) { }
};

bool InterfaceDescription::Member::GetAnnotation(const qcc::String& name, qcc::String& value) const
{
    AnnotationsMap::const_iterator it = annotations->find(name);
    return (it != annotations->end() ? value = it->second, true : false);
}

InterfaceDescription::InterfaceDescription(const char* name, bool secure) :
    defs(new Definitions),
    name(name),
    isActivated(false)
{
    if (secure) {
        defs->annotations[org::alljoyn::Bus::Secure] = "true";
    }
}

InterfaceDescription::~InterfaceDescription()
{
    delete defs;
}

InterfaceDescription::InterfaceDescription(const InterfaceDescription& other) :
    defs(new Definitions(other.defs->members, other.defs->properties, other.defs->annotations)),
    name(other.name),
    isActivated(false)
{
    /* Update the iface pointer in each member */
    Definitions::MemberMap::iterator mit = defs->members.begin();
    while (mit != defs->members.end()) {
        mit++->second.iface = this;
    }
}

InterfaceDescription& InterfaceDescription::operator=(const InterfaceDescription& other)
{
    if (this != &other) {
        name = other.name;
        defs->members = other.defs->members;
        defs->properties = other.defs->properties;
        defs->annotations = other.defs->annotations;

        /* Update the iface pointer in each member */
        Definitions::MemberMap::iterator mit = defs->members.begin();
        while (mit != defs->members.end()) {
            mit++->second.iface = this;
        }
    }
    return *this;
}

bool InterfaceDescription::IsSecure() const
{
    AnnotationsMap::const_iterator it = defs->annotations.find(org::alljoyn::Bus::Secure);
    return (it != defs->annotations.end() && it->second == "true");
}

qcc::String InterfaceDescription::Introspect(size_t indent) const
{
    qcc::String in(indent, ' ');
    const qcc::String close = "\">\n";
    qcc::String xml = in + "<interface name=\"";

    xml += name + close;
    /*
     * Iterate over interface defs->members
     */
    Definitions::MemberMap::const_iterator mit = defs->members.begin();
    while (mit != defs->members.end()) {
        const Member& member = mit->second;
        qcc::String argNames = member.argNames;
        qcc::String mtype = (member.memberType == MESSAGE_METHOD_CALL) ? "method" : "signal";
        xml += in + "  <" + mtype + " name=\"" + member.name + close;

        /* Iterate over IN arguments */
        for (const char* sig = member.signature.c_str(); *sig;) {
            // always treat signals as direction=out
            xml += NextArg(sig, argNames, member.memberType != MESSAGE_SIGNAL, indent + 4);
        }
        /* Iterate over OUT arguments */
        for (const char* sig = member.returnSignature.c_str(); *sig;) {
            xml += NextArg(sig, argNames, false, indent + 4);
        }
        /*
         * Add annotations
         */

        AnnotationsMap::const_iterator ait = member.annotations->begin();
        for (; ait != member.annotations->end(); ++ait) {
            xml += in + "    <annotation name=\"" + ait->first.c_str() + "\" value=\"" + ait->second + "\"/>\n";
        }

        xml += in + "  </" + mtype + ">\n";
        ++mit;
    }
    /*
     * Iterate over interface properties
     */
    Definitions::PropertyMap::const_iterator pit = defs->properties.begin();
    while (pit != defs->properties.end()) {
        const Property& property = pit->second;
        xml += in + "  <property name=\"" + property.name + "\" type=\"" + property.signature + "\"";
        if (property.access == PROP_ACCESS_READ) {
            xml += " access=\"read\"";
        } else if (property.access == PROP_ACCESS_WRITE) {
            xml += " access=\"write\"";
        } else {
            xml += " access=\"readwrite\"";
        }

        if (property.annotations->size()) {
            xml += ">\n";

            // add annotations
            AnnotationsMap::const_iterator ait = property.annotations->begin();
            for (; ait != property.annotations->end(); ++ait) {
                xml += in + "    <annotation name=\"" + ait->first.c_str() + "\" value=\"" + ait->second + "\"/>\n";
            }

            xml += in + "  </property>\n";
        } else {
            xml += "/>\n";
        }

        ++pit;
    }

    // add interface annotations
    AnnotationsMap::const_iterator ait = defs->annotations.begin();
    for (; ait != defs->annotations.end(); ++ait) {
        xml += in + "  <annotation name=\"" + ait->first.c_str() + "\" value=\"" + ait->second + "\"/>\n";
    }

    xml += in + "</interface>\n";
    return xml;
}

QStatus InterfaceDescription::AddMember(AllJoynMessageType type,
                                        const char* name,
                                        const char* inSig,
                                        const char* outSig,
                                        const char* argNames,
                                        uint8_t annotation,
                                        const char* accessPerms)
{
    if (isActivated) {
        return ER_BUS_INTERFACE_ACTIVATED;
    }

    StringMapKey key = qcc::String(name);
    Member member(this, type, name, inSig, outSig, argNames, annotation, accessPerms);
    pair<StringMapKey, Member> item(key, member);
    pair<Definitions::MemberMap::iterator, bool> ret = defs->members.insert(item);
    return ret.second ? ER_OK : ER_BUS_MEMBER_ALREADY_EXISTS;
}

QStatus InterfaceDescription::AddMemberAnnotation(const char* member, const qcc::String& name, const qcc::String& value)
{
    if (isActivated) {
        return ER_BUS_INTERFACE_ACTIVATED;
    }

    Definitions::MemberMap::iterator it = defs->members.find(StringMapKey(member));
    if (it == defs->members.end()) {
        return ER_BUS_INTERFACE_NO_SUCH_MEMBER;
    }

    Member& m = it->second;
    std::pair<AnnotationsMap::iterator, bool> ret = m.annotations->insert(AnnotationsMap::value_type(name, value));
    return (ret.second || (ret.first->first == name && ret.first->second == value)) ? ER_OK : ER_BUS_ANNOTATION_ALREADY_EXISTS;
}

bool InterfaceDescription::GetMemberAnnotation(const char* member, const qcc::String& name, qcc::String& value) const
{
    Definitions::MemberMap::const_iterator mit = defs->members.find(StringMapKey(member));
    if (mit == defs->members.end()) {
        return false;
    }

    const Member& m = mit->second;
    AnnotationsMap::const_iterator ait = m.annotations->find(name);
    return (ait != defs->annotations.end() ? value = ait->second, true : false);
}


QStatus InterfaceDescription::AddProperty(const char* name, const char* signature, uint8_t access)
{
    if (isActivated) {
        return ER_BUS_INTERFACE_ACTIVATED;
    }

    StringMapKey key = qcc::String(name);
    Property prop(name, signature, access);
    pair<StringMapKey, Property> item(key, prop);
    pair<Definitions::PropertyMap::iterator, bool> ret = defs->properties.insert(item);
    return ret.second ? ER_OK : ER_BUS_PROPERTY_ALREADY_EXISTS;
}

QStatus InterfaceDescription::AddPropertyAnnotation(const qcc::String& p_name, const qcc::String& name, const qcc::String& value)
{
    if (isActivated) {
        return ER_BUS_INTERFACE_ACTIVATED;
    }

    Definitions::PropertyMap::iterator pit = defs->properties.find(StringMapKey(p_name));
    if (pit == defs->properties.end()) {
        return ER_BUS_NO_SUCH_PROPERTY;
    }

    Property& property = pit->second;
    std::pair<AnnotationsMap::iterator, bool> ret = property.annotations->insert(AnnotationsMap::value_type(name, value));
    return (ret.second || (ret.first->first == name && ret.first->second == value)) ? ER_OK : ER_BUS_ANNOTATION_ALREADY_EXISTS;
}

bool InterfaceDescription::GetPropertyAnnotation(const qcc::String& p_name, const qcc::String& name, qcc::String& value) const
{
    Definitions::PropertyMap::const_iterator pit = defs->properties.find(StringMapKey(p_name));
    if (pit == defs->properties.end()) {
        return false;
    }

    const Property& property = pit->second;
    AnnotationsMap::const_iterator ait = property.annotations->find(name);
    return (ait != property.annotations->end() ? value = ait->second, true : false);
}

QStatus InterfaceDescription::AddAnnotation(const qcc::String& name, const qcc::String& value)
{
    if (isActivated) {
        return ER_BUS_INTERFACE_ACTIVATED;
    }

    std::pair<AnnotationsMap::iterator, bool> ret = defs->annotations.insert(std::make_pair(name, value));
    return (ret.second || (ret.first->first == name && ret.first->second == value)) ? ER_OK : ER_BUS_ANNOTATION_ALREADY_EXISTS;
}

bool InterfaceDescription::GetAnnotation(const qcc::String& name, qcc::String& value) const
{
    AnnotationsMap::const_iterator it = defs->annotations.find(name);
    return (it != defs->annotations.end() ? value = it->second, true : false);
}

bool InterfaceDescription::operator==(const InterfaceDescription& other) const
{
    if (name != other.name) {
        return false;
    }

    if (defs->members != other.defs->members ||
        defs->properties != other.defs->properties ||
        defs->annotations != other.defs->annotations) {
        return false;
    }

    return true;
}

size_t InterfaceDescription::GetProperties(const Property** props, size_t numProps) const
{
    size_t count = defs->properties.size();
    if (props) {
        count = min(count, numProps);
        Definitions::PropertyMap::const_iterator pit = defs->properties.begin();
        for (size_t i = 0; i < count; i++, pit++) {
            props[i] = &(pit->second);
        }
    }
    return count;
}

const InterfaceDescription::Property* InterfaceDescription::GetProperty(const char* name) const
{
    Definitions::PropertyMap::const_iterator pit = defs->properties.find(qcc::StringMapKey(name));
    return (pit == defs->properties.end()) ? NULL : &(pit->second);
}

size_t InterfaceDescription::GetMembers(const Member** members, size_t numMembers) const
{
    size_t count = defs->members.size();
    if (members) {
        count = min(count, numMembers);
        Definitions::MemberMap::const_iterator mit = defs->members.begin();
        for (size_t i = 0; i < count; i++, mit++) {
            members[i] = &(mit->second);
        }
    }
    return count;
}

const InterfaceDescription::Member* InterfaceDescription::GetMember(const char* name) const
{
    Definitions::MemberMap::const_iterator mit = defs->members.find(qcc::StringMapKey(name));
    return (mit == defs->members.end()) ? NULL : &(mit->second);
}

bool InterfaceDescription::HasMember(const char* name, const char* inSig, const char* outSig)
{
    const Member* member = GetMember(name);
    if (member == NULL) {
        return false;
    } else if ((inSig == NULL) && (outSig == NULL)) {
        return true;
    } else {
        bool found = true;
        if (inSig) {
            found = found && (member->signature.compare(inSig) == 0);
        }
        if (outSig && (member->memberType == MESSAGE_METHOD_CALL)) {
            found = found && (member->returnSignature.compare(outSig) == 0);
        }
        return found;
    }
}


}
