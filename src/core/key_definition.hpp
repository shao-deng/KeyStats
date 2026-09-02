#pragma once

#include "key_id.hpp"

#include <string_view>

namespace keystats {

struct KeyDefinition {
    KeyId id{};
    std::string_view display_name;
    std::string_view category;
    int display_order = 0;
};

}  // namespace keystats
