#ifndef _ALLJOYN_METHODTABLE_H
#define _ALLJOYN_METHODTABLE_H
/**
 * @file
 * This file defines the method hash table class
 */

/******************************************************************************
 * Copyright 2009-2012, Qualcomm Innovation Center, Inc.
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

#include <vector>

#include <qcc/String.h>
#include <qcc/Mutex.h>

#include <alljoyn/BusObject.h>
#include <alljoyn/InterfaceDescription.h>
#include <alljoyn/MessageReceiver.h>

#include <Status.h>

#include <qcc/STLContainer.h>
//#if defined(__GNUC__) && !defined(ANDROID)
//#include <ext/hash_map>
//namespace std {
//using namespace __gnu_cxx;
//}
//#else
//#include <hash_map>
//#endif

namespace ajn {

/**
 * %MethodTable is a hash table that maps object paths to BusObject instances.
 */
class MethodTable {

  public:

    /**
     * Type definition for a method hash table entry
     */
    struct Entry {
        /**
         * Construct an Entry
         */
        Entry(BusObject* object,
              MessageReceiver::MethodHandler handler,
              const InterfaceDescription::Member* member,
              void* context)
            : object(object), handler(handler), member(member), context(context), ifaceStr(member->iface->GetName()), methodStr(member->name) { }

        /**
         * Construct an empty Entry.
         */
        Entry(void) : object(NULL), handler(), ifaceStr(), methodStr() { }

        BusObject* object;                             /**<  BusObject instance*/
        MessageReceiver::MethodHandler handler;        /**<  Handler for method */
        const InterfaceDescription::Member* member;    /**<  Member that handler implements  */
        void* context;                                 /**<  Optional context provided when handler was registered */
        qcc::String ifaceStr;                          /**<  Interface string */
        qcc::String methodStr;                         /**<  Method string */
    };

    /**
     * Destructor
     */
    ~MethodTable();

    /**
     * Add an entry to the method hash table.
     *
     * @param object     Object instance.
     * @param func       Handler for method.
     * @param member     Member that func implements.
     */
    void Add(BusObject* object,
             MessageReceiver::MethodHandler func,
             const InterfaceDescription::Member* member,
             void* context = NULL);

    /**
     * Find an Entry based on set of criteria.
     *
     * @param objectPath   The object path.
     * @param iface        The interface.
     * @param methodName   The method name.
     * @return
     *      - Entry that matches objectPath, interface and method
     *      - NULL if not found
     */
    const Entry* Find(const char* objectPath, const char* iface, const char* methodName);

    /**
     * Remove all hash entries related to the specified object.
     *
     * @param object   Object whose method table entries are to be removed.
     */
    void RemoveAll(BusObject* object);

    /**
     * Register handlers for an object's methods.
     *
     * @param object  Object whose methods are to be registered.
     */
    void AddAll(BusObject* object);

  private:

    qcc::Mutex lock; /**< Lock protecting the method table */

    /**
     * Type definition for method hash table key
     */
    class Key {
      public:
        const char* objPath;
        const char* iface;
        const char* methodName;
        Key(const char* obj, const char* ifc, const char* method) : objPath(obj), iface((ifc && *ifc) ? ifc : NULL), methodName(method) { }
    };

    /**
     * Hash functor
     */
    struct Hash {
        /** Calculate hash for Key k  */
        size_t operator()(const Key& k) const {
            size_t hash = 37;
            for (const char* p = k.methodName; *p; ++p) {
                hash = *p + hash * 11;
            }
            for (const char* p = k.objPath; *p; ++p) {
                hash = *p + hash * 5;
            }
            if (k.iface) {
                for (const char* p = k.iface; *p; ++p) {
                    hash += *p * 7;
                }
            }
            return hash;
        }
    };

    /**
     * Functor for testing 2 keys for equality
     */
    struct Equal {
        /**
         * Return true two keys are equal
         */
        bool operator()(const Key& k1, const Key& k2) const {
            if ((k1.iface == NULL) || (k2.iface == NULL)) {
                return (k1.iface == k2.iface) && (strcmp(k1.methodName, k2.methodName) == 0) && (strcmp(k1.objPath, k2.objPath) == 0);
            } else {
                return (strcmp(k1.methodName, k2.methodName) == 0) && (strcmp(k1.iface, k2.iface) == 0) && (strcmp(k1.objPath, k2.objPath) == 0);
            }
        }
    };

    /** The hash table */
    unordered_map<Key, Entry*, Hash, Equal> hashTable;
};

}

#endif
