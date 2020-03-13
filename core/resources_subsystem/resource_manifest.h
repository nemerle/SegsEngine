#pragma once

#include "core/reference.h"
#include "core/hash_map.h"
#include "core/string.h"
#include "core/uuid.h"
#include "core/resources_subsystem/resource_path.h"

class ResourcePath;

class GODOT_EXPORT ResourceManifest : public RefCounted {
    GDCLASS(ResourceManifest,RefCounted)
public:
    /**	Registers a new resource path in the manifest
     *@note Registering a new resource path for a resource that already exists will override the path
     */
    void register_resource(const se::UUID& uuid, const ResourcePath& filePath);

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
