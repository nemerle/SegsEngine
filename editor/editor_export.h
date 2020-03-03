/*************************************************************************/
/*  editor_export.h                                                      */
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

#include "core/list.h"
#include "core/set.h"
#include "core/map.h"
#include "core/os/dir_access.h"
#include "core/resources_subsystem/resource.h"
#include "core/property_info.h"
#include "scene/main/node.h"
#include "scene/main/timer.h"
#include "scene/resources/texture.h"
class FileAccess;
class EditorExportPlatform;
class EditorFileSystemDirectory;
struct EditorProgress;

class EditorExportPreset : public RefCounted {

    GDCLASS(EditorExportPreset,RefCounted)

public:
    enum ExportFilter {
        EXPORT_ALL_RESOURCES,
        EXPORT_SELECTED_SCENES,
        EXPORT_SELECTED_RESOURCES,
    };

    enum ScriptExportMode {
        MODE_SCRIPT_TEXT,
        MODE_SCRIPT_COMPILED,
        MODE_SCRIPT_ENCRYPTED,
    };

private:
    Ref<EditorExportPlatform> platform;
    ExportFilter export_filter;
    String include_filter;
    String exclude_filter;
    String export_path;
    String exporter;
    String name;
    String custom_features;
    String script_key;

    Set<String> selected_files;
    Vector<String> patches;
    Vector<PropertyInfo> properties;
    HashMap<StringName, Variant> values;

    int script_mode;
    bool runnable;

    friend class EditorExport;
    friend class EditorExportPlatform;
protected:
    bool _set(const StringName &p_name, const Variant &p_value);
    bool _get(const StringName &p_name, Variant &r_ret) const;
    void _get_property_list(Vector<PropertyInfo> *p_list) const;

public:
    Ref<EditorExportPlatform> get_platform() const;

    bool has(const StringName &p_property) const { return values.contains(p_property); }

    Vector<String> get_files_to_export() const;

    void add_export_file(StringView p_path);
    void remove_export_file(StringView p_path);
    bool has_export_file(StringView p_path);

    void set_name(StringView p_name);
    const String &get_name() const;

    void set_runnable(bool p_enable);
    bool is_runnable() const;

    void set_export_filter(ExportFilter p_filter);
    ExportFilter get_export_filter() const;

    void set_include_filter(StringView p_include);
    const String &get_include_filter() const;

    void set_exclude_filter(StringView p_exclude);
    const String &get_exclude_filter() const;

    void add_patch(StringView p_path, int p_at_pos = -1);
    void set_patch(int p_index, StringView p_path);
    const String &get_patch(int p_index);
    void remove_patch(int p_idx);
    const Vector<String> &get_patches() const { return patches; }

    void set_custom_features(StringView p_custom_features);
    const String & get_custom_features() const;

    void set_export_path(StringView p_path);
    const String &get_export_path() const;

    void set_script_export_mode(int p_mode);
    int get_script_export_mode() const;

    void set_script_encryption_key(const String &p_key);
    const String &get_script_encryption_key() const;

    const Vector<PropertyInfo> &get_properties() const { return properties; }

    EditorExportPreset();
};

struct SharedObject {
    String path;
    Vector<String> tags;

    SharedObject(StringView p_path, const Vector<String> &p_tags) :
            path(p_path),
            tags(p_tags) {
    }

    SharedObject() {}
};

class EditorExportPlatform : public RefCounted {

    GDCLASS(EditorExportPlatform,RefCounted)

public:
    using EditorExportSaveFunction = Error (*)(void *, StringView, const Vector<uint8_t> &, int, int);
    using EditorExportSaveSharedObject = Error (*)(void *, const SharedObject &);

private:
    struct FeatureContainers {
        Set<String> features;
        PoolVector<String> features_pv;
    };

    void _export_find_resources(EditorFileSystemDirectory *p_dir, Set<String> &p_paths);
    void _export_find_dependencies(StringView p_path, Set<String> &p_paths);

    void gen_debug_flags(Vector<String> &r_flags, int p_flags);
    static Error _save_pack_file(void *p_userdata, StringView p_path, const Vector<uint8_t> &p_data, int p_file, int p_total);
    static Error _save_zip_file(void *p_userdata, StringView p_path, const Vector<uint8_t> &p_data, int p_file, int p_total);

    void _edit_files_with_filter(DirAccess *da, const Vector<String> &p_filters, Set<String> &r_list, bool exclude);
    void _edit_filter_list(Set<String> &r_list, StringView p_filter, bool exclude);

    static Error _add_shared_object(void *p_userdata, const SharedObject &p_so);

protected:
    struct ExportNotifier {
        ExportNotifier(EditorExportPlatform &p_platform, const Ref<EditorExportPreset> &p_preset, bool p_debug, StringView p_path, int p_flags);
        ~ExportNotifier();
    };

    FeatureContainers get_feature_containers(const Ref<EditorExportPreset> &p_preset);

    bool exists_export_template(StringView template_file_name, String *err) const;
    String find_export_template(StringView template_file_name, String *err = nullptr) const;
    void gen_export_flags(Vector<String> &r_flags, int p_flags);

public:
    virtual void get_preset_features(const Ref<EditorExportPreset> &p_preset, Vector<String> *r_features) = 0;

    struct ExportOption {
        PropertyInfo option;
        Variant default_value;

        ExportOption(const PropertyInfo &p_info, const Variant &p_default) :
                option(p_info),
                default_value(p_default) {
        }
        ExportOption() {}
    };

    virtual Ref<EditorExportPreset> create_preset();

    virtual void get_export_options(Vector<ExportOption> *r_options) = 0;
    virtual bool get_option_visibility(const StringName &p_option, const HashMap<StringName, Variant> &p_options) const { return true; }

    virtual const String & get_os_name() const = 0;
    virtual const String & get_name() const = 0;
    virtual Ref<Texture> get_logo() const = 0;

    Error export_project_files(const Ref<EditorExportPreset> &p_preset, EditorExportSaveFunction p_func, void *p_udata, EditorExportSaveSharedObject p_so_func = nullptr);

    Error save_pack(const Ref<EditorExportPreset> &p_preset, StringView p_path, Vector<SharedObject> *p_so_files = nullptr, bool p_embed = false, int64_t *r_embedded_start = nullptr, int64_t *r_embedded_size = nullptr);
    Error save_zip(const Ref<EditorExportPreset> &p_preset, const String &p_path);

    virtual bool poll_export() { return false; }
    virtual int get_options_count() const { return 0; }
    virtual const String & get_options_tooltip() const { return null_string; }
    virtual Ref<ImageTexture> get_option_icon(int p_index) const;
    virtual StringName get_option_label(int p_device) const { return StringName(); }
    virtual StringName get_option_tooltip(int p_device) const { return StringName(); }

    enum DebugFlags {
        DEBUG_FLAG_DUMB_CLIENT = 1,
        DEBUG_FLAG_REMOTE_DEBUG = 2,
        DEBUG_FLAG_REMOTE_DEBUG_LOCALHOST = 4,
        DEBUG_FLAG_VIEW_COLLISONS = 8,
        DEBUG_FLAG_VIEW_NAVIGATION = 16,
    };

    virtual Error run(const Ref<EditorExportPreset> &p_preset, int p_device, int p_debug_flags) { return OK; }
    virtual Ref<Texture> get_run_icon() const { return get_logo(); }

    StringName test_etc2() const; //generic test for etc2 since most platforms use it
    virtual bool can_export(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates) const = 0;

    virtual Vector<String> get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const = 0;
    virtual Error export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, StringView p_path, int p_flags = 0) = 0;
    virtual Error export_pack(const Ref<EditorExportPreset> &p_preset, bool p_debug, StringView p_path, int p_flags = 0);
    virtual Error export_zip(const Ref<EditorExportPreset> &p_preset, bool p_debug, StringView p_path, int p_flags = 0);
    virtual void get_platform_features(Vector<String> *r_features) = 0;
    virtual void resolve_platform_feature_priorities(const Ref<EditorExportPreset> &p_preset, Set<String> &p_features) = 0;

    EditorExportPlatform();
};

class EditorExportPlugin : public RefCounted {
    GDCLASS(EditorExportPlugin,RefCounted)

    friend class EditorExportPlatform;

    Ref<EditorExportPreset> export_preset;

    Vector<SharedObject> shared_objects;
    struct ExtraFile {
        String path;
        Vector<uint8_t> data;
        bool remap;
    };
    Vector<ExtraFile> extra_files;
    bool skipped;

    _FORCE_INLINE_ void _clear() {
        shared_objects.clear();
        extra_files.clear();
        skipped = false;
    }

    _FORCE_INLINE_ void _export_end() {
    }

    void _export_file_script(StringView p_path, StringView p_type, const PoolVector<String> &p_features);
    void _export_begin_script(const PoolVector<String> &p_features, bool p_debug, StringView p_path, int p_flags);
    void _export_end_script();

protected:
    void set_export_preset(const Ref<EditorExportPreset> &p_preset);
    Ref<EditorExportPreset> get_export_preset() const;
public: // exposed to scripting
    void add_file(StringView p_path, const Vector<uint8_t> &p_file, bool p_remap);
    void add_shared_object(StringView p_path, const Vector<String> &tags);
    void skip();
protected:

    virtual void _export_file(StringView p_path, StringView p_type, const Set<String> &p_features);
    virtual void _export_begin(const Set<String> &p_features, bool p_debug, StringView p_path, int p_flags);

    static void _bind_methods();

public:

    EditorExportPlugin();
};

class EditorExport : public Node {
    GDCLASS(EditorExport,Node)

    Vector<Ref<EditorExportPlatform> > export_platforms;
    Vector<Ref<EditorExportPreset> > export_presets;
    Vector<Ref<EditorExportPlugin> > export_plugins;

    Timer *save_timer;
    bool block_save;

    static EditorExport *singleton;

    void _save();

protected:
    friend class EditorExportPreset;
    void save_presets();

    void _notification(int p_what);
    static void _bind_methods();

public:
    static EditorExport *get_singleton() { return singleton; }

    void add_export_platform(const Ref<EditorExportPlatform> &p_platform);
    int get_export_platform_count();
    Ref<EditorExportPlatform> get_export_platform(int p_idx);

    void add_export_preset(const Ref<EditorExportPreset> &p_preset, int p_at_pos = -1);
    int get_export_preset_count() const;
    Ref<EditorExportPreset> get_export_preset(int p_idx);
    void remove_export_preset(int p_idx);

    void add_export_plugin(const Ref<EditorExportPlugin> &p_plugin);
    void remove_export_plugin(const Ref<EditorExportPlugin> &p_plugin);
    const Vector<Ref<EditorExportPlugin> > &get_export_plugins();

    void load_config();

    bool poll_export_platforms();

    EditorExport();
    ~EditorExport() override;
};

class EditorExportPlatformPC : public EditorExportPlatform {

    GDCLASS(EditorExportPlatformPC,EditorExportPlatform)

public:
    using FixUpEmbeddedPckFunc = Error (*)(StringView, int64_t, int64_t);

private:
    Ref<ImageTexture> logo;
    String name;
    String os_name;
    Map<String, String> extensions;

    String release_file_32;
    String release_file_64;
    String debug_file_32;
    String debug_file_64;

    Set<String> extra_features;

    int chmod_flags;

    FixUpEmbeddedPckFunc fixup_embedded_pck_func;

public:
    void get_preset_features(const Ref<EditorExportPreset> &p_preset, Vector<String> *r_features) override;

    void get_export_options(Vector<ExportOption> *r_options) override;

    const String &get_name() const override;
    const String &get_os_name() const override;
    Ref<Texture> get_logo() const override;

    bool can_export(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates) const override;
    Vector<String> get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const override;
    Error export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, StringView p_path, int p_flags = 0) override;
    virtual Error sign_shared_object(const Ref<EditorExportPreset> &p_preset, bool p_debug, StringView p_path);

    void set_extension(StringView p_extension, StringView p_feature_key = "default");
    void set_name(StringView p_name);
    void set_os_name(StringView p_name);

    void set_logo(const Ref<Texture> &p_logo);

    void set_release_64(StringView p_file);
    void set_release_32(StringView p_file);
    void set_debug_64(StringView p_file);
    void set_debug_32(StringView p_file);

    void add_platform_feature(StringView p_feature);
    void get_platform_features(Vector<String> *r_features) override;
    void resolve_platform_feature_priorities(const Ref<EditorExportPreset> &p_preset, Set<String> &p_features) override;

    int get_chmod_flags() const;
    void set_chmod_flags(int p_flags);

    FixUpEmbeddedPckFunc get_fixup_embedded_pck_func() const;
    void set_fixup_embedded_pck_func(FixUpEmbeddedPckFunc p_fixup_embedded_pck_func);

    EditorExportPlatformPC();
};

class EditorExportTextSceneToBinaryPlugin : public EditorExportPlugin {

    GDCLASS(EditorExportTextSceneToBinaryPlugin,EditorExportPlugin)

public:
    void _export_file(StringView p_path, StringView p_type, const Set<String> &p_features) override;
    EditorExportTextSceneToBinaryPlugin();
};
