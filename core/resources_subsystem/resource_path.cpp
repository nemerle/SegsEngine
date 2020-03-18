#include "resource_path.h"
#include <assert.h>
/**
 * Construct ResourcePath object from a string path
 */
ResourcePath::ResourcePath(StringView sv) {
    if(sv.empty())
        return;

    auto idx = sv.find("://");
    if (idx!=String::npos) {
        m_mountpoint = sv.substr(0,idx+1); //+1 so that it contains the ':'
        sv = sv.substr(idx+3); 
    }
    if(sv[0]=='/') { // rooted path
        m_mountpoint="fs:";
    }
    FixedVector<StringView, 8> components;
    String::split_ref(components, sv, '/');

    if(sv.size()>2 && sv[1]==':') { // windows-like path
        m_mountpoint="fs:";
        components.push_front(sv.substr(0,1)); // drive letter into path component c:/aa -> fs:/c/aa
    }
    //NOTE: good point to add some pre-processing, if needed.
    m_path_components.assign(components.begin(),components.end());
}

ResourcePath &ResourcePath::cd(const ResourcePath &path) {
    assert(path.is_relative());
    for(const auto &component : path.components()) {
        if(component==".." && !m_path_components.empty())
            m_path_components.pop_back();
        else if(component!=".") // skip useless dots in path
            m_path_components.emplace_back(component);
    }
    return *this;
}

ResourcePath &ResourcePath::cleanup()
{
    return *this;
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
