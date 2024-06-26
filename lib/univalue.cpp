// Copyright 2014 BitPay Inc.
// Copyright 2015 Bitcoin Core Developers
// Copyright (c) 2020-2021 The Bitcoin developers
// Copyright (c) 2021 Calin A. Culianu <calin.culianu@gmail.com>
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#define __STDC_FORMAT_MACROS 1

#include <algorithm>
#include <array>
#include <cassert>
#include <cinttypes>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "univalue.h"
#include "univalue_internal.h"

namespace {
// Helper class to run a functor at scope end
template <typename Func>
struct Defer {
    Func func;
    Defer(Func && f) noexcept : func(std::move(f)) {}
    ~Defer() { func(); }
};
}

/* static */ const UniValue UniValue::Null{VNULL};
/* static */ const std::string UniValue::emptyVal{};

/// Note: Update this manually, for now, when doing a new release.
/* static */ std::tuple<int, int, int> UniValue::version() { return {2, 3, 0}; }

const UniValue& UniValue::Object::operator[](std::string_view key) const noexcept
{
    if (auto found = locate(key)) {
        return *found;
    }
    return Null;
}

const UniValue& UniValue::Object::operator[](size_type index) const noexcept
{
    if (index < vector.size()) {
        return vector[index].second;
    }
    return Null;
}

const UniValue* UniValue::Object::locate(std::string_view key) const noexcept {
    for (auto& entry : vector) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}
UniValue* UniValue::Object::locate(std::string_view key) noexcept {
    for (auto& entry : vector) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

const UniValue& UniValue::Object::at(std::string_view key) const {
    if (auto found = locate(key)) {
        return *found;
    }
    throw std::out_of_range("Key not found in JSON object: " + std::string(key));
}
UniValue& UniValue::Object::at(std::string_view key) {
    if (auto found = locate(key)) {
        return *found;
    }
    throw std::out_of_range("Key not found in JSON object: " + std::string(key));
}

const UniValue& UniValue::Object::at(size_type index) const
{
    if (index < vector.size()) {
        return vector[index].second;
    }
    throw std::out_of_range("Index " + std::to_string(index) + " out of range in JSON object of length " +
                            std::to_string(vector.size()));
}
UniValue& UniValue::Object::at(size_type index)
{
    if (index < vector.size()) {
        return vector[index].second;
    }
    throw std::out_of_range("Index " + std::to_string(index) + " out of range in JSON object of length " +
                            std::to_string(vector.size()));
}

const UniValue& UniValue::Object::front() const noexcept
{
    if (!vector.empty()) {
        return vector.front().second;
    }
    return Null;
}

const UniValue& UniValue::Object::back() const noexcept
{
    if (!vector.empty()) {
        return vector.back().second;
    }
    return Null;
}

const UniValue& UniValue::Array::operator[](size_type index) const noexcept
{
    if (index < vector.size()) {
        return vector[index];
    }
    return Null;
}

const UniValue& UniValue::Array::at(size_type index) const
{
    if (index < vector.size()) {
        return vector[index];
    }
    throw std::out_of_range("Index " + std::to_string(index) + " out of range in JSON array of length " +
                            std::to_string(vector.size()));
}
UniValue& UniValue::Array::at(size_type index)
{
    if (index < vector.size()) {
        return vector[index];
    }
    throw std::out_of_range("Index " + std::to_string(index) + " out of range in JSON array of length " +
                            std::to_string(vector.size()));
}

const UniValue& UniValue::Array::front() const noexcept
{
    if (!vector.empty()) {
        return vector.front();
    }
    return Null;
}

const UniValue& UniValue::Array::back() const noexcept
{
    if (!vector.empty()) {
        return vector.back();
    }
    return Null;
}

void UniValue::setNumStr(const char* val_)
{
    if (auto optStr = univalue_internal::validateAndStripNumStr(val_)) {
        var.emplace<NumStr>(std::move(*optStr));
    }
}

template<typename Int64>
void UniValue::setInt64(Int64 val_)
{
    static_assert(std::is_same_v<Int64, int64_t> || std::is_same_v<Int64, uint64_t>,
                  "This function may only be called with either an int64_t or a uint64_t argument.");
    // Begin by setting to null, so that null is assigned if the number cannot be accepted.
    setNull();
    // Longest possible 64-bit integers are "-9223372036854775808" and "18446744073709551615",
    // both of which require 20 visible characters and 1 terminating null,
    // hence buffer size 21.
    constexpr int bufSize = 21;
    std::array<char, bufSize> buf;
    int n = std::snprintf(buf.data(), size_t(bufSize), std::is_signed<Int64>::value ? "%" PRId64 : "%" PRIu64, val_);
    if (n <= 0 || n >= bufSize) // should never happen
        return;
    var.emplace<NumStr>(buf.data(), std::string::size_type(n));
}

void UniValue::operator=(short val_) { setInt64<int64_t>(val_); }
void UniValue::operator=(int val_) { setInt64<int64_t>(val_); }
void UniValue::operator=(long val_) { setInt64<int64_t>(val_); }
void UniValue::operator=(long long val_) { setInt64<int64_t>(val_); }
void UniValue::operator=(unsigned short val_) { setInt64<uint64_t>(val_); }
void UniValue::operator=(unsigned val_) { setInt64<uint64_t>(val_); }
void UniValue::operator=(unsigned long val_) { setInt64<uint64_t>(val_); }
void UniValue::operator=(unsigned long long val_) { setInt64<uint64_t>(val_); }

void UniValue::operator=(double val_)
{
    // Begin by setting to null, so that null is assigned if the number cannot be accepted.
    setNull();
    // Ensure not NaN or inf, which are not representable by the JSON Number type.
    if (!std::isfinite(val_))
        return;
    // For floats and doubles, we can't use snprintf() since the C-locale may be anything,
    // which means the decimal character may be anything. What's more, we can't touch the
    // C-locale since it's a global object and is not thread-safe.
    //
    // So, for doubles we must fall-back to using the (slower) std::ostringstream.
    // See BCHN issue #137.
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    if (oss << std::setprecision(16) << val_) {
        var.emplace<NumStr>(oss.str());
    }
}

const UniValue& UniValue::operator[](std::string_view key) const noexcept
{
    if (auto found = locate(key)) {
        return *found;
    }
    return Null;
}

const UniValue& UniValue::operator[](size_type index) const noexcept
{
    switch (type()) {
    case VOBJ:
        return var.get<Object>()[index];
    case VARR:
        return var.get<Array>()[index];
    default:
        return Null;
    }
}

const UniValue& UniValue::front() const noexcept
{
    switch (type()) {
    case VOBJ:
        return var.get<Object>().front();
    case VARR:
        return var.get<Array>().front();
    default:
        return Null;
    }
}

const UniValue& UniValue::back() const noexcept
{
    switch (type()) {
    case VOBJ:
        return var.get<Object>().back();
    case VARR:
        return var.get<Array>().back();
    default:
        return Null;
    }
}

const UniValue* UniValue::locate(std::string_view key) const noexcept {
    return const_cast<UniValue *>(this)->locate(key);
}
UniValue* UniValue::locate(std::string_view key) noexcept {
    return type() == VOBJ ? var.get<Object>().locate(key) : nullptr;
}

const UniValue& UniValue::at(std::string_view key) const {
    return const_cast<UniValue *>(this)->at(key);
}
UniValue& UniValue::at(std::string_view key) {
    if (type() == VOBJ) {
        return var.get<Object>().at(key);
    }
    throw std::domain_error(std::string("Cannot look up keys in JSON ") + typeName(type()) +
                            ", expected object with key: " + std::string(key));
}

const UniValue& UniValue::at(size_type index) const
{
    return const_cast<UniValue *>(this)->at(index);
}
UniValue& UniValue::at(size_type index)
{
    switch (type()) {
    case VOBJ:
        return var.get<Object>().at(index);
    case VARR:
        return var.get<Array>().at(index);
    default:
        throw std::domain_error(std::string("Cannot look up indices in JSON ") + typeName(type()) +
                                ", expected array or object larger than " + std::to_string(index) + " elements");
    }
}

const char *UniValue::typeName(UniValue::VType t) noexcept
{
    switch (t) {
    case UniValue::VNULL: return "null";
    case UniValue::VFALSE: return "false";
    case UniValue::VTRUE: return "true";
    case UniValue::VOBJ: return "object";
    case UniValue::VARR: return "array";
    case UniValue::VNUM: return "number";
    case UniValue::VSTR: return "string";
    // Adding something here? Add it to the other UniValue::typeName overload below too!
    }

    // not reached
    return nullptr;
}

std::string UniValue::typeName(int t) {
    std::string result;
    auto appendTypeNameIfTypeIncludes = [&](UniValue::VType type) {
        if (t & type) {
            if (!result.empty()) {
                result += '/';
            }
            result += typeName(type);
        }
    };
    appendTypeNameIfTypeIncludes(UniValue::VNULL);
    appendTypeNameIfTypeIncludes(UniValue::VFALSE);
    appendTypeNameIfTypeIncludes(UniValue::VTRUE);
    appendTypeNameIfTypeIncludes(UniValue::VOBJ);
    appendTypeNameIfTypeIncludes(UniValue::VARR);
    appendTypeNameIfTypeIncludes(UniValue::VNUM);
    appendTypeNameIfTypeIncludes(UniValue::VSTR);
    return result;
}
