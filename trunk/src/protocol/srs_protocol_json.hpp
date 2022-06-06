//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_PROTOCOL_JSON_HPP
#define SRS_PROTOCOL_JSON_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// JSON decode
// 1. SrsJsonAny: read any from str:char*
//        SrsJsonAny* any = NULL;
//        if ((any = SrsJsonAny::loads(str)) == NULL) {
//            return -1;
//         }
//        srs_assert(pany); // if success, always valid object.
// 2. SrsJsonAny: convert to specifid type, for instance, string
//        SrsJsonAny* any = ...
//        if (any->is_string()) {
//            string v = any->to_str();
//        }
//
// For detail usage, see interfaces of each object.
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// @see: https://github.com/udp/json-parser

class SrsAmf0Any;
class SrsJsonArray;
class SrsJsonObject;

class SrsJsonAny
{
public:
    char marker;
    // Don't directly create this object,
    // please use SrsJsonAny::str() to create a concreated one.
protected:
    SrsJsonAny();
public:
    virtual ~SrsJsonAny();
public:
    virtual bool is_string();
    virtual bool is_boolean();
    virtual bool is_integer();
    virtual bool is_number();
    virtual bool is_object();
    virtual bool is_array();
    virtual bool is_null();
public:
    // Get the string of any when is_string() indicates true.
    // user must ensure the type is a string, or assert failed.
    virtual std::string to_str();
    // Get the boolean of any when is_boolean() indicates true.
    // user must ensure the type is a boolean, or assert failed.
    virtual bool to_boolean();
    // Get the integer of any when is_integer() indicates true.
    // user must ensure the type is a integer, or assert failed.
    virtual int64_t to_integer();
    // Get the number of any when is_number() indicates true.
    // user must ensure the type is a number, or assert failed.
    virtual double to_number();
    // Get the object of any when is_object() indicates true.
    // user must ensure the type is a object, or assert failed.
    virtual SrsJsonObject* to_object();
    // Get the ecma array of any when is_ecma_array() indicates true.
    // user must ensure the type is a ecma array, or assert failed.
    virtual SrsJsonArray* to_array();
public:
    virtual std::string dumps();
    virtual SrsAmf0Any* to_amf0();
public:
    static SrsJsonAny* str(const char* value = NULL);
    static SrsJsonAny* str(const char* value, int length);
    static SrsJsonAny* boolean(bool value = false);
    static SrsJsonAny* integer(int64_t value = 0);
    static SrsJsonAny* number(double value = 0.0);
    static SrsJsonAny* null();
    static SrsJsonObject* object();
    static SrsJsonArray* array();
public:
    // Read json tree from string.
    // @return json object. NULL if error.
    static SrsJsonAny* loads(std::string str);
};

class SrsJsonObject : public SrsJsonAny
{
private:
    typedef std::pair<std::string, SrsJsonAny*> SrsJsonObjectPropertyType;
    std::vector<SrsJsonObjectPropertyType> properties;
private:
    // Use SrsJsonAny::object() to create it.
    friend class SrsJsonAny;
    SrsJsonObject();
public:
    virtual ~SrsJsonObject();
public:
    virtual int count();
    // @remark: max index is count().
    virtual std::string key_at(int index);
    // @remark: max index is count().
    virtual SrsJsonAny* value_at(int index);
public:
    virtual std::string dumps();
    virtual SrsAmf0Any* to_amf0();
public:
    virtual SrsJsonObject* set(std::string key, SrsJsonAny* value);
    virtual SrsJsonAny* get_property(std::string name);
    virtual SrsJsonAny* ensure_property_string(std::string name);
    virtual SrsJsonAny* ensure_property_integer(std::string name);
    virtual SrsJsonAny* ensure_property_number(std::string name);
    virtual SrsJsonAny* ensure_property_boolean(std::string name);
    virtual SrsJsonAny* ensure_property_object(std::string name);
    virtual SrsJsonAny* ensure_property_array(std::string name);
};

class SrsJsonArray : public SrsJsonAny
{
private:
    std::vector<SrsJsonAny*> properties;
    
private:
    // Use SrsJsonAny::array() to create it.
    friend class SrsJsonAny;
    SrsJsonArray();
public:
    virtual ~SrsJsonArray();
public:
    virtual int count();
    // @remark: max index is count().
    virtual SrsJsonAny* at(int index);
    virtual SrsJsonArray* add(SrsJsonAny* value);
    // alias to add.
    virtual SrsJsonArray* append(SrsJsonAny* value);
public:
    virtual std::string dumps();
    virtual SrsAmf0Any* to_amf0();
};

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// JSON encode, please use JSON.dumps() to encode json object.

#endif
