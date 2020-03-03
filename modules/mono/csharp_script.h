/*************************************************************************/
/*  csharp_script.h                                                      */
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

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/script_language.h"
#include "core/self_list.h"
#include "core/list.h"

#include "mono_gc_handle.h"
#include "mono_gd/gd_mono.h"
#include "mono_gd/gd_mono_header.h"
#include "mono_gd/gd_mono_internals.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_plugin.h"
#endif

class CSharpScript;
class CSharpInstance;
class CSharpLanguage;
enum MultiplayerAPI_RPCMode : int8_t;
#ifdef NO_SAFE_CAST
template <typename TScriptInstance, typename TScriptLanguage>
TScriptInstance *cast_script_instance(ScriptInstance *p_inst) {
    if (!p_inst)
        return NULL;
    return p_inst->get_language() == TScriptLanguage::get_singleton() ? static_cast<TScriptInstance *>(p_inst) : NULL;
}
#else
template <typename TScriptInstance, typename TScriptLanguage>
TScriptInstance *cast_script_instance(ScriptInstance *p_inst) {
    return dynamic_cast<TScriptInstance *>(p_inst);
}
#endif

#define CAST_CSHARP_INSTANCE(m_inst) (cast_script_instance<CSharpInstance, CSharpLanguage>(m_inst))

class CSharpScript : public Script {

    GDCLASS(CSharpScript, Script);

    friend class CSharpInstance;
    friend class CSharpLanguage;
    friend struct CSharpScriptDepSort;

    bool tool;
    bool valid;

    bool builtin;

    GDMonoClass *base;
    GDMonoClass *native;
    GDMonoClass *script_class;

    Ref<CSharpScript> base_cache; // TODO what's this for?

    HashSet<Object *> instances;

#ifdef GD_MONO_HOT_RELOAD
    struct StateBackup {
        // TODO
        // Replace with buffer containing the serialized state of managed scripts.
        // Keep variant state backup to use only with script instance placeholders.
        Vector<Pair<StringName, Variant> > properties;
    };

    HashSet<ObjectID> pending_reload_instances;
    Map<ObjectID, StateBackup> pending_reload_state;
    StringName tied_class_name_for_reload;
    StringName tied_class_namespace_for_reload;
#endif

    String source;
    StringName name;

    SelfList<CSharpScript> script_list;

    struct Argument {
        StringName name;
        VariantType type;
    };

    Map<StringName, Vector<Argument> > _signals;
    bool signals_invalidated;

#ifdef TOOLS_ENABLED
    List<PropertyInfo> exported_members_cache; // members_cache
    HashMap<StringName, Variant> exported_members_defval_cache; // member_default_values_cache
    HashSet<PlaceHolderScriptInstance *> placeholders;
    bool source_changed_cache;
    bool placeholder_fallback_enabled;
    bool exports_invalidated;
    void _update_exports_values(HashMap<StringName, Variant> &values, Vector<PropertyInfo> &propnames);
    void _update_member_info_no_exports();
    void _placeholder_erased(PlaceHolderScriptInstance *p_placeholder) override;
#endif

    HashMap<StringName, PropertyInfo> member_info;

    void _clear();

    void load_script_signals(GDMonoClass *p_class, GDMonoClass *p_native_class);
    bool _get_signal(GDMonoClass *p_class, GDMonoClass *p_delegate, Vector<Argument> &params);

    bool _update_exports();
#ifdef TOOLS_ENABLED
    bool _get_member_export(IMonoClassMember *p_member, bool p_inspect_export, PropertyInfo &r_prop_info, bool &r_exported);
    static int _try_get_member_export_hint(IMonoClassMember *p_member, ManagedType p_type, VariantType p_variant_type, bool p_allow_generics, PropertyHint &r_hint, String &r_hint_string);
#endif

    CSharpInstance *_create_instance(const Variant **p_args, int p_argcount, Object *p_owner, bool p_isref, Variant::CallError &r_error);
public:
    Variant _new(const Variant **p_args, int p_argcount, Variant::CallError &r_error);
private:
    // Do not use unless you know what you are doing
    friend void GDMonoInternals::tie_managed_to_unmanaged(MonoObject *, Object *);
    static Ref<CSharpScript> create_for_managed_type(GDMonoClass *p_class, GDMonoClass *p_native);
    static void initialize_for_managed_type(Ref<CSharpScript> p_script, GDMonoClass *p_class, GDMonoClass *p_native);

protected:
    static void _bind_methods();

    Variant call(const StringName &p_method, const Variant **p_args, int p_argcount, Variant::CallError &r_error) override;
    void _resource_path_changed() override;
    bool _get(const StringName &p_name, Variant &r_ret) const;
    bool _set(const StringName &p_name, const Variant &p_value);
    void _get_property_list(Vector<PropertyInfo> *p_properties) const;

public:
    bool can_instance() const override;
    StringName get_instance_base_type() const override;
    ScriptInstance *instance_create(Object *p_this) override;
    PlaceHolderScriptInstance *placeholder_instance_create(Object *p_this) override;
    bool instance_has(const Object *p_this) const override;

    virtual bool has_source_code() const override;
    StringView get_source_code() const override;
    void set_source_code(String p_code) override;

    Error reload(bool p_keep_state = false) override;

    bool has_script_signal(const StringName &p_signal) const override;
    void get_script_signal_list(Vector<MethodInfo> *r_signals) const override;

    bool get_property_default_value(const StringName &p_property, Variant &r_value) const override;
    void get_script_property_list(Vector<PropertyInfo> *p_list) const override;
    void update_exports() override;

    bool is_tool() const override { return tool; }
    bool is_valid() const override { return valid; }

    Ref<Script> get_base_script() const override;
    ScriptLanguage *get_language() const override;

    void get_script_method_list(Vector<MethodInfo> *p_list) const override;
    bool has_method(const StringName &p_method) const override;
    MethodInfo get_method_info(const StringName &p_method) const override;

    int get_member_line(const StringName &p_member) const override;

#ifdef TOOLS_ENABLED
    bool is_placeholder_fallback_enabled() const override { return placeholder_fallback_enabled; }
#endif

    Error load_source_code(StringView p_path);

    StringName get_script_name() const;

    CSharpScript();
    ~CSharpScript() override;
};

class CSharpInstance : public ScriptInstance {

    friend class CSharpScript;
    friend class CSharpLanguage;

    Object *owner;
    bool base_ref;
    bool ref_dying;
    bool unsafe_referenced;
    bool predelete_notified;
    bool destructing_script_instance;

    Ref<CSharpScript> script;
    Ref<MonoGCHandle> gchandle;

    bool _reference_owner_unsafe();

    /*
     * If true is returned, the caller must memdelete the script instance's owner.
     */
    bool _unreference_owner_unsafe();

    /*
     * If NULL is returned, the caller must destroy the script instance by removing it from its owner.
     */
    MonoObject *_internal_new_managed();

    // Do not use unless you know what you are doing
    friend void GDMonoInternals::tie_managed_to_unmanaged(MonoObject *, Object *);
    static CSharpInstance *create_for_managed_type(Object *p_owner, CSharpScript *p_script, const Ref<MonoGCHandle> &p_gchandle);

    void _call_multilevel(MonoObject *p_mono_object, const StringName &p_method, const Variant **p_args, int p_argcount);

    MultiplayerAPI_RPCMode _member_get_rpc_mode(IMonoClassMember *p_member) const;

    void get_properties_state_for_reloading(Vector<Pair<StringName, Variant>> &r_state);

public:
    MonoObject *get_mono_object() const;

    _FORCE_INLINE_ bool is_destructing_script_instance() { return destructing_script_instance; }

    Object *get_owner() override;

    bool set(const StringName &p_name, const Variant &p_value) override;
    bool get(const StringName &p_name, Variant &r_ret) const override;
    void get_property_list(Vector<PropertyInfo> *p_properties) const override;
    VariantType get_property_type(const StringName &p_name, bool *r_is_valid) const override;

    /* TODO */ void get_method_list(Vector<MethodInfo> * /*p_list*/) const override {}
    bool has_method(const StringName &p_method) const override;
    Variant call(const StringName &p_method, const Variant **p_args, int p_argcount, Variant::CallError &r_error) override;
    void call_multilevel(const StringName &p_method, const Variant **p_args, int p_argcount) override;
    void call_multilevel_reversed(const StringName &p_method, const Variant **p_args, int p_argcount) override;

    void mono_object_disposed(MonoObject *p_obj);

    /*
     * If 'r_delete_owner' is set to true, the caller must memdelete the script instance's owner. Otherwise, if
     * 'r_remove_script_instance' is set to true, the caller must destroy the script instance by removing it from its owner.
     */
    void mono_object_disposed_baseref(MonoObject *p_obj, bool p_is_finalizer, bool &r_delete_owner, bool &r_remove_script_instance);

    void refcount_incremented() override;
    bool refcount_decremented() override;

    MultiplayerAPI_RPCMode get_rpc_mode(const StringName &p_method) const override;
    MultiplayerAPI_RPCMode get_rset_mode(const StringName &p_variable) const override;

    void notification(int p_notification) override;
    void _call_notification(int p_notification);

    String to_string(bool *r_valid) override;

    Ref<Script> get_script() const override;

    ScriptLanguage *get_language() override;

    CSharpInstance();
    ~CSharpInstance() override;
};

struct CSharpScriptBinding {
    bool inited;
    StringName type_name;
    GDMonoClass *wrapper_class;
    Ref<MonoGCHandle> gchandle;
    Object *owner;
};

class CSharpLanguage : public ScriptLanguage {

    friend class CSharpScript;
    friend class CSharpInstance;

    static CSharpLanguage *singleton;

    bool finalizing;

    GDMono *gdmono;
    SelfList<CSharpScript>::List script_list;

    Mutex *script_instances_mutex;
    Mutex *script_gchandle_release_mutex;
    Mutex *language_bind_mutex;

    Map<Object *, CSharpScriptBinding> script_bindings;
#ifdef DEBUG_ENABLED
    // List of unsafe object references
    Map<ObjectID, int> unsafe_object_references;
    Mutex *unsafe_object_references_lock;
#endif
    struct StringNameCache {

        StringName _signal_callback;
        StringName _set;
        StringName _get;
        StringName _get_property_list;
        StringName _notification;
        StringName _script_source;
        StringName dotctor; // .ctor
        StringName on_before_serialize; // OnBeforeSerialize
        StringName on_after_deserialize; // OnAfterDeserialize

        StringNameCache();
    };

    int lang_idx;

    Dictionary scripts_metadata;
    bool scripts_metadata_invalidated;

    // For debug_break and debug_break_parse
    int _debug_parse_err_line;
    String _debug_parse_err_file;
    String _debug_error;

    void _load_scripts_metadata();

    friend class GDMono;
    void _on_scripts_domain_unloaded();

#ifdef TOOLS_ENABLED
    EditorPlugin *godotsharp_editor;

    static void _editor_init_callback();
#endif

public:
    StringNameCache string_names;

    Mutex *get_language_bind_mutex() { return language_bind_mutex; }

    _FORCE_INLINE_ int get_language_index() { return lang_idx; }
    void set_language_index(int p_idx);

    const StringNameCache &get_string_names() { return string_names; }

    static CSharpLanguage *get_singleton() { return singleton; }

#ifdef TOOLS_ENABLED
    EditorPlugin *get_godotsharp_editor() const { return godotsharp_editor; }
#endif

    static void release_script_gchandle(Ref<MonoGCHandle> &p_gchandle);
    static void release_script_gchandle(MonoObject *p_expected_obj, Ref<MonoGCHandle> &p_gchandle);

    bool debug_break(const String &p_error, bool p_allow_continue = true);
    bool debug_break_parse(StringView p_file, int p_line, const String &p_error);

#ifdef GD_MONO_HOT_RELOAD
    bool is_assembly_reloading_needed();
    void reload_assemblies(bool p_soft_reload);
#endif

    _FORCE_INLINE_ Dictionary get_scripts_metadata_or_nothing() {
        return scripts_metadata_invalidated ? Dictionary() : scripts_metadata;
    }

    _FORCE_INLINE_ const Dictionary &get_scripts_metadata() {
        if (scripts_metadata_invalidated)
            _load_scripts_metadata();
        return scripts_metadata;
    }

    StringName get_name() const override;

    /* LANGUAGE FUNCTIONS */
    String get_type() const override;
    String get_extension() const override;
    Error execute_file(StringView p_path) override;
    void init() override;
    void finish() override;

    /* EDITOR FUNCTIONS */
    void get_reserved_words(Vector<String> *p_words) const override;
    void get_comment_delimiters(Vector<String> *p_delimiters) const override;
    void get_string_delimiters(Vector<String> *p_delimiters) const override;
    Ref<Script> get_template(StringView p_class_name, StringView p_base_class_name) const override;
    bool is_using_templates() override;
    void make_template(StringView p_class_name, StringView p_base_class_name, const Ref<Script> &p_script) override;
    bool validate(StringView p_script, int &r_line_error, int &r_col_error, String &r_test_error,
            StringView p_path = {}, Vector<String> *r_functions = nullptr,
            Vector<ScriptLanguage::Warning> *r_warnings = nullptr, Set<int> *r_safe_lines = nullptr) const override;
    String validate_path(StringView p_path) const override;
    Script *create_script() const override;
    bool has_named_classes() const override;
    bool supports_builtin_mode() const override;
    /* TODO? */ int find_function(StringView p_function, StringView p_code) const override { return -1; }
    String make_function(const String &p_class, const StringName &p_name, const PoolVector<String> &p_args) const override;
    String _get_indentation() const;
    /* TODO? */ void auto_indent_code(String &p_code, int p_from_line, int p_to_line) const override {}
    /* TODO */ void add_global_constant(const StringName &p_variable, const Variant &p_value) override {}

    /* DEBUGGER FUNCTIONS */
    const String &debug_get_error() const override;
    int debug_get_stack_level_count() const override;
    int debug_get_stack_level_line(int p_level) const override;
    String debug_get_stack_level_function(int p_level) const override;
    String debug_get_stack_level_source(int p_level) const override;
    /* TODO */
    String debug_parse_stack_level_expression(int p_level, StringView p_expression, int p_max_subitems = -1, int p_max_depth = -1) override { return ""; }

    void debug_get_stack_level_locals(int p_level, Vector<String> *p_locals, Vector<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override {}
    void debug_get_stack_level_members(int p_level, Vector<String> *p_members, Vector<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override {}
    void debug_get_globals(Vector<String> *p_globals, Vector<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override {}

    Vector<StackInfo> debug_get_current_stack_info() override;

    /* PROFILING FUNCTIONS */
    /* TODO */ void profiling_start() override {}
    /* TODO */ void profiling_stop() override {}
    /* TODO */ int profiling_get_accumulated_data(ProfilingInfo *p_info_arr, int p_info_max) override { return 0; }
    /* TODO */ int profiling_get_frame_data(ProfilingInfo *p_info_arr, int p_info_max) override { return 0; }

    void frame() override;

    /* TODO? */ void get_public_functions(Vector<MethodInfo> *p_functions) const override {}
    /* TODO? */ void get_public_constants(Vector<Pair<StringView, Variant>> *p_constants) const override {}

    void reload_all_scripts() override;
    void reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) override;

    /* LOADER FUNCTIONS */
    void get_recognized_extensions(Vector<String> *p_extensions) const override;

#ifdef TOOLS_ENABLED
    Error open_in_external_editor(const Ref<Script> &p_script, int p_line, int p_col) override;
    bool overrides_external_editor() override;
#endif

    /* THREAD ATTACHING */
    void thread_enter() override;
    void thread_exit() override;

    // Don't use these. I'm watching you
    void *alloc_instance_binding_data(Object *p_object) override;
    void free_instance_binding_data(void *p_data) override;
    void refcount_incremented_instance_binding(Object *p_object) override;
    bool refcount_decremented_instance_binding(Object *p_object) override;

    Map<Object *, CSharpScriptBinding>::iterator insert_script_binding(Object *p_object, const CSharpScriptBinding &p_script_binding);
    bool setup_csharp_script_binding(CSharpScriptBinding &r_script_binding, Object *p_object);

#ifdef DEBUG_ENABLED
    Vector<StackInfo> stack_trace_get_info(MonoObject *p_stack_trace);
#endif

    void post_unsafe_reference(Object *p_obj);
    void pre_unsafe_unreference(Object *p_obj);

    CSharpLanguage();
    ~CSharpLanguage();
};

class ResourceFormatLoaderCSharpScript : public ResourceFormatLoader {
public:
    RES load(StringView p_path, StringView p_original_path = "", Error *r_error = nullptr) override;
    void get_recognized_extensions(Vector<String> &p_extensions) const override;
    bool handles_type(StringView p_type) const override;
    String get_resource_type(StringView p_path) const override;
};

class ResourceFormatSaverCSharpScript : public ResourceFormatSaver {
public:
    Error save(StringView p_path, const RES &p_resource, uint32_t p_flags = 0) override;
    void get_recognized_extensions(const RES &p_resource, Vector<String> &p_extensions) const override;
    bool recognize(const RES &p_resource) const override;
};
