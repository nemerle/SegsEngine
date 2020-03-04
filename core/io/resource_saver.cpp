/*************************************************************************/
/*  resource_saver.cpp                                                   */
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

#include "resource_saver.h"
#include "core/method_info.h"
#include "core/class_db.h"
#include "core/io/resource_loader.h"
#include "core/io/image_saver.h"
#include "core/plugin_interfaces/ImageLoaderInterface.h"
#include "core/os/file_access.h"
#include "core/project_settings.h"
#include "core/string_utils.h"
#include "core/script_language.h"
#include "core/object_tooling.h"

#include "scene/resources/texture.h"
#include "EASTL/deque.h"

IMPL_GDCLASS(ResourceFormatSaver)

enum {
    MAX_SAVERS = 64
};

static eastl::deque<Ref<ResourceFormatSaver>> saver;

bool ResourceSaver::timestamp_on_save = false;
ResourceSavedCallback ResourceSaver::save_callback = nullptr;

Error ResourceFormatSaver::save(StringView p_path, const Ref<Resource> &p_resource, uint32_t p_flags) {

    if (get_script_instance() && get_script_instance()->has_method("save")) {
        return (Error)get_script_instance()->call("save", p_path, p_resource, p_flags).operator int64_t();
    }
    Ref<ImageTexture> texture = dynamic_ref_cast<ImageTexture>( p_resource );
    if(texture) {
        ERR_FAIL_COND_V_MSG(!texture->get_width(), ERR_INVALID_PARAMETER, "Can't save empty texture as PNG.");
        Ref<Image> img = texture->get_data();
        Ref<Image> source_image = prepareForPngStorage(img);
        return ImageSaver::save_image(p_path,source_image);
    }

    return ERR_METHOD_NOT_FOUND;
}

bool ResourceFormatSaver::recognize(const Ref<Resource> &p_resource) const {

    if (get_script_instance() && get_script_instance()->has_method("recognize")) {
        return get_script_instance()->call("recognize", p_resource).as<bool>();
    }
    return p_resource && p_resource->is_class("ImageTexture");
}

void ResourceFormatSaver::get_recognized_extensions(const Ref<Resource> &p_resource, Vector<String> &p_extensions) const {

    if (get_script_instance() && get_script_instance()->has_method("get_recognized_extensions")) {
        PoolVector<String> exts = get_script_instance()->call("get_recognized_extensions", p_resource).as<PoolVector<String>>();

        {
            PoolVector<String>::Read r = exts.read();
            for (int i = 0; i < exts.size(); ++i) {
                p_extensions.push_back(r[i]);
            }
        }
    }
    if (object_cast<ImageTexture>(p_resource.get())) {
        //TODO: use resource name here ?
        auto saver = ImageSaver::recognize("png");
        saver->get_saved_extensions(p_extensions);
    }
}

void ResourceFormatSaver::_bind_methods() {

    {
        PropertyInfo arg0 = PropertyInfo(VariantType::STRING, "path");
        PropertyInfo arg1 = PropertyInfo(VariantType::OBJECT, "resource", PropertyHint::ResourceType, "Resource");
        PropertyInfo arg2 = PropertyInfo(VariantType::INT, "flags");
        ClassDB::add_virtual_method(get_class_static_name(), MethodInfo(VariantType::INT, "save", eastl::move(arg0), eastl::move(arg1), eastl::move(arg2)));
    }

    ClassDB::add_virtual_method(get_class_static_name(), MethodInfo(VariantType::POOL_STRING_ARRAY, "get_recognized_extensions", PropertyInfo(VariantType::OBJECT, "resource", PropertyHint::ResourceType, "Resource")));
    ClassDB::add_virtual_method(get_class_static_name(), MethodInfo(VariantType::BOOL, "recognize", PropertyInfo(VariantType::OBJECT, "resource", PropertyHint::ResourceType, "Resource")));
}

Error ResourceSaver::save(StringView p_path, const RES &p_resource, uint32_t p_flags) {

    StringView extension = PathUtils::get_extension(p_path);
    Error err = ERR_FILE_UNRECOGNIZED;

    for (const Ref<ResourceFormatSaver> & s : saver) {

        if (!s->recognize(p_resource))
            continue;

        Vector<String> extensions;
        bool recognized = false;
        s->get_recognized_extensions(p_resource, extensions);

        for (auto & ext : extensions) {

            if (StringUtils::compare(ext,extension,StringUtils::CaseInsensitive) == 0)
                recognized = true;
        }

        if (!recognized)
            continue;

        const ResourcePath &old_path(p_resource->get_path());

        String local_path = ProjectSettings::get_singleton()->localize_path(p_path);

        RES rwcopy(p_resource);
        if (p_flags & FLAG_CHANGE_PATH)
            rwcopy->set_path(local_path);

        err = s->save(p_path, p_resource, p_flags);

        if (err == OK) {

            Object_set_edited(p_resource.get(),false);
#ifdef TOOLS_ENABLED
            if (timestamp_on_save) {
                uint64_t mt = FileAccess::get_modified_time(p_path);
                p_resource->set_last_modified_time(mt);
            }
#endif

            if (p_flags & FLAG_CHANGE_PATH)
                rwcopy->set_path(old_path);

            if (save_callback && StringUtils::begins_with(p_path,"res://"))
                save_callback(p_resource, p_path);

            return OK;
        }
    }

    return err;
}

void ResourceSaver::set_save_callback(ResourceSavedCallback p_callback) {

    save_callback = p_callback;
}

void ResourceSaver::get_recognized_extensions(const RES &p_resource, Vector<String> &p_extensions) {

    for (const Ref<ResourceFormatSaver> & s : saver) {

        s->get_recognized_extensions(p_resource, p_extensions);
    }
}

void ResourceSaver::add_resource_format_saver(const Ref<ResourceFormatSaver>& p_format_saver, bool p_at_front) {

    ERR_FAIL_COND_MSG(not p_format_saver, "It's not a reference to a valid ResourceFormatSaver object.");

    if (p_at_front) {
        saver.push_front(p_format_saver);
    } else {
        saver.push_back(p_format_saver);
    }
}

void ResourceSaver::remove_resource_format_saver(const Ref<ResourceFormatSaver>& p_format_saver) {

    ERR_FAIL_COND_MSG(not p_format_saver, "It's not a reference to a valid ResourceFormatSaver object.");
    // Find saver
    auto iter = eastl::find(saver.begin(), saver.end(),p_format_saver);
    ERR_FAIL_COND(iter == saver.end()); // Not found

    saver.erase(iter);
}

Ref<ResourceFormatSaver> ResourceSaver::_find_custom_resource_format_saver(StringView path) {
    for (const Ref<ResourceFormatSaver> & s : saver) {
        if (s->get_script_instance() && s->get_script_instance()->get_script()->get_path() == path) {
            return s;
        }
    }
    return Ref<ResourceFormatSaver>();
}

bool ResourceSaver::add_custom_resource_format_saver(StringView script_path) {

    if (_find_custom_resource_format_saver(script_path))
        return false;

    Ref<Script> s = ResourceLoader::load<Script>(script_path);
    ERR_FAIL_COND_V(not s, false);

    StringName ibt = s->get_instance_base_type();
    bool valid_type = ClassDB::is_parent_class(ibt, "ResourceFormatSaver");
    ERR_FAIL_COND_V_MSG(!valid_type, false, "Script does not inherit a CustomResourceSaver: " + String(script_path) + ".");

    Object *obj = ClassDB::instance(ibt);

    ERR_FAIL_COND_V_MSG(obj == nullptr, false, "Cannot instance script as custom resource saver, expected 'ResourceFormatSaver' inheritance, got: " + String(ibt) + ".");

    auto *crl = object_cast<ResourceFormatSaver>(obj);
    crl->set_script(s.get_ref_ptr());
    ResourceSaver::add_resource_format_saver(Ref<ResourceFormatSaver>(crl));

    return true;
}

void ResourceSaver::remove_custom_resource_format_saver(StringView script_path) {

    Ref<ResourceFormatSaver> custom_saver = _find_custom_resource_format_saver(script_path);
    if (custom_saver)
        remove_resource_format_saver(custom_saver);
}

void ResourceSaver::add_custom_savers() {
    // Custom resource savers exploits global class names

    StringName custom_saver_base_class(ResourceFormatSaver::get_class_static_name());

    Vector<StringName> global_classes;
    ScriptServer::get_global_class_list(&global_classes);

    for (const StringName &class_name : global_classes) {

        StringName base_class = ScriptServer::get_global_class_native_base(class_name);

        if (base_class == custom_saver_base_class) {
            StringView path = ScriptServer::get_global_class_path(class_name);
            add_custom_resource_format_saver(path);
        }
    }
}

void ResourceSaver::remove_custom_savers() {

    Vector<Ref<ResourceFormatSaver> > custom_savers;
    for (const Ref<ResourceFormatSaver> & s : saver) {
        if (s->get_script_instance()) {
            custom_savers.push_back(s);
        }
    }

    for (const Ref<ResourceFormatSaver> & saver : custom_savers) {
        remove_resource_format_saver(saver);
    }
}

void ResourceSaver::finalize()
{
    saver.clear();
}
