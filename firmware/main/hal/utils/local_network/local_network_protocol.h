/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace local_network {

constexpr size_t HeaderSize     = 20;
constexpr size_t MaxPayloadSize = UINT16_MAX;

inline std::string normalizeMac(std::string_view value)
{
    std::string result;
    for (unsigned char character : value) {
        if (character == ':' || character == '-') continue;
        if (!std::isxdigit(character)) return {};
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result.size() == 12 ? result : std::string();
}

inline void writeBigEndian(uint8_t* data, uint64_t value, size_t width)
{
    while (width > 0) {
        data[--width] = static_cast<uint8_t>(value);
        value >>= 8;
    }
}

inline bool isKnownType(uint8_t type)
{
    // Keep these ranges aligned with the App's MsgType wire values.
    return (type >= 0x01 && type <= 0x07) || (type >= 0x09 && type <= 0x0E) || (type >= 0x10 && type <= 0x1B);
}

inline bool hasTargetPrefix(uint8_t type)
{
    return (type >= 0x02 && type <= 0x06) || type == 0x12 || type == 0x13;
}

inline bool encode(uint8_t type, const uint8_t* payload, size_t len, uint64_t session, uint32_t sequence,
                   std::vector<uint8_t>& frame)
{
    if (!isKnownType(type) || len > MaxPayloadSize || (len != 0 && payload == nullptr)) return false;
    frame.assign(HeaderSize + len, 0);
    frame[0] = 0x53;
    frame[1] = 0x43;
    frame[2] = 1;
    frame[3] = type;
    writeBigEndian(frame.data() + 6, session, 8);
    writeBigEndian(frame.data() + 14, sequence, 4);
    writeBigEndian(frame.data() + 18, len, 2);
    if (len != 0) memcpy(frame.data() + HeaderSize, payload, len);
    return true;
}

// Validate the LAN envelope and the target, then reuse the existing HAL handler
// by returning [type:1][length:4][payload] with the MAC prefix removed.
inline bool decodeToRemoteFrame(const uint8_t* data, size_t len, std::string_view factory_mac,
                                std::vector<uint8_t>& frame)
{
    if (data == nullptr || len < HeaderSize || data[0] != 0x53 || data[1] != 0x43 || data[2] != 1 ||
        !isKnownType(data[3]))
        return false;
    size_t payload_size = (static_cast<size_t>(data[18]) << 8) | data[19];
    if (payload_size != len - HeaderSize) return false;
    const auto* payload = data + HeaderSize;
    if (hasTargetPrefix(data[3])) {
        const auto expected = normalizeMac(factory_mac);
        if (payload_size < 12 || expected.empty() ||
            normalizeMac(std::string_view(reinterpret_cast<const char*>(payload), 12)) != expected)
            return false;
        payload += 12;
        payload_size -= 12;
    }
    frame.resize(5 + payload_size);
    frame[0] = data[3];
    writeBigEndian(frame.data() + 1, payload_size, 4);
    if (payload_size != 0) memcpy(frame.data() + 5, payload, payload_size);
    return true;
}

namespace detail {

inline void skipJsonWhitespace(std::string_view text, size_t& index)
{
    while (index < text.size()) {
        const char character = text[index];
        if (character != ' ' && character != '\t' && character != '\n' && character != '\r') break;
        ++index;
    }
}

inline bool readJsonString(std::string_view text, size_t& index, std::string& out)
{
    if (index >= text.size() || text[index] != '"') return false;
    ++index;
    std::string result;
    while (index < text.size()) {
        const char character = text[index++];
        if (character == '"') {
            out = std::move(result);
            return true;
        }
        if (character == '\\') {
            // The App only encodes ASCII addresses and paths here, so the escape
            // payload is kept verbatim instead of decoding \u sequences.
            if (index >= text.size()) return false;
            result.push_back(text[index++]);
            continue;
        }
        result.push_back(character);
    }
    return false;
}

inline bool skipJsonValue(std::string_view text, size_t& index);

inline bool skipJsonArray(std::string_view text, size_t& index)
{
    if (index >= text.size() || text[index] != '[') return false;
    ++index;
    skipJsonWhitespace(text, index);
    if (index < text.size() && text[index] == ']') {
        ++index;
        return true;
    }
    while (index < text.size()) {
        if (!skipJsonValue(text, index)) return false;
        skipJsonWhitespace(text, index);
        if (index < text.size() && text[index] == ',') {
            ++index;
            skipJsonWhitespace(text, index);
            continue;
        }
        if (index < text.size() && text[index] == ']') {
            ++index;
            return true;
        }
        return false;
    }
    return false;
}

inline bool skipJsonObject(std::string_view text, size_t& index)
{
    if (index >= text.size() || text[index] != '{') return false;
    ++index;
    skipJsonWhitespace(text, index);
    if (index < text.size() && text[index] == '}') {
        ++index;
        return true;
    }
    std::string key;
    while (index < text.size()) {
        if (!readJsonString(text, index, key)) return false;
        skipJsonWhitespace(text, index);
        if (index >= text.size() || text[index] != ':') return false;
        ++index;
        if (!skipJsonValue(text, index)) return false;
        skipJsonWhitespace(text, index);
        if (index < text.size() && text[index] == ',') {
            ++index;
            skipJsonWhitespace(text, index);
            continue;
        }
        if (index < text.size() && text[index] == '}') {
            ++index;
            return true;
        }
        return false;
    }
    return false;
}

inline bool skipJsonValue(std::string_view text, size_t& index)
{
    skipJsonWhitespace(text, index);
    if (index >= text.size()) return false;
    const char character = text[index];
    if (character == '"') {
        std::string ignored;
        return readJsonString(text, index, ignored);
    }
    if (character == '[') return skipJsonArray(text, index);
    if (character == '{') return skipJsonObject(text, index);
    // Numbers, booleans and null all end at the same delimiters.
    const auto start = index;
    while (index < text.size()) {
        const char current = text[index];
        if (current == ',' || current == ']' || current == '}' || current == ' ' || current == '\t' ||
            current == '\n' || current == '\r')
            break;
        ++index;
    }
    return index > start;
}

inline bool readJsonUnsigned(std::string_view text, size_t& index, uint32_t& out)
{
    skipJsonWhitespace(text, index);
    const auto start = index;
    uint64_t value   = 0;
    while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
        value = value * 10 + static_cast<unsigned>(text[index] - '0');
        if (value > UINT32_MAX) return false;
        ++index;
    }
    if (index == start) return false;
    out = static_cast<uint32_t>(value);
    return true;
}

inline bool readJsonStringArray(std::string_view text, size_t& index, std::vector<std::string>& out)
{
    skipJsonWhitespace(text, index);
    if (index >= text.size() || text[index] != '[') return false;
    ++index;
    out.clear();
    skipJsonWhitespace(text, index);
    if (index < text.size() && text[index] == ']') {
        ++index;
        return true;
    }
    while (index < text.size()) {
        std::string item;
        if (!readJsonString(text, index, item)) return false;
        out.push_back(std::move(item));
        skipJsonWhitespace(text, index);
        if (index < text.size() && text[index] == ',') {
            ++index;
            skipJsonWhitespace(text, index);
            continue;
        }
        if (index < text.size() && text[index] == ']') {
            ++index;
            return true;
        }
        return false;
    }
    return false;
}

}  // namespace detail

/// Parse the App's endpoint announcement, for example
/// {"port":45678,"path":"/ws","ip":["192.168.1.5","10.0.0.3"]}.
///
/// Only the port and the address list are used. Whitespace, extra keys and key
/// order are tolerated so the payload can grow without breaking old firmware;
/// the payload stays a flat object because both ends are ours.
inline bool parseEndpointAnnouncement(std::string_view json, std::vector<std::string>& hosts, uint16_t& port)
{
    size_t index = 0;
    detail::skipJsonWhitespace(json, index);
    if (index >= json.size() || json[index] != '{') return false;
    ++index;
    std::vector<std::string> parsed_hosts;
    bool has_port        = false;
    uint32_t parsed_port = 0;
    detail::skipJsonWhitespace(json, index);
    if (index < json.size() && json[index] == '}') {
        ++index;
    } else {
        while (index < json.size()) {
            std::string key;
            if (!detail::readJsonString(json, index, key)) return false;
            detail::skipJsonWhitespace(json, index);
            if (index >= json.size() || json[index] != ':') return false;
            ++index;
            detail::skipJsonWhitespace(json, index);
            if (key == "port") {
                if (!detail::readJsonUnsigned(json, index, parsed_port)) return false;
                has_port = true;
            } else if (key == "ip") {
                if (!detail::readJsonStringArray(json, index, parsed_hosts)) return false;
            } else if (!detail::skipJsonValue(json, index)) {
                return false;
            }
            detail::skipJsonWhitespace(json, index);
            if (index < json.size() && json[index] == ',') {
                ++index;
                detail::skipJsonWhitespace(json, index);
                continue;
            }
            if (index < json.size() && json[index] == '}') {
                ++index;
                break;
            }
            return false;
        }
    }
    detail::skipJsonWhitespace(json, index);
    parsed_hosts.erase(
        std::remove_if(parsed_hosts.begin(), parsed_hosts.end(), [](const std::string& host) { return host.empty(); }),
        parsed_hosts.end());
    if (index != json.size() || !has_port || parsed_port == 0 || parsed_port > UINT16_MAX || parsed_hosts.empty()) {
        return false;
    }
    hosts = std::move(parsed_hosts);
    port  = static_cast<uint16_t>(parsed_port);
    return true;
}

}  // namespace local_network
