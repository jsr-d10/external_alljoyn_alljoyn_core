#ifndef _ALLJOYN_AUTHMECHDBUSSOOKIESHA1_H
#define _ALLJOYN_AUTHMECHDBUSCOOKIESHA1_H
/**
 * @file
 * This file defines the class for the DBUS Cookie SHA1 authentication method
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

#ifndef __cplusplus
#error Only include AuthMechDBusCookieSHA1.h in C++ code.
#endif

#include <qcc/platform.h>
#include <qcc/String.h>
#include "AuthMechanism.h"

namespace ajn {

/**
 * Derived class for the DBUS Cookie SHA1 authentication method
 */
class AuthMechDBusCookieSHA1 : public AuthMechanism {
  public:

    /**
     * Returns the static name for this authentication method
     *
     * @return string "DBUS_COOKIE_SHA1"
     *
     * @see AuthMechDBusCookieSHA1::GetName
     */
    static const char* AuthName() { return "DBUS_COOKIE_SHA1"; }

    /**
     * Returns the name for this authentication method
     *
     * returns the same result as \b AuthMechAuthMechDBusCookieSHA1::AuthName
     *
     * @return the static name for the DBus cookie SHA1 authentication mechanism.
     */
    const char* GetName() { return AuthName(); }

    /**
     * Function of type AuthMechanismManager::AuthMechFactory
     *
     * @param keyStore   The key store avaiable to this authentication mechansim.
     * @param listener   The listener to register (listener is not used by AuthMechDBusCookieSHA1)
     *
     * @return  An object of class AuthMechDBusCookieSHA1
     */
    static AuthMechanism* Factory(KeyStore& keyStore, ProtectedAuthListener& listener) { return new AuthMechDBusCookieSHA1(keyStore, listener); }

    /**
     * Initial response from this client which in this case is the current user name
     *
     * @param result  Returns:
     *                - ALLJOYN_AUTH_OK        if the authentication is complete,
     *                - ALLJOYN_AUTH_CONTINUE  if the authentication is incomplete
     *
     * @return the user name set by the environment variable "USERNAME".
     */
    qcc::String InitialResponse(AuthResult& result);

    /**
     * Client's response to a challenge from the server
     * @param challenge String representing the challenge provided by the server
     * @param result    Returns:
     *                      - ALLJOYN_AUTH_OK  if the authentication is complete,
     *                      - ALLJOYN_AUTH_ERROR    if the response was rejected.
     *
     * @return a string that is in response to the challenge string provided by
     *         the server. This string is not expected to be human readable.
     */
    qcc::String Response(const qcc::String& challenge, AuthResult& result);

    /**
     * Server's challenge to be sent to the client
     *
     * @param response Response used by server
     * @param result Returns:
     *                   - ALLJOYN_AUTH_OK  if the authentication is complete,
     *                   - ALLJOYN_AUTH_CONTINUE if the authentication is incomplete,
     *                   - ALLJOYN_AUTH_ERROR    if the response was rejected.
     * @return a string designed to be used as the the challenge string
     *         when calling \b AuthMechDBusCookieSHA1::Response.
     *         This string is not expected to be human readable.
     */
    qcc::String Challenge(const qcc::String& response, AuthResult& result);

  private:

    AuthMechDBusCookieSHA1(KeyStore& keyStore, ProtectedAuthListener& listener) : AuthMechanism(keyStore, listener) { }

    qcc::String userName;
    qcc::String cookie;
    qcc::String nonce;

};

}

#endif
