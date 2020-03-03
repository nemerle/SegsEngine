#pragma once

#include "core/forward_decls.h"
#include "core/reference.h"
#include "core/resources_subsystem/resource.h"

#include "EASTL/shared_ptr.h"

class ResourceManifest;

class GODOT_EXPORT ResourceManager {
public:
    se::UUID get_uuid_for_resource(const RES&);

    /**
     * Loads the resource from a given path. Returns an empty handle if resource can't be loaded. Resource is loaded
     * synchronously.
     *
     * @param[in]	filePath	File path to the resource to load. This can be absolute or relative to the working
     *							folder.
     * @param[in]	loadFlags	Flags used to control the load process.
     *
     * @see		release(ResourceHandleBase&), unloadAllUnused()
     */
    INVOCABLE HResource load(const ResourcePath& filePath, se::ResourceLoadFlags loadFlags = se::ResourceLoadFlag::Default);

    /**
     * Loads the resource given a string. The code will check if provided string is UUID or a path.
     * @returns an empty handle if resource can't be loaded.
     * @note Resource is loaded synchronously.
     *
     * @param[in]	filePath	File path to the resource to load. This can be absolute or relative to the working
     *							folder.
     * @param[in]	loadFlags	Flags used to control the load process.
     *
     * @see		release(ResourceHandleBase&), unloadAllUnused()
     */
    INVOCABLE HResource load(StringView sv, se::ResourceLoadFlags loadFlags = se::ResourceLoadFlag::Default);
    /** @copydoc load(const Path&, ResourceLoadFlags) */
    template <class T>
    se::ResourceHandle<T> load(StringView filePath, se::ResourceLoadFlags loadFlags = se::ResourceLoadFlag::Default)
    {
        return se::static_resource_cast<T>(load(filePath, loadFlags));
    }

    /** @copydoc load(const WeakResourceHandle<Resource>&, ResourceLoadFlags) */
    template <class T>
    se::ResourceHandle<T> load(const se::WeakResourceHandle<T>& handle, se::ResourceLoadFlags loadFlags = se::ResourceLoadFlag::Default)
    {
        return se::static_resource_cast<T>(load((const se::WeakResourceHandle<Resource>&)handle, loadFlags));
    }
    /**
     * Loads the resource with the given UUID. Returns an empty handle if resource can't be loaded.
     *
     * @param[in]	uuid		UUID of the resource to load.
     * @param[in]	async		If true resource will be loaded asynchronously. Handle to non-loaded resource will be
     *							returned immediately while loading will continue in the background.
     * @param[in]	loadFlags	Flags used to control the load process.
     *
     * @see		load(const Path&, bool)
     */
    INVOCABLE HResource loadFromUUID(const se::UUID& uuid, bool async = false, se::ResourceLoadFlags loadFlags = se::ResourceLoadFlag::Default);

    /**
     * Updates an existing resource handle with a new resource. Caller must ensure that new resource type matches the
     * original resource type.
     */
    void update(HResource& handle, const Ref<Resource>& resource);
private:
    Vector<SPtr<ResourceManifest>> mResourceManifests;
    SPtr<ResourceManifest> mDefaultResourceManifest;
};
/** Provides easier access to Resources manager. */
GODOT_EXPORT ResourceManager& gResourceManager();
