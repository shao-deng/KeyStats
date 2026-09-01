#pragma once

#include "key_id.hpp"

#include <optional>

namespace keystats {

struct KeyProcessResult {
    std::optional<KeyId> key_id;
    bool recognized = false;
    bool counted = false;

    static KeyProcessResult Unrecognized() { return {}; }

    static KeyProcessResult RecognizedOnly(KeyId id) { return {id, true, false}; }

    static KeyProcessResult CountedPress(KeyId id) { return {id, true, true}; }
};

}  // namespace keystats
