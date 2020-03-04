/* http://www.segs.dev/
 * Copyright (c) 2006 - 2020 SEGS Team (see AUTHORS.md)
 * This software is licensed under the terms of the 3-clause BSD License.
 * See LICENSE.md for details.
*/
#pragma once

#include "core/godot_export.h"
#include "core/forward_decls.h"

#include "EASTL/type_traits.h"

namespace eastl {
template <typename T> struct hash;
}
namespace se {
class GODOT_EXPORT UUID {
public:
    /** Create an empty UUID. */
    constexpr UUID() noexcept = default;
    /** Build UUID directly from provided data*/
    constexpr UUID(uint32_t a, uint32_t b, uint32_t c, uint32_t d) noexcept
    : m_data{a, b, c, d}
    { }
    /** Extract UUID from provided string representation. */
    explicit UUID(const String& uuid) noexcept;

    constexpr bool operator==(const UUID& rhs) const noexcept
    {
        return m_data[0] == rhs.m_data[0] &&
               m_data[1] == rhs.m_data[1] &&
               m_data[2] == rhs.m_data[2] &&
               m_data[3] == rhs.m_data[3];
    }
    constexpr bool operator!=(const UUID& rhs) const noexcept {
        return !(*this==rhs);
    }

    /** Checks has the UUID been initialized to a valid value. */
    constexpr bool valid() const noexcept
    {
        return *this != UUID();
    }
    /** Converts the UUID into its string representation. */
    String to_string() const;

    static UUID EMPTY;
    static UUID generate();
private:
    friend struct eastl::hash<UUID>;
    explicit constexpr operator size_t() const {
        // Since we either have generated a random value for uuid, or have an empty one, we use first component as hash
        return m_data[0];
    }
    uint32_t m_data[4] = {0, 0, 0, 0};

};
static_assert (eastl::is_trivially_copyable_v<UUID>,"UUID is not trivially copyable");
} // namespace se
