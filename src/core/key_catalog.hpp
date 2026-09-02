#pragma once

#include "key_definition.hpp"
#include "key_id.hpp"

#include <span>

namespace keystats {

class KeyCatalog {
public:
    static constexpr int Count = kKeyCount;

    static std::span<const KeyDefinition> Definitions();
    static const KeyDefinition& Get(KeyId id);
};

}  // namespace keystats
