/*************************************************************************/
/*  visual_script_func_nodes.h                                           */
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

#include "visual_script.h"
#include "core/method_info.h"

class VisualScriptFunctionCall : public VisualScriptNode {

    GDCLASS(VisualScriptFunctionCall,VisualScriptNode)

public:
    enum CallMode {
        CALL_MODE_SELF,
        CALL_MODE_NODE_PATH,
        CALL_MODE_INSTANCE,
        CALL_MODE_BASIC_TYPE,
        CALL_MODE_SINGLETON,
    };

    enum RPCCallMode {
        RPC_DISABLED,
        RPC_RELIABLE,
        RPC_UNRELIABLE,
        RPC_RELIABLE_TO_ID,
        RPC_UNRELIABLE_TO_ID
    };

private:
    CallMode call_mode;
    StringName base_type;
    ResourcePath base_script;
    VariantType basic_type;
    NodePath base_path;
    StringName function;
    int use_default_args;
    RPCCallMode rpc_call_mode;
    StringName singleton;
    bool validate;
public:
    Node *_get_base_node() const;
    StringName _get_base_type() const;

    MethodInfo method_cache;
    void _update_method_cache();

    void _set_argument_cache(const Dictionary &p_cache);
    Dictionary _get_argument_cache() const;

protected:
    void _validate_property(PropertyInfo &property) const override;

    static void _bind_methods();

public:
    int get_output_sequence_port_count() const override;
    bool has_input_sequence_port() const override;

    StringView get_output_sequence_port_text(int p_port) const override;

    int get_input_value_port_count() const override;
    int get_output_value_port_count() const override;

    PropertyInfo get_input_value_port_info(int p_idx) const override;
    PropertyInfo get_output_value_port_info(int p_idx) const override;

    StringView get_caption() const override;
    String get_text() const override;
    const char *get_category() const override { return "functions"; }

    void set_basic_type(VariantType p_type);
    VariantType get_basic_type() const;

    void set_base_type(const StringName &p_type);
    StringName get_base_type() const;

    void set_base_script(const ResourcePath &p_path);
    const ResourcePath &get_base_script() const;

    void set_singleton(const StringName &p_type);
    StringName get_singleton() const;

    void set_function(const StringName &p_type);
    StringName get_function() const;

    void set_base_path(const NodePath &p_type);
    NodePath get_base_path() const;

    void set_call_mode(CallMode p_mode);
    CallMode get_call_mode() const;

    void set_use_default_args(int p_amount);
    int get_use_default_args() const;

    void set_validate(bool p_amount);
    bool get_validate() const;

    void set_rpc_call_mode(RPCCallMode p_mode);
    RPCCallMode get_rpc_call_mode() const;

    VisualScriptNodeInstance *instance(VisualScriptInstance *p_instance) override;

    TypeGuess guess_output_type(TypeGuess *p_inputs, int p_output) const override;

    VisualScriptFunctionCall();
};

class VisualScriptPropertySet : public VisualScriptNode {

    GDCLASS(VisualScriptPropertySet,VisualScriptNode)

public:
    enum CallMode {
        CALL_MODE_SELF,
        CALL_MODE_NODE_PATH,
        CALL_MODE_INSTANCE,
        CALL_MODE_BASIC_TYPE,

    };

    enum AssignOp {
        ASSIGN_OP_NONE,
        ASSIGN_OP_ADD,
        ASSIGN_OP_SUB,
        ASSIGN_OP_MUL,
        ASSIGN_OP_DIV,
        ASSIGN_OP_MOD,
        ASSIGN_OP_SHIFT_LEFT,
        ASSIGN_OP_SHIFT_RIGHT,
        ASSIGN_OP_BIT_AND,
        ASSIGN_OP_BIT_OR,
        ASSIGN_OP_BIT_XOR,
        ASSIGN_OP_MAX
    };

private:
    PropertyInfo type_cache;

    CallMode call_mode;
    VariantType basic_type;
    StringName base_type;
    String base_script;
    NodePath base_path;
    StringName property;
    StringName index;
    AssignOp assign_op;

    Node *_get_base_node() const;
    StringName _get_base_type() const;

    void _update_base_type();

    void _update_cache();
public:
    void _set_type_cache(const Dictionary &p_type);
    Dictionary _get_type_cache() const;

    void _adjust_input_index(PropertyInfo &pinfo) const;

protected:
    void _validate_property(PropertyInfo &property) const override;

    static void _bind_methods();

public:
    int get_output_sequence_port_count() const override;
    bool has_input_sequence_port() const override;

    StringView get_output_sequence_port_text(int p_port) const override;

    int get_input_value_port_count() const override;
    int get_output_value_port_count() const override;

    PropertyInfo get_input_value_port_info(int p_idx) const override;
    PropertyInfo get_output_value_port_info(int p_idx) const override;

    StringView get_caption() const override;
    String get_text() const override;
    const char *get_category() const override { return "functions"; }

    void set_base_type(const StringName &p_type);
    StringName get_base_type() const;

    void set_base_script(StringView p_path);
    const String &get_base_script() const;

    void set_basic_type(VariantType p_type);
    VariantType get_basic_type() const;

    void set_property(const StringName &p_type);
    StringName get_property() const;

    void set_base_path(const NodePath &p_type);
    NodePath get_base_path() const;

    void set_call_mode(CallMode p_mode);
    CallMode get_call_mode() const;

    void set_index(const StringName &p_type);
    StringName get_index() const;

    void set_assign_op(AssignOp p_op);
    AssignOp get_assign_op() const;

    VisualScriptNodeInstance *instance(VisualScriptInstance *p_instance) override;
    TypeGuess guess_output_type(TypeGuess *p_inputs, int p_output) const override;

    VisualScriptPropertySet();
};


class VisualScriptPropertyGet : public VisualScriptNode {

    GDCLASS(VisualScriptPropertyGet,VisualScriptNode)

public:
    enum CallMode {
        CALL_MODE_SELF,
        CALL_MODE_NODE_PATH,
        CALL_MODE_INSTANCE,
        CALL_MODE_BASIC_TYPE,

    };

private:
    VariantType type_cache;

    CallMode call_mode;
    VariantType basic_type;
    StringName base_type;
    String base_script;
    NodePath base_path;
    StringName property;
    StringName index;

    void _update_base_type();
    Node *_get_base_node() const;
    StringName _get_base_type() const;

    void _update_cache();

public:
    void _set_type_cache(VariantType p_type);
    VariantType _get_type_cache() const;

protected:
    void _validate_property(PropertyInfo &property) const override;

    static void _bind_methods();

public:
    int get_output_sequence_port_count() const override;
    bool has_input_sequence_port() const override;

    StringView get_output_sequence_port_text(int p_port) const override;

    int get_input_value_port_count() const override;
    int get_output_value_port_count() const override;

    PropertyInfo get_input_value_port_info(int p_idx) const override;
    PropertyInfo get_output_value_port_info(int p_idx) const override;

    StringView get_caption() const override;
    String get_text() const override;
    const char *get_category() const override { return "functions"; }

    void set_base_type(const StringName &p_type);
    StringName get_base_type() const;

    void set_base_script(StringView p_path);
    const String &get_base_script() const { return base_script; }

    void set_basic_type(VariantType p_type);
    VariantType get_basic_type() const;

    void set_property(const StringName &p_type);
    StringName get_property() const;

    void set_base_path(const NodePath &p_type);
    NodePath get_base_path() const;

    void set_call_mode(CallMode p_mode);
    CallMode get_call_mode() const;

    void set_index(const StringName &p_type);
    StringName get_index() const;

    VisualScriptNodeInstance *instance(VisualScriptInstance *p_instance) override;

    VisualScriptPropertyGet();
};


class VisualScriptEmitSignal : public VisualScriptNode {

    GDCLASS(VisualScriptEmitSignal,VisualScriptNode)

private:
    StringName name;

protected:
    void _validate_property(PropertyInfo &property) const override;

    static void _bind_methods();

public:
    int get_output_sequence_port_count() const override;
    bool has_input_sequence_port() const override;

    StringView get_output_sequence_port_text(int p_port) const override;

    int get_input_value_port_count() const override;
    int get_output_value_port_count() const override;

    PropertyInfo get_input_value_port_info(int p_idx) const override;
    PropertyInfo get_output_value_port_info(int p_idx) const override;

    StringView get_caption() const override;
    //virtual String get_text() const;
    const char *get_category() const override { return "functions"; }

    void set_signal(const StringName &p_type);
    StringName get_signal() const;

    VisualScriptNodeInstance *instance(VisualScriptInstance *p_instance) override;

    VisualScriptEmitSignal();
};

void register_visual_script_func_nodes();
