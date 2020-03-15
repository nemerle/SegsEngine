/*************************************************************************/
/*  gdscript.h                                                           */
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

#include "core/io/resource_format_loader.h"
#include "core/io/resource_saver.h"
#include "core/script_language.h"
#include "gdscript_function.h"
#include "core/hash_set.h"
#include "core/hash_map.h"

namespace std {
class recursive_mutex;
}
using Mutex = std::recursive_mutex;

class GDScriptNativeClass : public RefCounted {

    GDCLASS(GDScriptNativeClass,RefCounted)

    StringName name;

protected:
    bool _get(const StringName &p_name, Variant &r_ret) const;
    static void _bind_methods();

public:
    _FORCE_INLINE_ const StringName &get_name() const { return name; }
    Variant _new();
    Object *instance();
    GDScriptNativeClass(const StringName &p_name);
};

class GDScript : public Script {

    GDCLASS(GDScript,Script)

    bool tool;
    bool valid;

    struct MemberInfo {
        int index;
        StringName setter;
        StringName getter;
        MultiplayerAPI_RPCMode rpc_mode;
        GDScriptDataType data_type;
    };

    friend class GDScriptInstance;
    friend class GDScriptFunction;
    friend class GDScriptCompiler;
    friend class GDScriptFunctions;
    friend class GDScriptLanguage;

    Variant _static_ref; //used for static call
    Ref<GDScriptNativeClass> native;
    Ref<GDScript> base;
    GDScript *_base; //fast pointer access
    GDScript *_owner; //for subclasses

    HashSet<StringName> members; //members are just indices to the instanced script.
    HashMap<StringName, Variant> constants;
    Map<StringName, GDScriptFunction *> member_functions;
    Map<StringName, MemberInfo> member_indices; //members are just indices to the instanced script.
    Map<StringName, Ref<GDScript> > subclasses;
    Map<StringName, Vector<StringName> > _signals;

#ifdef TOOLS_ENABLED

    Map<StringName, int> member_lines;

    HashMap<StringName, Variant> member_default_values;

    Vector<PropertyInfo> members_cache;
    HashMap<StringName, Variant> member_default_values_cache;
    Ref<GDScript> base_cache;
    HashSet<ObjectID> inheriters_cache;
    bool source_changed_cache;
    bool placeholder_fallback_enabled;
    void _update_exports_values(HashMap<StringName, Variant> &values, Vector<PropertyInfo> &propnames);

#endif
    Map<StringName, PropertyInfo> member_info;

    GDScriptFunction *initializer; //direct pointer to _init , faster to locate

    int subclass_count;
    HashSet<Object *> instances;
    //exported members
    String source;
    String path;
    StringName name;
    String fully_qualified_name;
    SelfList<GDScript> script_list;

    GDScriptInstance *_create_instance(const Variant **p_args, int p_argcount, Object *p_owner, bool p_isref, Variant::CallError &r_error);

    void _set_subclass_path(Ref<GDScript> &p_sc, StringView p_path);

#ifdef TOOLS_ENABLED
    HashSet<PlaceHolderScriptInstance *> placeholders;
    //void _update_placeholder(PlaceHolderScriptInstance *p_placeholder);
    void _placeholder_erased(PlaceHolderScriptInstance *p_placeholder) override;
#endif

#ifdef DEBUG_ENABLED

    Map<ObjectID, Vector<Pair<StringName, Variant> > > pending_reload_state;

#endif

    bool _update_exports();

    void _save_orphaned_subclasses();

protected:
    bool _get(const StringName &p_name, Variant &r_ret) const;
    bool _set(const StringName &p_name, const Variant &p_value);
    void _get_property_list(Vector<PropertyInfo> *p_properties) const;

    Variant call(const StringName &p_method, const Variant **p_args, int p_argcount, Variant::CallError &r_error) override;
    //void call_multilevel(const StringName& p_method,const Variant** p_args,int p_argcount);

    static void _bind_methods();

public:
    bool is_valid() const override { return valid; }

    const Map<StringName, Ref<GDScript> > &get_subclasses() const { return subclasses; }
    const HashMap<StringName, Variant> &get_constants() const { return constants; }
    const HashSet<StringName> &get_members() const { return members; }
    const GDScriptDataType &get_member_type(const StringName &p_member) const {
        CRASH_COND(!member_indices.contains(p_member));
        return member_indices.at(p_member).data_type;
    }
    const Map<StringName, GDScriptFunction *> &get_member_functions() const { return member_functions; }
    const Ref<GDScriptNativeClass> &get_native() const { return native; }
    const StringName &get_script_class_name() const { return name; }

    bool has_script_signal(const StringName &p_signal) const override;
    void get_script_signal_list(Vector<MethodInfo> *r_signals) const override;

    bool is_tool() const override { return tool; }
    Ref<GDScript> get_base() const;

    const Map<StringName, MemberInfo> &debug_get_member_indices() const { return member_indices; }
    const Map<StringName, GDScriptFunction *> &debug_get_member_functions() const; //this is debug only
    StringName debug_get_member_by_index(int p_idx) const;

    Variant _new(const Variant **p_args, int p_argcount, Variant::CallError &r_error);
    bool can_instance() const override;

    Ref<Script> get_base_script() const override;

    StringName get_instance_base_type() const override; // this may not work in all scripts, will return empty if so
    ScriptInstance *instance_create(Object *p_this) override;
    PlaceHolderScriptInstance *placeholder_instance_create(Object *p_this) override;
    bool instance_has(const Object *p_this) const override;

    bool has_source_code() const override;
    StringView get_source_code() const override;
    void set_source_code(String p_code) override;
    void update_exports() override;

    Error reload(bool p_keep_state = false) override;

    void set_script_path(StringView p_path) { path = p_path; } //because subclasses need a path too...
    Error load_source_code(StringView p_path);
    Error load_byte_code(StringView p_path);

    Vector<uint8_t> get_as_byte_code() const;

    bool get_property_default_value(const StringName &p_property, Variant &r_value) const override;

    void get_script_method_list(Vector<MethodInfo> *p_list) const override;
    bool has_method(const StringName &p_method) const override;
    MethodInfo get_method_info(const StringName &p_method) const override;

    void get_script_property_list(Vector<PropertyInfo> *p_list) const override;

    ScriptLanguage *get_language() const override;

    int get_member_line(const StringName &p_member) const override {
#ifdef TOOLS_ENABLED
        if (member_lines.contains(p_member))
            return member_lines.at(p_member);
        else
#endif
            return -1;
    }

    void get_constants(HashMap<StringName, Variant> *p_constants) override;
    void get_members(HashSet<StringName> *p_members) override;

#ifdef TOOLS_ENABLED
    bool is_placeholder_fallback_enabled() const override { return placeholder_fallback_enabled; }
#endif

    GDScript();
    ~GDScript() override;
};

class GDScriptInstance : public ScriptInstance {
    friend class GDScript;
    friend class GDScriptFunction;
    friend class GDScriptFunctions;
    friend class GDScriptCompiler;

    Object *owner;
    Ref<GDScript> script;
#ifdef DEBUG_ENABLED
    Map<StringName, int> member_indices_cache; //used only for hot script reloading
#endif
    Vector<Variant> members;
    bool base_ref;

    void _ml_call_reversed(GDScript *sptr, const StringName &p_method, const Variant **p_args, int p_argcount);

public:
    Object *get_owner() override { return owner; }

    bool set(const StringName &p_name, const Variant &p_value) override;
    bool get(const StringName &p_name, Variant &r_ret) const override;
    void get_property_list(Vector<PropertyInfo> *p_properties) const override;
    VariantType get_property_type(const StringName &p_name, bool *r_is_valid = nullptr) const override;

    void get_method_list(Vector<MethodInfo> *p_list) const override;
    bool has_method(const StringName &p_method) const override;
    Variant call(const StringName &p_method, const Variant **p_args, int p_argcount, Variant::CallError &r_error) override;
    void call_multilevel(const StringName &p_method, const Variant **p_args, int p_argcount) override;
    void call_multilevel_reversed(const StringName &p_method, const Variant **p_args, int p_argcount) override;

    Variant debug_get_member_by_index(int p_idx) const { return members[p_idx]; }

    void notification(int p_notification) override;
    String to_string(bool *r_valid) override;

    Ref<Script> get_script() const override;

    ScriptLanguage *get_language() override;

    void set_path(StringView p_path);

    void reload_members();

    MultiplayerAPI_RPCMode get_rpc_mode(const StringName &p_method) const override;
    MultiplayerAPI_RPCMode get_rset_mode(const StringName &p_variable) const override;

    GDScriptInstance();
    ~GDScriptInstance() override;
};

#ifdef DEBUG_ENABLED
struct GDScriptWarning {
    enum Code {
        UNASSIGNED_VARIABLE, // Variable used but never assigned
        UNASSIGNED_VARIABLE_OP_ASSIGN, // Variable never assigned but used in an assignment operation (+=, *=, etc)
        UNUSED_VARIABLE, // Local variable is declared but never used
        SHADOWED_VARIABLE, // Variable name shadowed by other variable
        UNUSED_CLASS_VARIABLE, // Class variable is declared but never used in the file
        UNUSED_ARGUMENT, // Function argument is never used
        UNREACHABLE_CODE, // Code after a return statement
        STANDALONE_EXPRESSION, // Expression not assigned to a variable
        VOID_ASSIGNMENT, // Function returns void but it's assigned to a variable
        NARROWING_CONVERSION, // Float value into an integer slot, precision is lost
        FUNCTION_MAY_YIELD, // Typed assign of function call that yields (it may return a function state)
        VARIABLE_CONFLICTS_FUNCTION, // Variable has the same name of a function
        FUNCTION_CONFLICTS_VARIABLE, // Function has the same name of a variable
        FUNCTION_CONFLICTS_CONSTANT, // Function has the same name of a constant
        INCOMPATIBLE_TERNARY, // Possible values of a ternary if are not mutually compatible
        UNUSED_SIGNAL, // Signal is defined but never emitted
        RETURN_VALUE_DISCARDED, // Function call returns something but the value isn't used
        PROPERTY_USED_AS_FUNCTION, // Function not found, but there's a property with the same name
        CONSTANT_USED_AS_FUNCTION, // Function not found, but there's a constant with the same name
        FUNCTION_USED_AS_PROPERTY, // Property not found, but there's a function with the same name
        INTEGER_DIVISION, // Integer divide by integer, decimal part is discarded
        UNSAFE_PROPERTY_ACCESS, // Property not found in the detected type (but can be in subtypes)
        UNSAFE_METHOD_ACCESS, // Function not found in the detected type (but can be in subtypes)
        UNSAFE_CAST, // Cast used in an unknown type
        UNSAFE_CALL_ARGUMENT, // Function call argument is of a supertype of the require argument
        DEPRECATED_KEYWORD, // The keyword is deprecated and should be replaced
        STANDALONE_TERNARY, // Return value of ternary expression is discarded
        WARNING_MAX,
    } code;
    Vector<String> symbols;
    int line;

    String get_name() const;
    String get_message() const;
    static const char *get_name_from_code(Code p_code);
    static Code get_code_from_name(const String &p_name);
    // Used to order the warnings by line
    bool operator<(const GDScriptWarning &oth) const {
        return line<oth.line;
    }
    GDScriptWarning() :
            code(WARNING_MAX),
            line(-1) {}
};
#endif // DEBUG_ENABLED

class GDScriptLanguage : public ScriptLanguage {

    static GDScriptLanguage *singleton;

    Variant *_global_array;
    Vector<Variant> global_array;
    Map<StringName, int> globals;
    HashMap<StringName, Variant> named_globals;

    struct CallLevel {

        Variant *stack;
        GDScriptFunction *function;
        GDScriptInstance *instance;
        int *ip;
        int *line;
    };

    int _debug_parse_err_line;
    String _debug_parse_err_file;
    String _debug_error;
    int _debug_call_stack_pos;
    int _debug_max_call_stack;
    CallLevel *_call_stack;

    void _add_global(const StringName &p_name, const Variant &p_value);

    friend class GDScriptInstance;

    Mutex *lock;

    friend class GDScript;

    SelfList<GDScript>::List script_list;
    friend class GDScriptFunction;

    SelfList<GDScriptFunction>::List function_list;
    bool profiling;
    uint64_t script_frame_time;

    Map<String, ObjectID> orphan_subclasses;
public:
    int calls;

    bool debug_break(StringView p_error, bool p_allow_continue = true);
    bool debug_break_parse(StringView p_file, int p_line, StringView p_error);

    _FORCE_INLINE_ void enter_function(GDScriptInstance *p_instance, GDScriptFunction *p_function, Variant *p_stack, int *p_ip, int *p_line) {

        if (Thread::get_main_id() != Thread::get_caller_id())
            return; //no support for other threads than main for now

        if (ScriptDebugger::get_singleton()->get_lines_left() > 0 && ScriptDebugger::get_singleton()->get_depth() >= 0)
            ScriptDebugger::get_singleton()->set_depth(ScriptDebugger::get_singleton()->get_depth() + 1);

        if (_debug_call_stack_pos >= _debug_max_call_stack) {
            //stack overflow
            _debug_error = "Stack Overflow (Stack Size: " + ::to_string(_debug_max_call_stack) + ")";
            ScriptDebugger::get_singleton()->debug(this);
            return;
        }

        _call_stack[_debug_call_stack_pos].stack = p_stack;
        _call_stack[_debug_call_stack_pos].instance = p_instance;
        _call_stack[_debug_call_stack_pos].function = p_function;
        _call_stack[_debug_call_stack_pos].ip = p_ip;
        _call_stack[_debug_call_stack_pos].line = p_line;
        _debug_call_stack_pos++;
    }

    _FORCE_INLINE_ void exit_function() {

        if (Thread::get_main_id() != Thread::get_caller_id())
            return; //no support for other threads than main for now

        if (ScriptDebugger::get_singleton()->get_lines_left() > 0 && ScriptDebugger::get_singleton()->get_depth() >= 0)
            ScriptDebugger::get_singleton()->set_depth(ScriptDebugger::get_singleton()->get_depth() - 1);

        if (_debug_call_stack_pos == 0) {

            _debug_error = "Stack Underflow (Engine Bug)";
            ScriptDebugger::get_singleton()->debug(this);
            return;
        }

        _debug_call_stack_pos--;
    }

    Vector<StackInfo> debug_get_current_stack_info() override {
        if (Thread::get_main_id() != Thread::get_caller_id())
            return Vector<StackInfo>();

        Vector<StackInfo> csi;
        csi.resize(_debug_call_stack_pos);
        for (int i = 0; i < _debug_call_stack_pos; i++) {
            csi[_debug_call_stack_pos - i - 1].line = _call_stack[i].line ? *_call_stack[i].line : 0;
            if (_call_stack[i].function) {
                csi[_debug_call_stack_pos - i - 1].func = _call_stack[i].function->get_name().asCString();
                csi[_debug_call_stack_pos - i - 1].file = _call_stack[i].function->get_script()->get_path().to_string();
            }
        }
        return csi;
    }

    struct {

        StringName _init;
        StringName _notification;
        StringName _set;
        StringName _get;
        StringName _get_property_list;
        StringName _script_source;

    } strings;

    _FORCE_INLINE_ int get_global_array_size() const { return global_array.size(); }
    _FORCE_INLINE_ Variant *get_global_array() { return _global_array; }
    _FORCE_INLINE_ const Map<StringName, int> &get_global_map() const { return globals; }
    _FORCE_INLINE_ const HashMap<StringName, Variant> &get_named_globals_map() const { return named_globals; }

    _FORCE_INLINE_ static GDScriptLanguage *get_singleton() { return singleton; }

    StringName get_name() const override;

    /* LANGUAGE FUNCTIONS */
    void init() override;
    String get_type() const override;
    String get_extension() const override;
    Error execute_file(StringView p_path) override;
    void finish() override;

    /* EDITOR FUNCTIONS */
    void get_reserved_words(Vector<String> *p_words) const override;
    void get_comment_delimiters(Vector<String> *p_delimiters) const override;
    void get_string_delimiters(Vector<String> *p_delimiters) const override;
    virtual String _get_processed_template(StringView p_template, StringView p_base_class_name) const;
    Ref<Script> get_template(StringView p_class_name, StringView p_base_class_name) const override;
    bool is_using_templates() override;
    void make_template(StringView p_class_name, StringView p_base_class_name, const Ref<Script> &p_script) override;
    bool validate(StringView p_script, int &r_line_error, int &r_col_error, String &r_test_error, StringView p_path = {}, Vector
            <String> *r_functions = nullptr, Vector<ScriptLanguage::Warning> *r_warnings = nullptr, Set<int> *r_safe_lines = nullptr) const override;
    Script *create_script() const override;
    bool has_named_classes() const override;
    bool supports_builtin_mode() const override;
    bool can_inherit_from_file() override { return true; }
    int find_function(StringView p_function, StringView p_code) const override;
    String make_function(const String &p_class, const StringName &p_name, const PoolVector<String> &p_args) const override;
    Error complete_code(const String &p_code, StringView p_path, Object *p_owner, Vector<ScriptCodeCompletionOption> *r_options, bool &r_forced, String &r_call_hint) override;
#ifdef TOOLS_ENABLED
    Error lookup_code(StringView p_code, StringView p_symbol, StringView p_path, Object *p_owner, LookupResult &r_result) override;
#endif
    virtual const char *_get_indentation() const;
    void auto_indent_code(String &p_code, int p_from_line, int p_to_line) const override;
    void add_global_constant(const StringName &p_variable, const Variant &p_value) override;
    void add_named_global_constant(const StringName &p_name, const Variant &p_value) override;
    void remove_named_global_constant(const StringName &p_name) override;

    /* DEBUGGER FUNCTIONS */

    const String &debug_get_error() const override;
    int debug_get_stack_level_count() const override;
    int debug_get_stack_level_line(int p_level) const override;
    String debug_get_stack_level_function(int p_level) const override;
    String debug_get_stack_level_source(int p_level) const override;
    void debug_get_stack_level_locals(int p_level, Vector<String> *p_locals, Vector<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override;
    void debug_get_stack_level_members(int p_level, Vector<String> *p_members, Vector<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override;
    ScriptInstance *debug_get_stack_level_instance(int p_level) override;
    void debug_get_globals(Vector<String> *p_globals, Vector<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override;
    String debug_parse_stack_level_expression(int p_level, StringView p_expression, int p_max_subitems = -1, int p_max_depth = -1) override;

    void reload_all_scripts() override;
    void reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) override;

    void frame() override;

    void get_public_functions(Vector<MethodInfo> *p_functions) const override;
    void get_public_constants(Vector<Pair<StringView, Variant>> *p_constants) const override;

    void profiling_start() override;
    void profiling_stop() override;

    int profiling_get_accumulated_data(ProfilingInfo *p_info_arr, int p_info_max) override;
    int profiling_get_frame_data(ProfilingInfo *p_info_arr, int p_info_max) override;

    /* LOADER FUNCTIONS */

    void get_recognized_extensions(Vector<String> *p_extensions) const override;

    /* GLOBAL CLASSES */

    bool handles_global_class_type(StringView p_type) const override;
    StringName get_global_class_name(const ResourcePath & p_path, String *r_base_type = nullptr, String *r_icon_path = nullptr) const override;

    void add_orphan_subclass(const String &p_qualified_name, const ObjectID &p_subclass);
    Ref<GDScript> get_orphan_subclass(const String &p_qualified_name);

    GDScriptLanguage();
    ~GDScriptLanguage() override;
};

class ResourceFormatLoaderGDScript : public ResourceFormatLoader {
public:
    RES load(const ResourcePath & p_path, StringView p_original_path = StringView(), Error *r_error = nullptr) override;
    void get_recognized_extensions(Vector<String> &p_extensions) const override;
    bool handles_type(StringView p_type) const override;
    String get_resource_type(const ResourcePath &p_path) const override;
    void get_dependencies(const ResourcePath & p_path, Vector<String> &p_dependencies, bool p_add_types = false) override;
};

class ResourceFormatSaverGDScript : public ResourceFormatSaver {
public:
    Error save(StringView p_path, const RES &p_resource, uint32_t p_flags = 0) override;
    void get_recognized_extensions(const RES &p_resource, Vector<String> &p_extensions) const override;
    bool recognize(const RES &p_resource) const override;
};

