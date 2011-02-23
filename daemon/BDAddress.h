/**
 * @file
 * Abstraction class for Bluetooth Device address.
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
#ifndef _BDADDRESS_H
#define _BDADDRESS_H

#include <qcc/platform.h>

#include <qcc/String.h>
#include <qcc/StringUtil.h>
#include <qcc/Util.h>

namespace ajn {

class BDAddress {
  public:
    const static size_t ADDRESS_SIZE = 6;   /**< BT addresses are 6 octets in size. */

    /**
     * Default Constructor - initializes the BD Address to 00:00:00:00:00:00.
     */
    BDAddress() : buf(0), cacheValid(false) { }

    /**
     * Copy Constructor
     *
     * @param other     BD Address to copy from.
     */
    BDAddress(const BDAddress& other) : buf(buf), cacheValid(false) { }

    /**
     * Constructor that initializes the BD Address from a string in one of the
     * following forms:
     * - 123456789abc
     * - 12.34.56.78.9a.bc
     * - 12:34:56:78:9a:bc
     *
     * @param addr  BD address in a string.
     */
    BDAddress(const qcc::String& addr) : cacheValid(false) {
        if (FromString(addr) != ER_OK) {
            // Failed to parse the string.  Gotta intialize to something...
            buf = 0;
        }
    }

    /**
     * Constructor that initializes the BD Address from an array of bytes.
     *
     * @param addr          An array of 6 bytes that contains the BD Address.
     * @param littleEndian  [Optional] Flag to indicate if bytes are arranged in
     *                      little-endian (BlueZ) order (default is no).
     */
    BDAddress(const uint8_t* addr, bool littleEndian = false) : cacheValid(false) {
        this->CopyFrom(addr, littleEndian);
    }

    /**
     * Function to set the BD Address from an array of bytes.
     *
     * @param addr          An array of 6 bytes that contains the BD Address.
     * @param littleEndian  [Optional] Flag to indicate if bytes are arranged in
     *                      little-endian (BlueZ) order (default is no).
     */
    void CopyFrom(const uint8_t* addr, bool littleEndian = false) {
        if (littleEndian) {
            buf = (letoh32(*(uint32_t*)&addr[2]) << 16) | letoh16(*(uint16_t*)&addr[0]);
        } else {
            buf = (betoh32(*(uint32_t*)&addr[0]) << 16) | betoh16(*(uint16_t*)&addr[4]);
        }
        cacheValid = false;
    }

    /**
     * Function write the BD Address to an array of bytes.
     *
     * @param addr          [Out] An array of 6 bytes for writing the BD Address.
     * @param littleEndian  [Optional] Flag to indicate if bytes are arranged in
     *                      little-endian (BlueZ) order (default is no).
     */
    void CopyTo(uint8_t* addr, bool littleEndian = false) const {
        if (littleEndian) {
            *(uint32_t*)&addr[0] = htole32((uint32_t)buf & 0xffffffff);
            *(uint16_t*)&addr[4] = htole16((uint16_t)(buf >> 32) & 0xffff);
        } else {
            *(uint32_t*)&addr[0] = htobe32((uint32_t)(buf >> 16) & 0xffffffff);
            *(uint16_t*)&addr[4] = htobe16((uint16_t)buf & 0xffff);
        }
    }

    /**
     * Function to represent the BD Address in a string.  By default, the
     * parts of the address are separated by a ":", this may be changed by
     * setting the separator parameter.
     *
     * @param separator     [Optional] Separator character to use when generating the string.
     *
     * @return  A string representation of the BD Address.
     */
    const qcc::String& ToString(char separator = ':') const {
        if (!cacheValid) {
            /* Humans accustomed to reading left-to-right script tend to
             * prefer bytes to be in big endian order so that is the
             * convention used for string representations. */
            const uint64_t be = htobe64(buf);
            cache = qcc::BytesToHexString(((const uint8_t*)&be) + 2, 6, true, separator);
            cacheValid = true;
        }
        return cache;
    }

    /**
     * Function to set the BD Address from a string in one of the
     * following forms:
     * - 123456789abc
     * - 12.34.56.78.9a.bc
     * - 12:34:56:78:9a:bc
     *
     * @param addr  BD address in a string.
     *
     * @return  Indication of success or failure.
     */
    QStatus FromString(const qcc::String addr) {
        uint64_t be;
        if ((HexStringToBytes(addr, ((uint8_t*)&be) + 2, ADDRESS_SIZE) != ADDRESS_SIZE) &&
            (HexStringToBytes(addr, ((uint8_t*)&be) + 2, ADDRESS_SIZE, '.') != ADDRESS_SIZE) &&
            (HexStringToBytes(addr, ((uint8_t*)&be) + 2, ADDRESS_SIZE, ':') != ADDRESS_SIZE)) {
            return ER_FAIL;
        }
        buf = betoh64(be);
        return ER_OK;
    }

    /**
     * Assignment operator.
     *
     * @param other     Reference to BD Address to copy from.
     *
     * @return  Reference to *this.
     */
    BDAddress& operator=(const BDAddress& other) {
        buf = other.buf;
        cacheValid = false;
        return *this;
    }

    /**
     * Test if 2 BD Address are the same.
     *
     * @param other     The other BD Address used for comparison.
     *
     * @return  True if they are the same, false otherwise.
     */
    bool operator==(const BDAddress& other) const {
        return (buf == other.buf);
    }

    /**
     * Test if 2 BD Address are different.
     *
     * @param other      The other BD Address used for comparison.
     *
     * @return  True if they are the same, false otherwise.
     */
    bool operator!=(const BDAddress& other) const {
        return (buf != other.buf);
    }

    /**
     * Test if *this BD Address is less than the other BD Address.
     *
     * @param other      The other BD Address used for comparison.
     *
     * @return  True if *this < other, false otherwise.
     */
    bool operator<(const BDAddress& other) const { return buf < other.buf; }

    /**
     * Test if *this BD Address is greater than the other BD Address.
     *
     * @param other      The other BD Address used for comparison.
     *
     * @return  True if *this > other, false otherwise.
     */
    bool operator>(const BDAddress& other) const { return buf > other.buf; }

  private:

    uint64_t buf;               /**< BD Address storage. */

    mutable qcc::String cache;  /** Cache storage for the string representation.  Make usage of BDAddress::ToString() easier. */
    mutable bool cacheValid;    /**< Flag indicating if the string representation cache is valid or not. */
};

}

#endif
