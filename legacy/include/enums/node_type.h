#pragma once

#include <cstdint>

enum class NodeType : uint8_t {
    ACTION,
    CHANCE,
    TERMINAL,
    SHOWDOWN
};