/*************************************************************************/
/*  visual_script.h                                                      */
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

#include "core/os/thread.h"
#include "core/hash_set.h"
#include "core/script_language.h"
#include "core/string_utils.h"
#include "core/math/vector2.h"
#include "core/list.h"

class VisualScriptInstance;
class VisualScriptNodeInstance;
class VisualScript;
namespace std {
class recursive_mutex;
}
using Mutex = std::recursive_mutex;

class VisualScriptNode : public Resource {
    GDCLASS(VisualScriptNode,Resource)

    friend class VisualScript;

    HashSet<VisualScript *> scripts_used;

    Array default_input_values;
    bool breakpoint;

public:
    void _set_default_input_values(Array p_values);
    Array _get_default_input_values() const;

    void validate_input_default_values();

    void ports_changed_notify();
protected:
    static void _bind_methods();

public:
    Ref<VisualScript> get_visual_script() const;

    virtual int get_output_sequence_port_count() const = 0;
    virtual bool has_input_sequence_port() const = 0;

    virtual StringView get_output_sequence_port_text(int p_port) const = 0;

    virtual bool has_mixed_input_and_sequence_ports() const { return false; }

    virtual int get_input_value_port_count() const = 0;
    virtual int get_output_value_port_count() const = 0;

    virtual PropertyInfo get_input_value_port_info(int p_idx) const = 0;
    virtual PropertyInfo get_output_value_port_info(int p_idx) const = 0;

    void set_default_input_value(int p_port, const Variant &p_value);
    Variant get_default_input_value(int p_port) const;

    virtual StringView get_caption() const = 0;
    virtual String get_text() const;
    virtual const char *get_category() const = 0;

    //used by editor, this is not really saved
    void set_breakpoint(bool p_breakpoint);
    bool is_breakpoint() const;

    virtual VisualScriptNodeInstance *instance(VisualScriptInstance *p_instance) = 0;

    struct TypeGuess {

        VariantType type;
        StringName gdclass;
        Ref<Script> script;

        TypeGuess() {
            type = VariantType::NIL;
        }
    };

    virtual TypeGuess guess_output_type(TypeGuess *p_inputs, int p_output) const;

    VisualScriptNode();
};

class VisualScriptNodeInstance {
    friend class VisualScriptInstance;
    friend class VisualScriptLanguage; //for debugger

    enum { //input argument addressing
        INPUT_SHIFT = 1 << 24,
        INPUT_MASK = INPUT_SHIFT - 1,
        INPUT_DEFAULT_VALUE_BIT = INPUT_SHIFT, // from unassigned input port, using default value (edited by user)
    };

    int id;
    int sequence_index;
    VisualScriptNodeInstance **sequence_outputs;
    int sequence_output_count;
    Vector<VisualScriptNodeInstance *> dependencies;
    int *input_ports;
    int input_port_count;
    int *output_ports;
    int output_port_count;
    int working_mem_idx;
    int pass_idx;

    VisualScriptNode *base;

public:
    enum StartMode {
        START_MODE_BEGIN_SEQUENCE,
        START_MODE_CONTINUE_SEQUENCE,
        START_MODE_RESUME_YIELD
    };

    enum {
        STEP_SHIFT = 1 << 24,
        STEP_MASK = STEP_SHIFT - 1,
        STEP_FLAG_PUSH_STACK_BIT = STEP_SHIFT, //push bit to stack
        STEP_FLAG_GO_BACK_BIT = STEP_SHIFT << 1, //go back to previous node
        STEP_NO_ADVANCE_BIT = STEP_SHIFT << 2, //do not advance past this node
        STEP_EXIT_FUNCTION_BIT = STEP_SHIFT << 3, //return from function
        STEP_YIELD_BIT = STEP_SHIFT << 4, //yield (will find VisualScriptFunctionState state in first working memory)

        FLOW_STACK_PUSHED_BIT = 1 << 30, //in flow stack, means bit was pushed (must go back here if end of sequence)
        FLOW_STACK_MASK = FLOW_STACK_PUSHED_BIT - 1

    };

    _FORCE_INLINE_ int get_input_port_count() const { return input_port_count; }
    _FORCE_INLINE_ int get_output_port_count() const { return output_port_count; }
    _FORCE_INLINE_ int get_sequence_output_count() const { return sequence_output_count; }

    _FORCE_INLINE_ int get_id() const { return id; }

    virtual int get_working_memory_size() const { return 0; }

    virtual int step(const Variant **p_inputs, Variant **p_outputs, StartMode p_start_mode, Variant *p_working_mem, Variant::CallError &r_error, String &r_error_str) = 0; //do a step, return which sequence port to go out

    Ref<VisualScriptNode> get_base_node() { return Ref<VisualScriptNode>(base); }

    VisualScriptNodeInstance();
    virtual ~VisualScriptNodeInstance();
};

class VisualScript : public Script {

    GDCLASS(VisualScript,Script)

    RES_BASE_EXTENSION("vs");

public:
    struct SequenceConnection {

        union {

            struct {
                uint64_t from_node : 24;
                uint64_t from_output : 16;
                uint64_t to_node : 24;
            };
            uint64_t id;
        };

        bool operator==(const SequenceConnection &p_connection) const {

            return id == p_connection.id;
        }
    private:
        friend eastl::hash<SequenceConnection>;
        explicit operator size_t() const {return id;}

    };

    struct DataConnection {

        union {

            struct {
                uint64_t from_node : 24;
                uint64_t from_port : 8;
                uint64_t to_node : 24;
                uint64_t to_port : 8;
            };
            uint64_t id;
        };

        bool operator==(const DataConnection &p_connection) const {

            return id == p_connection.id;
        }
    private:
        friend eastl::hash<DataConnection>;
        explicit operator size_t() const {return id;}
    };

private:
    friend class VisualScriptInstance;

    StringName base_type;
    struct Argument {
        StringName name;
        VariantType type;
    };

    struct Function {
        struct NodeData {
            Point2 pos;
            Ref<VisualScriptNode> node;
        };

        Map<int, NodeData> nodes;

        HashSet<SequenceConnection> sequence_connections;

        HashSet<DataConnection> data_connections;

        int function_id;

        Vector2 scroll;

        Function() { function_id = -1; }
    };

    struct Variable {
        PropertyInfo info;
        Variant default_value;
        bool _export;
        // add getter & setter options here
    };

    HashMap<StringName, Function> functions;
    HashMap<StringName, Variable> variables;
    HashMap<StringName, Vector<Argument> > custom_signals;

    HashMap<Object *, VisualScriptInstance *> instances;

    bool is_tool_script;

#ifdef TOOLS_ENABLED
    HashSet<PlaceHolderScriptInstance *> placeholders;
    //void _update_placeholder(PlaceHolderScriptInstance *p_placeholder);
    void _placeholder_erased(PlaceHolderScriptInstance *p_placeholder) override;
    void _update_placeholders();
#endif

public:
    void _set_variable_info(const StringName &p_name, const Dictionary &p_info);
    Dictionary _get_variable_info(const StringName &p_name) const;

    void _set_data(const Dictionary &p_data);
    Dictionary _get_data() const;

protected:
    void _node_ports_changed(int p_id);
    static void _bind_methods();

public:
    // TODO: Remove it in future when breaking changes are acceptable
    StringName get_default_func() const;
    void add_function(const StringName &p_name);
    bool has_function(const StringName &p_name) const;
    void remove_function(const StringName &p_name);
    void rename_function(const StringName &p_name, const StringName &p_new_name);
    void set_function_scroll(const StringName &p_name, const Vector2 &p_scroll);
    Vector2 get_function_scroll(const StringName &p_name) const;
    void get_function_list(Vector<StringName> *r_functions) const;
    int get_function_node_id(const StringName &p_name) const;
    void set_tool_enabled(bool p_enabled);

    void add_node(const StringName &p_func, int p_id, const Ref<VisualScriptNode> &p_node, const Point2 &p_pos = Point2());
    void remove_node(const StringName &p_func, int p_id);
    bool has_node(const StringName &p_func, int p_id) const;
    Ref<VisualScriptNode> get_node(const StringName &p_func, int p_id) const;
    void set_node_position(const StringName &p_func, int p_id, const Point2 &p_pos);
    Point2 get_node_position(const StringName &p_func, int p_id) const;
    void get_node_list(const StringName &p_func, Vector<int> *r_nodes) const;

    void sequence_connect(const StringName &p_func, int p_from_node, int p_from_output, int p_to_node);
    void sequence_disconnect(const StringName &p_func, int p_from_node, int p_from_output, int p_to_node);
    bool has_sequence_connection(const StringName &p_func, int p_from_node, int p_from_output, int p_to_node) const;
    void get_sequence_connection_list(const StringName &p_func, ListOld<SequenceConnection> *r_connection) const;

    void data_connect(const StringName &p_func, int p_from_node, int p_from_port, int p_to_node, int p_to_port);
    void data_disconnect(const StringName &p_func, int p_from_node, int p_from_port, int p_to_node, int p_to_port);
    bool has_data_connection(const StringName &p_func, int p_from_node, int p_from_port, int p_to_node, int p_to_port) const;
    void get_data_connection_list(const StringName &p_func, ListOld<DataConnection> *r_connection) const;
    bool is_input_value_port_connected(const StringName &p_func, int p_node, int p_port) const;
    bool get_input_value_port_connection_source(const StringName &p_func, int p_node, int p_port, int *r_node, int *r_port) const;

    void add_variable(const StringName &p_name, const Variant &p_default_value = Variant(), bool p_export = false);
    bool has_variable(const StringName &p_name) const;
    void remove_variable(const StringName &p_name);
    void set_variable_default_value(const StringName &p_name, const Variant &p_value);
    Variant get_variable_default_value(const StringName &p_name) const;
    void set_variable_info(const StringName &p_name, const PropertyInfo &p_info);
    PropertyInfo get_variable_info(const StringName &p_name) const;
    void set_variable_export(const StringName &p_name, bool p_export);
    bool get_variable_export(const StringName &p_name) const;
    void get_variable_list(Vector<StringName> *r_variables) const;
    void rename_variable(const StringName &p_name, const StringName &p_new_name);

    void add_custom_signal(const StringName &p_name);
    bool has_custom_signal(const StringName &p_name) const;
    void custom_signal_add_argument(const StringName &p_func, VariantType p_type, const StringName &p_name, int p_index = -1);
    void custom_signal_set_argument_type(const StringName &p_func, int p_argidx, VariantType p_type);
    VariantType custom_signal_get_argument_type(const StringName &p_func, int p_argidx) const;
    void custom_signal_set_argument_name(const StringName &p_func, int p_argidx, const StringName &p_name);
    StringView custom_signal_get_argument_name(const StringName &p_func, int p_argidx) const;
    void custom_signal_remove_argument(const StringName &p_func, int p_argidx);
    int custom_signal_get_argument_count(const StringName &p_func) const;
    void custom_signal_swap_argument(const StringName &p_func, int p_argidx, int p_with_argidx);
    void remove_custom_signal(const StringName &p_name);
    void rename_custom_signal(const StringName &p_name, const StringName &p_new_name);
    Set<int> get_output_sequence_ports_connected(StringView edited_func, int from_node);

    void get_custom_signal_list(Vector<StringName> *r_custom_signals) const;

    int get_available_id() const;

    void set_instance_base_type(const StringName &p_type);

    bool can_instance() const override;

    Ref<Script> get_base_script() const override;
    StringName get_instance_base_type() const override;
    ScriptInstance *instance_create(Object *p_this) override;
    bool instance_has(const Object *p_this) const override;

    bool has_source_code() const override;
    StringView get_source_code() const override;
    void set_source_code(String p_code) override;
    Error reload(bool p_keep_state = false) override;

    bool is_tool() const override;
    bool is_valid() const override;

    ScriptLanguage *get_language() const override;

    bool has_script_signal(const StringName &p_signal) const override;
    void get_script_signal_list(Vector<MethodInfo> *r_signals) const override;

    bool get_property_default_value(const StringName &p_property, Variant &r_value) const override;
    void get_script_method_list(Vector<MethodInfo> *p_list) const override;

    bool has_method(const StringName &p_method) const override;
    MethodInfo get_method_info(const StringName &p_method) const override;

    void get_script_property_list(Vector<PropertyInfo> *p_list) const override;

    int get_member_line(const StringName &p_member) const override;

#ifdef TOOLS_ENABLED
    virtual bool are_subnodes_edited() const;
#endif

    VisualScript();
    ~VisualScript() override;
};

class VisualScriptInstance : public ScriptInstance {
    Object *owner;
    Ref<VisualScript> script;

    HashMap<StringName, Variant> variables; //using variable path, not script
    Map<int, VisualScriptNodeInstance *> instances;

    struct Function {
        int node;
        int max_stack;
        int trash_pos;
        int flow_stack_size;
        int pass_stack_size;
        int node_count;
        int argument_count;
    };

    Map<StringName, Function> functions;

    Vector<Variant> default_values;
    int max_input_args, max_output_args;

    String source;

    void _dependency_step(VisualScriptNodeInstance *node, int p_pass, int *pass_stack, const Variant **input_args, Variant **output_args, Variant *variant_stack, Variant::CallError &r_error, String &error_str, VisualScriptNodeInstance **r_error_node);
    Variant _call_internal(const StringName &p_method, void *p_stack, int p_stack_size, VisualScriptNodeInstance *p_node, int p_flow_stack_pos, int p_pass, bool p_resuming_yield, Variant::CallError &r_error);

    //Map<StringName,Function> functions;
    friend class VisualScriptFunctionState; //for yield
    friend class VisualScriptLanguage; //for debugger
public:
    bool set(const StringName &p_name, const Variant &p_value) override;
    bool get(const StringName &p_name, Variant &r_ret) const override;
    void get_property_list(Vector<PropertyInfo> *p_properties) const override;
    VariantType get_property_type(const StringName &p_name, bool *r_is_valid = nullptr) const override;

    void get_method_list(Vector<MethodInfo> *p_list) const override;
    bool has_method(const StringName &p_method) const override;
    Variant call(const StringName &p_method, const Variant **p_args, int p_argcount, Variant::CallError &r_error) override;
    void notification(int p_notification) override;
    String to_string(bool *r_valid) override;

    bool set_variable(const StringName &p_variable, const Variant &p_value) {

        HashMap<StringName, Variant>::iterator E = variables.find(p_variable);
        if (E==variables.end())
            return false;

        E->second = p_value;
        return true;
    }

    bool get_variable(const StringName &p_variable, Variant *r_variable) const {

        const HashMap<StringName, Variant>::const_iterator E = variables.find(p_variable);
        if (E==variables.end())
            return false;

        *r_variable = E->second;
        return true;
    }

    Ref<Script> get_script() const override;

    _FORCE_INLINE_ VisualScript *get_script_ptr() { return script.get(); }
    _FORCE_INLINE_ Object *get_owner_ptr() { return owner; }

    void create(const Ref<VisualScript> &p_script, Object *p_owner);

    ScriptLanguage *get_language() override;

    MultiplayerAPI_RPCMode get_rpc_mode(const StringName &p_method) const override;
    MultiplayerAPI_RPCMode get_rset_mode(const StringName &p_variable) const override;

    VisualScriptInstance();
    ~VisualScriptInstance() override;
};

class VisualScriptFunctionState : public RefCounted {

    GDCLASS(VisualScriptFunctionState,RefCounted)

    friend class VisualScriptInstance;

    ObjectID instance_id;
    ObjectID script_id;
    VisualScriptInstance *instance;
    StringName function;
    Vector<uint8_t> stack;
    int working_mem_index;
    int variant_stack_size;
    VisualScriptNodeInstance *node;
    int flow_stack_pos;
    int pass;

    Variant _signal_callback(const Variant **p_args, int p_argcount, Variant::CallError &r_error);

protected:
    static void _bind_methods();

public:
    void connect_to_signal(Object *p_obj, StringView p_signal, Array p_binds);
    void connect_to_signal_sv(Object *p_obj, StringView p_signal, Array p_binds);
    bool is_valid() const;
    Variant resume(Array p_args);
    VisualScriptFunctionState();
    ~VisualScriptFunctionState() override;
};

using VisualScriptNodeRegisterFunc = Ref<VisualScriptNode> (*)(StringView);

class VisualScriptLanguage : public ScriptLanguage {

    Map<String, VisualScriptNodeRegisterFunc> register_funcs;

    struct CallLevel {

        Variant *stack;
        Variant **work_mem;
        const StringName *function;
        VisualScriptInstance *instance;
        int *current_id;
    };

    int _debug_parse_err_node;
    String _debug_parse_err_file;
    String _debug_error;
    int _debug_call_stack_pos;
    int _debug_max_call_stack;
    CallLevel *_call_stack;

public:
    StringName notification;
    StringName _get_output_port_unsequenced;
    StringName _step;
    StringName _subcall;

    static VisualScriptLanguage *singleton;

    Mutex *lock;

    bool debug_break(StringView p_error, bool p_allow_continue = true);
    bool debug_break_parse(StringView p_file, int p_node, StringView p_error);

    _FORCE_INLINE_ void enter_function(VisualScriptInstance *p_instance, const StringName *p_function, Variant *p_stack, Variant **p_work_mem, int *current_id) {

        if (Thread::get_main_id() != Thread::get_caller_id())
            return; //no support for other threads than main for now

        if (ScriptDebugger::get_singleton()->get_lines_left() > 0 && ScriptDebugger::get_singleton()->get_depth() >= 0)
            ScriptDebugger::get_singleton()->set_depth(ScriptDebugger::get_singleton()->get_depth() + 1);

        if (_debug_call_stack_pos >= _debug_max_call_stack) {
            //stack overflow
            _debug_error = "Stack Overflow (Stack Size: " + itos(_debug_max_call_stack) + ")";
            ScriptDebugger::get_singleton()->debug(this);
            return;
        }

        _call_stack[_debug_call_stack_pos].stack = p_stack;
        _call_stack[_debug_call_stack_pos].instance = p_instance;
        _call_stack[_debug_call_stack_pos].function = p_function;
        _call_stack[_debug_call_stack_pos].work_mem = p_work_mem;
        _call_stack[_debug_call_stack_pos].current_id = current_id;
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

    //////////////////////////////////////

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
    Ref<Script> get_template(StringView p_class_name, StringView p_base_class_name) const override;
    bool is_using_templates() override;
    void make_template(StringView p_class_name, StringView p_base_class_name, const Ref<Script> &p_script) override;
    bool validate(StringView p_script, int &r_line_error, int &r_col_error, String &r_test_error,
            StringView p_path = {}, Vector<String> *r_functions = nullptr,
            Vector<ScriptLanguage::Warning> *r_warnings = nullptr, Set<int> *r_safe_lines = nullptr) const override;
    Script *create_script() const override;
    bool has_named_classes() const override;
    bool supports_builtin_mode() const override;
    int find_function(StringView p_function, StringView p_code) const override;
    String make_function(const String &p_class, const StringName &p_name, const PoolVector<String> &p_args) const override;
    void auto_indent_code(String &p_code, int p_from_line, int p_to_line) const override;
    void add_global_constant(const StringName &p_variable, const Variant &p_value) override;

    /* DEBUGGER FUNCTIONS */

    const String &debug_get_error() const override;
    int debug_get_stack_level_count() const override;
    int debug_get_stack_level_line(int p_level) const override;
    String debug_get_stack_level_function(int p_level) const override;
    String debug_get_stack_level_source(int p_level) const override;
    void debug_get_stack_level_locals(int p_level, Vector<String> *p_locals, Vector<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override;
    void debug_get_stack_level_members(int p_level, Vector<String> *p_members, Vector<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override;
    void debug_get_globals(Vector<String> *p_locals, Vector<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override;
    String debug_parse_stack_level_expression(int p_level, StringView p_expression, int p_max_subitems = -1, int p_max_depth = -1) override;

    void reload_all_scripts() override;
    void reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) override;
    /* LOADER FUNCTIONS */

    void get_recognized_extensions(Vector<String> *p_extensions) const override;
    void get_public_functions(Vector<MethodInfo> *p_functions) const override;
    void get_public_constants(Vector<Pair<StringView, Variant>> *p_constants) const override;

    void profiling_start() override;
    void profiling_stop() override;

    int profiling_get_accumulated_data(ProfilingInfo *p_info_arr, int p_info_max) override;
    int profiling_get_frame_data(ProfilingInfo *p_info_arr, int p_info_max) override;

    void add_register_func(StringView p_name, VisualScriptNodeRegisterFunc p_func);
    void remove_register_func(StringView p_name);
    Ref<VisualScriptNode> create_node_from_name(const String &p_name);
    void get_registered_node_names(ListOld<String> *r_names);

    VisualScriptLanguage();
    ~VisualScriptLanguage() override;
};

//aid for registering
template <class T>
static Ref<VisualScriptNode> create_node_generic(StringView p_name) {

    Ref<T> node(make_ref_counted<T>());
    return node;
}
