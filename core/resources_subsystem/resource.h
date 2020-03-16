/*************************************************************************/
/*  resource.h                                                           */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2019 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2019 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#pragma once
#include "core/forward_decls.h"
#include "core/reference.h"
#include "core/resources_subsystem/resource_path.h"
#include "core/uuid.h"
#include "core/flags.h"

#include "EASTL/shared_ptr.h"
#include <utility>
#include <atomic>

namespace eastl {
template <typename Key, typename T, typename Compare, typename Allocator>
class map;
}
template <class T>
struct Comparator;
class wrap_allocator;
template <class K,class V>
using DefMap = eastl::map<K,V,Comparator<K>,wrap_allocator>;
class RWLock;

class ResourceManager;
class Resource;

//The code below is also based on BSF
//************************************ bs::framework - Copyright 2018 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
namespace se {

/** Flags that can be used to control resource loading. */
enum class ResourceLoadFlag
{
    /** No flags. */
    None = 0,
    /** If enabled all resources referenced by the root resource will be loaded as well. */
    LoadDependencies = 1 << 0,
    /**
     * If enabled the resource system will keep an internal reference to the resource so it doesn't get destroyed when
     * it goes out of scope. You can call ResourceManager::release() to release the internal reference. Each call to load will
     * create a new internal reference and therefore must be followed by the same number of release calls. If
     * dependencies are being loaded, they will not have internal references created regardless of this parameter.
     */
    KeepInternalRef = 1 << 1,
    /**
     * Determines if the loaded resource keeps original data loaded. Sometime resources will process loaded data
     * and discard the original (e.g. uncompressing audio on load). This flag can prevent the resource from discarding
     * the original data. The original data might be required for saving the resource (via Resources::save), but will
     * use up extra memory. Normally you want to keep this enabled if you plan on saving the resource to disk.
     */
    KeepSourceData = 1 << 2,
    /**
     * Determines if the load 'request' should skip cached resource
     */
    SkipCache = 1 << 3,
    /** Default set of flags used for resource loading. */
    Default = LoadDependencies | KeepInternalRef
};
typedef Flags<ResourceLoadFlag> ResourceLoadFlags;
SE_FLAGS_OPERATORS(ResourceLoadFlag);

/**	Data that is shared between all resource handles. */
struct GODOT_EXPORT ResourceHandleData
{
    Ref<Resource> m_ptr;
    se::UUID m_UUID;
    bool m_is_created = false;
    std::atomic<uint32_t> m_ref_count { 0 };
};
/**
     * Represents a handle to a resource. Handles are similar to a smart pointers, but they have two advantages:
     *	- When loading a resource asynchronously you can be immediately returned the handle that you may use throughout
     *    the engine. The handle will be made valid as soon as the resource is loaded.
     *	- Handles can be serialized and deserialized, therefore saving/restoring references to their original resource.
     */
class GODOT_EXPORT ResourceHandleBase {
public:
    /**
     * Checks if the resource is loaded. Until resource is loaded this handle is invalid and you may not get the
     * internal resource from it.
     *
     * @param[in]	checkDependencies	If true, and if resource has any dependencies, this method will also check if
     *									they are loaded.
     */
    bool is_loaded(bool checkDependencies = true) const;
    /**
     * Releases an internal reference to this resource held by the resources system, if there is one.
     *
     * @see		Resources::release(ResourceHandleBase&)
     */
    void release();

    /** Returns the UUID of the resource the handle is referring to. */
    const UUID& get_uuid() const noexcept { return m_data != nullptr ? m_data->m_UUID : UUID::EMPTY; }
protected:
    /**	Destroys the resource the handle is pointing to. */
    void destroy();
    /**
     * Sets the created flag to true and assigns the resource pointer. Called by the constructors, or if you
     * constructed just using a UUID, then you need to call this manually before you can access the resource from
     * this handle.
     *
     * @note
     * This is needed because two part construction is required due to  multithreaded nature of resource loading.
     * @note
     * Internal method.
     */
    void setHandleData(const Ref<Resource> &ptr, const UUID& uuid);

    /**	Gets the handle data. For internal use only. */
    const SPtr<ResourceHandleData>& getHandleData() const { return m_data; }

    /**
     * Clears the created flag and the resource pointer, making the handle invalid until the resource is loaded again
     * and assigned through setHandleData().
     */
    void clearHandleData();

    /** Increments the reference count of the handle. Only to be used by Resources for keeping internal references. */
    void addInternalRef();

    /** Decrements the reference count of the handle. Only to be used by Resources for keeping internal references. */
    void removeInternalRef();

    /**
     * Notification sent by the resource system when the resource is done with the loading process. This will trigger
     * even if the load fails.
     */
    void notifyLoadComplete();
protected:

    /**
     * @note
     * All handles to the same source must share this same handle data. Otherwise things like counting number of
     * references or replacing pointed to resource become impossible without additional logic. */
    SPtr<ResourceHandleData> m_data;
};

/**
 * @copydoc	ResourceHandleBase
 *
 * Handles differences in reference counting depending if the handle is normal or weak.
 */
template <bool WeakHandle>
class GODOT_EXPORT ResourceHandleBaseT : public ResourceHandleBase
{ };

/**	Specialization of ResourceHandleBaseT for weak handles. Weak handles do no reference counting. */
template<>
class GODOT_EXPORT ResourceHandleBaseT<true> : public ResourceHandleBase
{
protected:
    void addRef() { }
    void releaseRef() { }
};
/**	Specialization of ResourceHandleBaseT for normal (non-weak) handles. */
template<>
class GODOT_EXPORT ResourceHandleBaseT<false> : public ResourceHandleBase
{
protected:
    void addRef()
    {
        if (m_data)
            m_data->m_ref_count.fetch_add(1, std::memory_order_relaxed);
    }

    void releaseRef()
    {
        if (!m_data)
            return;

        std::uint32_t refCount = m_data->m_ref_count.fetch_sub(1, std::memory_order_release);

        if (refCount == 1)
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            destroy();
        }
    }
};
/** @copydoc ResourceHandleBase */
template <typename T, bool WeakHandle>
class ResourceHandleT : public ResourceHandleBaseT<WeakHandle> {
public:
    ResourceHandleT() = default;

    ResourceHandleT(std::nullptr_t) { }

    /**	Copy constructor. */
    ResourceHandleT(const ResourceHandleT& other)
    {
        this->mData = other.getHandleData();
        this->addRef();
    }
    /** Move constructor. */
    ResourceHandleT(ResourceHandleT&& other) = default;

    ~ResourceHandleT()
    {
        this->releaseRef();
    }
    explicit operator bool() const
    {
        return this->m_data != nullptr && this->m_data->m_UUID.valid();
    }
    /**
     * Returns internal resource pointer.
     *
     * @note	Throws exception if handle is invalid.
     */
    T* operator->() const { return get(); }

    /**
     * Returns internal resource pointer and dereferences it.
     *
     * @note	Throws exception if handle is invalid.
     */
    T& operator*() const { return *get(); }

    /** Clears the handle making it invalid and releases any references held to the resource. */
    ResourceHandleT<T, WeakHandle>& operator=(std::nullptr_t ptr)
    {
        this->releaseRef();
        this->mData = nullptr;

        return *this;
    }
    /**	Copy assignment. */
    ResourceHandleT<T, WeakHandle>& operator=(const ResourceHandleT<T, WeakHandle>& rhs)
    {
        setHandleData(rhs.getHandleData());
        return *this;
    }

    /**	Move assignment. */
    ResourceHandleT& operator=(ResourceHandleT&& other)
    {
        if (this == &other)
            return *this;

        this->releaseRef();
        this->m_data = std::exchange(other.m_data, nullptr);

        return *this;
    }
    /**
     * Returns internal resource pointer and dereferences it.
     *
     * @note Throws exception if handle is invalid.
     */
    T* get() const
    {
        assert(this->is_loaded(false)); // this->throwIfNotLoaded();

        return reinterpret_cast<T*>(this->m_data->m_ptr.get());
    }
    /** Converts a handle into a weak handle. */
    ResourceHandleT<T, true> getWeak() const
    {
        ResourceHandleT<T, true> handle;
        handle.setHandleData(this->getHandleData());

        return handle;
    }
protected:
    friend ::ResourceManager;
    template<class _T, bool _Weak>
    friend class ResourceHandleT;
    template<class _Ty1, class _Ty2, bool _Weak2, bool _Weak1>
    friend ResourceHandleT<_Ty1, _Weak1> static_resource_cast(const ResourceHandleT<_Ty2, _Weak2>& other);
    template<class _Ty1, class _Ty2, bool _Weak2>
    friend ResourceHandleT<_Ty1, false> static_resource_cast(const ResourceHandleT<_Ty2, _Weak2>& other);
    /**
     * Constructs a new valid handle for the provided resource with the provided UUID.
     *
     * @note	Handle will take ownership of the provided resource pointer, so make sure you don't delete it elsewhere.
     */
    explicit ResourceHandleT(T* ptr, const UUID& uuid) : ResourceHandleBaseT<WeakHandle>()
    {
        this->m_data = eastl::allocate_shared<ResourceHandleData>(wrap_allocator());
        this->addRef();

        this->setHandleData(RES(ptr), uuid);
        this->mIsCreated = true;
    }
    /**
     * Constructs an invalid handle with the specified UUID. You must call setHandleData() with the actual resource
     * pointer to make the handle valid.
     */
    ResourceHandleT(const UUID& uuid)
    {
        this->m_data = eastl::allocate_shared<ResourceHandleData>(wrap_allocator());
        this->m_data->m_UUID = uuid;

        this->addRef();
    }

    /**	Constructs a new valid handle for the provided resource with the provided UUID. */
    ResourceHandleT(const Ref<T> &ptr, const UUID& uuid)
    {
        this->m_data = eastl::allocate_shared<ResourceHandleData>(wrap_allocator());
        this->addRef();

        this->setHandleData(ptr, uuid);
        this->m_data->m_is_created = true;
    }

    /**	Replaces the internal handle data pointer, effectively transforming the handle into a different handle. */
    void setHandleData(const SPtr<ResourceHandleData>& data)
    {
        this->releaseRef();
        this->m_data = data;
        this->addRef();
    }

    /**	Converts a weak handle into a normal handle. */
    ResourceHandleT<T, false> lock() const
    {
        ResourceHandleT<T, false> handle;
        handle.setHandleData(this->getHandleData());

        return handle;
    }

    using ResourceHandleBase::setHandleData;
};
/**	Checks if two handles point to the same resource. */
template<class _Ty1, bool _Weak1, class _Ty2, bool _Weak2>
bool operator==(const ResourceHandleT<_Ty1, _Weak1>& _Left, const ResourceHandleT<_Ty2, _Weak2>& _Right)
{
    if (_Left.getHandleData() != nullptr && _Right.getHandleData() != nullptr)
        return _Left.getHandleData()->mPtr == _Right.getHandleData()->mPtr;

    return _Left.getHandleData() == _Right.getHandleData();
}

/**	Checks if a handle is null. */
template<class _Ty1, bool _Weak1, class _Ty2, bool _Weak2>
bool operator==(const ResourceHandleT<_Ty1, _Weak1>& _Left, std::nullptr_t  _Right)
{
    return _Left.getHandleData() == nullptr || _Left.getHandleData()->mUUID.empty();
}

template<class _Ty1, bool _Weak1, class _Ty2, bool _Weak2>
bool operator!=(const ResourceHandleT<_Ty1, _Weak1>& _Left, const ResourceHandleT<_Ty2, _Weak2>& _Right)
{
    return (!(_Left == _Right));
}

/** @addtogroup Resources
 *  @{
 */

 /** @copydoc ResourceHandleBase */
template <typename T>
using ResourceHandle = ResourceHandleT<T, false>;

/**
 * @copydoc ResourceHandleBase
 *
 * Weak handles don't prevent the resource from being unloaded.
 */
template <typename T>
using WeakResourceHandle = ResourceHandleT<T, true>;

/**	Casts one resource handle to another. */
template<class _Ty1, class _Ty2, bool _Weak2, bool _Weak1>
ResourceHandleT<_Ty1, _Weak1> static_resource_cast(const ResourceHandleT<_Ty2, _Weak2>& other)
{
    ResourceHandleT<_Ty1, _Weak1> handle;
    handle.setHandleData(other.getHandleData());

    return handle;
}

/**	Casts one resource handle to another. */
template<class _Ty1, class _Ty2, bool _Weak2>
ResourceHandleT<_Ty1, false> static_resource_cast(const ResourceHandleT<_Ty2, _Weak2>& other)
{
    ResourceHandleT<_Ty1, false> handle;
    assert(!other.get() || ObjectNS::cast_to<_Ty1>(other.get())!=nullptr);

    handle.setHandleData(other.getHandleData());

    return handle;
}

/** @} */

} // end of se namespace

// Code below is original

#define RES_BASE_EXTENSION_IMPL(m_class,m_ext)                                                                      \
                                                                                                                    \
void m_class::register_custom_data_to_otdb() {                                                                      \
    ClassDB::add_resource_base_extension(StringName(m_ext), get_class_static_name());                               \
}

#define RES_BASE_EXTENSION(m_ext)                                                                                   \
public:                                                                                                             \
    StringName get_base_extension() const override { return StringName(m_ext); }                                    \
                                                                                                                    \
    static void register_custom_data_to_otdb();                                                                     \
private:

class GODOT_EXPORT IResourceTooling {
public:
    virtual uint32_t hash_edited_version() const = 0;

    virtual void set_last_modified_time(uint64_t p_time) = 0;
    virtual uint64_t get_last_modified_time() const = 0;

    virtual void set_import_last_modified_time(uint64_t p_time) = 0;
    virtual uint64_t get_import_last_modified_time() const = 0;

    virtual void set_import_path(StringView p_path) = 0;
    virtual UIString get_import_path() const = 0;
    virtual ~IResourceTooling() = default;
};

class GODOT_EXPORT Resource : public RefCounted {

    GDCLASS(Resource,RefCounted)
//    Q_GADGET
//    Q_CLASSINFO("Category","Resources")
//    Q_PROPERTY(bool resource_local_to_scene READ is_local_to_scene WRITE set_local_to_scene )
    OBJ_CATEGORY("Resources")
private:
    friend class ResBase;
    friend class ResourceCache;
    friend class SceneState;
    struct Data;
    Data *impl_data;

#ifdef TOOLS_ENABLED
    uint64_t last_modified_time;
    uint64_t import_last_modified_time;
#endif
    virtual bool _use_builtin_script() const { return true; }

    /** A list of all other resources this resource depends on. */
    Vector<se::WeakResourceHandle<Resource>> m_dependencies;
protected:
    void emit_changed();

    void notify_change_to_owners();

    virtual void _resource_path_changed();
    static void _bind_methods();
public:
    void _set_path(StringView p_path);
    void _take_over_path(StringView p_path);
//Q_SIGNALS:
    void changed();
public:
    virtual StringName get_base_extension() const { return StringName("res"); }
    static void register_custom_data_to_otdb();

    static Node *(*_get_local_scene_func)(); //used by editor

    virtual bool editor_can_reload_from_file();
    virtual void reload_from_file();

    void register_owner(Object *p_owner);
    void unregister_owner(Object *p_owner);

    void set_name(StringView p_name);
    const String &get_name() const;

    virtual void set_path(const ResourcePath &p_path, bool p_take_over = false);
    const ResourcePath &get_path() const;

    void set_subindex(int p_sub_index);
    int get_subindex() const;

    virtual Ref<Resource> duplicate(bool p_subresources = false) const;
    Ref<Resource> duplicate_for_local_scene(Node *p_for_scene, DefMap<Ref<Resource>, Ref<Resource> > &remap_cache);
    void configure_for_local_scene(Node *p_for_scene, DefMap<Ref<Resource>, Ref<Resource> > &remap_cache);

    /*Q_INVOKABLE*/ void set_local_to_scene(bool p_enable);
    /*Q_INVOKABLE*/ bool is_local_to_scene() const;

    virtual void setup_local_to_scene();

    Node *get_local_scene() const;

    //IResourceTooling * get_resource_tooling() const;
#ifdef TOOLS_ENABLED

    uint32_t hash_edited_version() const;

    virtual void set_last_modified_time(uint64_t p_time) { last_modified_time = p_time; }
    uint64_t get_last_modified_time() const { return last_modified_time; }

    virtual void set_import_last_modified_time(uint64_t p_time) { import_last_modified_time = p_time; }
    uint64_t get_import_last_modified_time() const { return import_last_modified_time; }

    void set_import_path(StringView p_path);
    const String &get_import_path() const;

#endif

    void set_as_translation_remapped(bool p_remapped);
    bool is_translation_remapped() const;

    virtual RID get_rid() const; // some resources may offer conversion to RID

#ifdef TOOLS_ENABLED
    //helps keep IDs same number when loading/saving scenes. -1 clears ID and it Returns -1 when no id stored
    //void set_id_for_path(StringView p_path, int p_id);
    //int get_id_for_path(StringView p_path) const;
#endif

    Resource();
    ~Resource() override;
};

using RES = Ref<Resource>;

class GODOT_EXPORT ResourceCache {
    friend class Resource;
    friend class ResourceLoader; //need the lock
    friend void unregister_core_types();
    friend void register_core_types();

    static RWLock *lock;
    static void clear();
    static void setup();
    static Resource *get_unguarded(const ResourcePath &p_path);
public:
    static void reload_externals();
    static bool has(const ResourcePath &p_path);
    static Resource *get(const ResourcePath &p_path);
    static void dump(StringView p_file = nullptr, bool p_short = false);
    static void get_cached_resources(List<Ref<Resource>> *p_resources);
    static int get_cached_resource_count();
};

using HResource = se::ResourceHandle<Resource>;
using HScript = se::ResourceHandle<class Script>;
using HAnimation = se::ResourceHandle<class Animation>;
using HTexture = se::ResourceHandle<class Texture>;
using HPackedScene = se::ResourceHandle<class PackedScene>;
