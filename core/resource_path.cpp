#include "resource_path.h"

ResourcePath::ResourcePath(StringView sv) {
    FixedVector<StringView, 8> components;
    String::split_ref(components, sv, '/');

    if (!components.empty() && components.front().ends_with(':')) {
        m_mountpoint = components.front();
        components.pop_front();
    }
}

String ResourcePath::to_string() const {
    String res;
    size_t total_size=m_mountpoint.size()+1;
    for(const String &s: m_path_components)
        total_size+=s.size()+1;
    res.reserve(total_size);
    res.append(m_mountpoint);
    if(!m_mountpoint.empty())
        res+="/";
    for(const String &s: m_path_components) {
        res.append(s);
        if(&s!=&m_path_components.back())
            res.append("/");
    }
}
