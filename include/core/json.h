#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// minimal JSON output helpers for the UI protocol

// NaN / inf become null, tested on the bits since -ffast-math lets the compiler assume std::isnan is always false
inline std::string json_number(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    if (((bits >> 52) & 0x7FF) == 0x7FF) return "null";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

inline std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (char ch : s) {
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += ch;
        } else if ((unsigned char)ch < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", (unsigned)(unsigned char)ch);
            out += buf;
        } else {
            out += ch;
        }
    }
    return out + "\"";
}
