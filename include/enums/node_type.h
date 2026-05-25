#pragma once

#include<vector>

enum class NodeType : uint8_t {
    ACTION,
    CHANCE,
    TERMINAL,
    SHOWDOWN
};