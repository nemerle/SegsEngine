#pragma once

#include "core/reference.h"
#include "core/hash_map.h"

class ResourcePath;
namespace se {
    class UUID;
}

class GODOT_EXPORT ResourceManifest : public RefCounted {
    GDCLASS(ResourceManifest,RefCounted)
public:
    /**	Registers a new resource path in the manifest, optionally \a md5 of the resource can be provided */
    void register_resource(const se::UUID& uuid, const ResourcePath& filePath,const String &md5={});

    /**	Removes a resource from the manifest. */
    void unregister_resource(const se::UUID& uuid);
    /**
     * Find a resource with the provided UUID and fills \a resource_path.
     * \returns true if UUID was found, false otherwise.
     */
    bool uuid_to_file_path(const se::UUID& uuid, ResourcePath& resource_path) const;
    /**
     * Find a resource with the provided \a resource_path and fills \a uuid.
     * \returns true if resource was found, false otherwise.
     */
    bool file_path_to_uuid(const ResourcePath& resource_path,se::UUID& uuid) const;

    /**	Checks if provided UUID exists in the manifest. */
    bool exists(const se::UUID& uuid) const;

    /**	Checks if the provided resource path exists in the manifest. */
    bool exists(const ResourcePath& filePath) const;
private:
    String m_name; //!< Unique name for this manifest
    HashMap<se::UUID, ResourcePath> m_UUID_to_resource_path;
    HashMap<ResourcePath, se::UUID> m_resource_path_to_UUID;

};
