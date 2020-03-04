#pragma once

#include "core/vector.h"
#include "core/string.h"
#include "core/error_macros.h"

class GODOT_EXPORT ResourcePath {
    TmpString<8> m_mountpoint; // res: user: etc.
    Vector<String> m_path_components;
public:
    ResourcePath() = default;
    explicit ResourcePath(StringView sv);

    bool empty() const noexcept { return m_mountpoint.empty() && m_path_components.empty(); }
    void clear() noexcept {
        m_mountpoint.clear();
        m_mountpoint.shrink_to_fit();
        m_path_components.clear();
    }
    bool operator==(const ResourcePath &other) const noexcept {
        if(m_mountpoint.size()!=other.m_mountpoint.size() || m_path_components.size() != other.m_path_components.size())
            return false;

        if(m_mountpoint!=other.m_mountpoint)
            return false;

        for(uint32_t idx=0,fin=m_path_components.size(); idx<fin;++idx) {
            if(m_path_components[idx]!=other.m_path_components[idx])
                return false;
        }
        return true;
    }
    bool operator!=(const ResourcePath& other) const noexcept {
        return !(*this==other);
    }
    ResourcePath &set_mountpoint(StringView component) {
        // mountpoint name has to end with :
        ERR_FAIL_COND_V(component.back()!=':',*this);
        m_mountpoint.assign(component.begin(),component.end());
        return *this;
    }
    ResourcePath &cd(StringView component) {
        if(component==".." && !m_path_components.empty())
            m_path_components.pop_back();
        else
            m_path_components.emplace_back(component);
        return *this;
    }
    const Vector<String> &components() const { return m_path_components; }
    [[nodiscard]] ResourcePath meta_path() const;
    [[nodiscard]] ResourcePath import_path() const;
    [[nodiscard]] StringView leaf() const { return m_path_components.empty() ? StringView(m_mountpoint) : StringView(m_path_components.back()); }
    [[nodiscard]] bool is_relative() const { return m_mountpoint.empty(); }
    [[nodiscard]] bool references_nested_resource() const {
        for(const String &s: m_path_components)
            if(s.contains("::"))
                return true;
        return false;
    }
    [[nodiscard]] String to_string() const;
    [[nodiscard]] StringView mountpoint() const { return m_mountpoint; }
protected:
    friend eastl::hash<ResourcePath>;
    explicit operator size_t() const { // default hash uses this operator to retrieve hash.
        //TODO: consider hash caching
        eastl::hash<StringView> hasher;

        size_t hash_result = hasher(m_mountpoint);
        for(const String &s: m_path_components)
            hash_result = (hash_result<<2) ^ hasher(s);
        return hash_result;
    }
};