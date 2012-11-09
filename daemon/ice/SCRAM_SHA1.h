#ifndef SCRAM_SHA1_H_
#define SCRAM_SHA1_H_

/**
 * @file
 * This file defines the SCRAM-SHA-1 related functions.
 */

/******************************************************************************
 * Copyright 2012, Qualcomm Innovation Center, Inc.
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
#error Only include SCRAM_SHA1.h in C++ code.
#endif

#include <qcc/platform.h>
#include <qcc/String.h>
#include <qcc/StringUtil.h>
#include <JSON/json.h>
#include <list>

#include "RendezvousServerInterface.h"

using namespace qcc;
using namespace std;

namespace ajn {

class SCRAM_SHA_1 {

  public:

    /**
     * @brief Size in bytes of the SASL nonce
     */
    static const uint32_t SASL_NONCE_SIZE = 16;

    /**
     * @brief Size in bytes of the salt
     */
    static const uint32_t SALT_SIZE = 16;

    /**
     * @brief Size in bytes of the salt byte array
     */
    static const uint32_t SALT_BYTE_ARRAY_SIZE = SALT_SIZE + 4;

    /* Constructor */
    SCRAM_SHA_1();

    /* Destructor */
    ~SCRAM_SHA_1();

    /* Set the UserName and Password*/
    void SetUserCredentials(String userName, String password);

    /* Clear the contents of this object */
    void Reset(void);

    /**
     * Generate the client login first SASL message.
     */
    String GenerateClientLoginFirstSASLMessage(void);

    /**
     * Validate the client login first response.
     */
    QStatus ValidateClientLoginFirstResponse(String response);

    /**
     * Generate the client login final SASL message.
     */
    String GenerateClientLoginFinalSASLMessage(void);

    /**
     * Validate the client login final response.
     */
    QStatus ValidateClientLoginFinalResponse(ClientLoginFinalResponse response);

    /**
     * Returns true if the error field is present in the ServerFirstResponse.
     */
    bool IsErrorPresentInServerFirstResponse(void) { return ServerFirstResponse.is_e_Present(); };

    /**
     * Returns true if the error field is present in the ServerFinalResponse.
     */
    bool IsErrorPresentInServerFinalResponse(void) { return ServerFinalResponse.is_e_Present(); };

    /**
     * Returns the error field in the ServerFirstResponse.
     */
    SASLError GetErrorInServerFirstResponse(void) { return ServerFirstResponse.e; };

    /**
     * Returns the error field in the ServerFinalResponse.
     */
    SASLError GetErrorInServerFinalResponse(void) { return ServerFinalResponse.e; };

  private:

    /**
     * SASL nonce generated by the Client.
     */
    mutable String ClientNonce;

    /**
     * SASL channel binding.
     */
    String ChannelBinding;

    /**
     * SASL Client Proof
     */
    String ClientProof;

    /**
     * Client User Name.
     */
    String UserName;

    /**
     * Client Password.
     */
    String Password;

    /**
     * Client First Message.
     */
    SASLMessage ClientFirstMessage;

    /**
     * Client First Message as a String.
     */
    String ClientFirstMessageString;

    /**
     * Client Final Message.
     */
    SASLMessage ClientFinalMessage;

    /**
     * Client Final Message as a String.
     */
    String ClientFinalMessageString;

    /**
     * Server First Response.
     */
    SASLMessage ServerFirstResponse;

    /**
     * Server First Response as a String.
     */
    String ServerFirstResponseString;

    /**
     * Server Final Response.
     */
    SASLMessage ServerFinalResponse;

    /**
     * Server Final Response as a String.
     */
    String ServerFinalResponseString;

    /**
     * Salted password.
     */
    uint8_t SaltedPassword[Crypto_SHA1::DIGEST_SIZE];

    /**
     * Client Key.
     */
    uint8_t ClientKey[Crypto_SHA1::DIGEST_SIZE];

    /**
     * Stored Key.
     */
    uint8_t StoredKey[Crypto_SHA1::DIGEST_SIZE];

    /**
     * Client Signature.
     */
    uint8_t ClientSignature[Crypto_SHA1::DIGEST_SIZE];

    /**
     * Authorization Message.
     */
    String AuthMessage;

    /**
     * Generate the SASL nonce.
     */
    void GenerateNonce(void);

    /**
     * Generate the SASL channel binding.
     */
    void GenerateChannelBinding(void);

    /**
     * Generate the SASL Client Proof.
     */
    void GenerateClientProof(void);

    /**
     * Validate the server.
     */
    QStatus ValidateServer(String serverSignature);

    /**
     * Perform XOR of two byte arrays
     */
    void XorByteArray(uint8_t* in1, uint8_t* in2, size_t size);

    /**
     * Perform XOR of two byte arrays
     */
    void XorByteArray(const uint8_t* in1, const uint8_t* in2, uint8_t* out, size_t size);

    /**
     * Generate the salted password
     */
    void GenerateSaltedPassword(void);

    /**
     * Generate the Client Key
     */
    void GenerateClientKey(void);

    /**
     * Generate the Stored Key
     */
    void GenerateStoredKey(void);

    /**
     * Generate the UTF-8 encoded User Name
     */
    void GenerateUserName(void);

    /**
     * Generate the UTF-8 encoded Password
     */
    void GeneratePassword(void);

    /**
     * Generate the Authorization Message
     */
    void GenerateAuthMessage(void);

    /**
     * Generate the Client Signature
     */
    void GenerateClientSignature(void);
};

}

#endif /* SCRAM_SHA1_H_ */
