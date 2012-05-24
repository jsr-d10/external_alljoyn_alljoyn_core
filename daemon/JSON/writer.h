/**
 * @file
 * writer.h
 */

/******************************************************************************
 *
 * This file has been directly derived from the json-cpp distribution from
 * www.json.org which has been dedicated to the Public Domain
 *
 ******************************************************************************/

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

#ifndef JSON_WRITER_H_INCLUDED
# define JSON_WRITER_H_INCLUDED

# include "value.h"
# include <vector>
# include <string>

namespace Json {

class Value;

/** \brief Abstract class for writers.
 */
class JSON_API Writer {
  public:
    virtual ~Writer();

    virtual std::string write(const Value& root) = 0;
};

/** \brief Outputs a Value in <a HREF="http://www.json.org">JSON</a> format without formatting (not human friendly).
 *
 * The JSON document is written in a single line. It is not intended for 'human' consumption,
 * but may be usefull to support feature such as RPC where bandwith is limited.
 * \sa Reader, Value
 */
class JSON_API FastWriter : public Writer {
  public:
    FastWriter();
    virtual ~FastWriter() { }

    void enableYAMLCompatibility();

  public:  // overridden from Writer
    virtual std::string write(const Value& root);

  private:
    void writeValue(const Value& value);

    std::string document_;
    bool yamlCompatiblityEnabled_;
};

/** \brief Writes a Value in <a HREF="http://www.json.org">JSON</a> format in a human friendly way.
 *
 * The rules for line break and indent are as follow:
 * - Object value:
 *     - if empty then print {} without indent and line break
 *     - if not empty the print '{', line break & indent, print one value per line
 *       and then unindent and line break and print '}'.
 * - Array value:
 *     - if empty then print [] without indent and line break
 *     - if the array contains no object value, empty array or some other value types,
 *       and all the values fit on one lines, then print the array on a single line.
 *     - otherwise, it the values do not fit on one line, or the array contains
 *       object or non empty array, then print one value per line.
 *
 * If the Value have comments then they are outputed according to their #CommentPlacement.
 *
 * \sa Reader, Value, Value::setComment()
 */
class JSON_API StyledWriter : public Writer {
  public:
    StyledWriter();
    virtual ~StyledWriter() { }

  public:  // overridden from Writer
    /** \brief Serialize a Value in <a HREF="http://www.json.org">JSON</a> format.
     * \param root Value to serialize.
     * \return String containing the JSON document that represents the root value.
     */
    virtual std::string write(const Value& root);

  private:
    void writeValue(const Value& value);
    void writeArrayValue(const Value& value);
    bool isMultineArray(const Value& value);
    void pushValue(const std::string& value);
    void writeIndent();
    void writeWithIndent(const std::string& value);
    void indent();
    void unindent();
    void writeCommentBeforeValue(const Value& root);
    void writeCommentAfterValueOnSameLine(const Value& root);
    bool hasCommentForValue(const Value& value);
    static std::string normalizeEOL(const std::string& text);

    typedef std::vector<std::string> ChildValues;

    ChildValues childValues_;
    std::string document_;
    std::string indentString_;
    int rightMargin_;
    int indentSize_;
    bool addChildValues_;
};

std::string JSON_API valueToString(Int value);
std::string JSON_API valueToString(UInt value);
std::string JSON_API valueToString(double value);
std::string JSON_API valueToString(bool value);
std::string JSON_API valueToQuotedString(const char*value);

} // namespace Json



#endif // JSON_WRITER_H_INCLUDED
