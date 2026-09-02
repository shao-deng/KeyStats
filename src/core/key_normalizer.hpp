#pragma once

#include "key_id.hpp"
#include "raw_key_event.hpp"

#include <optional>

namespace keystats {

class KeyNormalizer {
public:
    static std::optional<KeyId> Normalize(const RawKeyEvent& key_event);
};

}  // namespace keystats
