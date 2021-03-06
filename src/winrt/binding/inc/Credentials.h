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

#pragma once

#include <alljoyn/AuthListener.h>
#include <qcc/ManagedObj.h>
#include <Status_CPP0x.h>
#include <Status.h>

namespace AllJoyn {

/// <summary>
///Credential indication Bitmasks
///Bitmasks used to indicated what type of credentials are being used.
/// </summary>
[Platform::Metadata::Flags]
public enum class CredentialType : uint32_t {
    /// <summary>Bit 0 indicates credentials include a password, pincode, or passphrase</summary>
    CRED_PASSWORD     = ajn::AuthListener::CRED_PASSWORD,
    /// <summary>Bit 1 indicates credentials include a user name</summary>
    CRED_USER_NAME    = ajn::AuthListener::CRED_USER_NAME,
    /// <summary>Bit 2 indicates credentials include a chain of PEM-encoded X509 certificates</summary>
    CRED_CERT_CHAIN   = ajn::AuthListener::CRED_CERT_CHAIN,
    /// <summary>Bit 3 indicates credentials include a PEM-encoded private key</summary>
    CRED_PRIVATE_KEY  = ajn::AuthListener::CRED_PRIVATE_KEY,
    /// <summary>Bit 4 indicates credentials include a logon entry that can be used to logon a remote user</summary>
    CRED_LOGON_ENTRY  = ajn::AuthListener::CRED_LOGON_ENTRY,
    /// <summary>Bit 5 indicates credentials include an expiration time</summary>
    CRED_EXPIRATION   = ajn::AuthListener::CRED_EXPIRATION,
    /// <summary>Indicates the credential request is for a newly created password</summary>
    /// <remarks>This value is only used in a credential request</remarks>
    CRED_NEW_PASSWORD = ajn::AuthListener::CRED_NEW_PASSWORD,
    /// <summary>Indicates the credential request is for a one time use password</summary>
    /// <remarks>This value is only used in a credential request</remarks>
    CRED_ONE_TIME_PWD = ajn::AuthListener::CRED_ONE_TIME_PWD
};

ref class __Credentials {
  private:
    friend ref class Credentials;
    friend class _Credentials;
    __Credentials();
    ~__Credentials();

    property Platform::String ^ Password;
    property Platform::String ^ UserName;
    property Platform::String ^ CertChain;
    property Platform::String ^ PrivateKey;
    property Platform::String ^ LogonEntry;
    property uint32_t Expiration;
};

class _Credentials : protected ajn::AuthListener::Credentials {
  protected:
    friend class qcc::ManagedObj<_Credentials>;
    friend ref class Credentials;
    friend ref class AuthListener;
    friend class _AuthListener;
    _Credentials();
    ~_Credentials();

    __Credentials ^ _eventsAndProperties;
};

/// <summary>
///Generic class for describing different authentication credentials.
/// </summary>
public ref class Credentials sealed {
  public:
    Credentials();

    /// <summary>
    ///Tests if one or more credentials are set
    /// </summary>
    /// <param name="creds">
    ///A logical or of the credential bit values.
    /// </param>
    /// <returns>
    /// true if the credentials are set
    /// </returns>
    bool IsSet(uint16_t creds);
    /// <summary>
    ///Clear the credentials.
    /// </summary>
    void Clear();

    /// <summary>
    ///Specifies a requested password, pincode, or passphrase for this credentials instance
    /// </summary>
    property Platform::String ^ Password
    {
        Platform::String ^ get();
        void set(Platform::String ^ value);
    }

    /// <summary>
    ///Specifies a requested user name for this credentials instance
    /// </summary>
    property Platform::String ^ UserName
    {
        Platform::String ^ get();
        void set(Platform::String ^ value);
    }

    /// <summary>
    ///Specifies the PEM encoded X509 certificate chain for this credentials instance
    /// </summary>
    property Platform::String ^ CertChain
    {
        Platform::String ^ get();
        void set(Platform::String ^ value);
    }

    /// <summary>
    ///Specifies the PEM encoded private key for this credentials instance
    /// </summary>
    property Platform::String ^ PrivateKey
    {
        Platform::String ^ get();
        void set(Platform::String ^ value);
    }

    /// <summary>
    ///Specifies the logon entry for this credentials instance
    /// </summary>
    property Platform::String ^ LogonEntry
    {
        Platform::String ^ get();
        void set(Platform::String ^ value);
    }

    /// <summary>
    ///Specifies the expiration time in seconds for this credentials instance
    /// </summary>
    /// <remarks>
    ///The expiration time can be equal to the maximum 32 bit unsigned value if it was not set.
    /// </remarks>
    property uint32_t Expiration
    {
        uint32_t get();
        void set(uint32_t value);
    }

  private:
    friend ref class AuthListener;
    friend class _AuthListener;
    Credentials(const ajn::AuthListener::Credentials * creds);
    Credentials(const qcc::ManagedObj<_Credentials>* creds);
    ~Credentials();

    qcc::ManagedObj<_Credentials>* _mCredentials;
    _Credentials* _credentials;
};

}
// Credentials.h
