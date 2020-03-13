/*************************************************************************/
/*  editor_run_script.cpp                                                */
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

#include "editor_run_script.h"
#include "core/string_formatter.h"
#include "core/method_bind.h"
#include "editor_node.h"

IMPL_GDCLASS(EditorScript)

void EditorScript::add_root_node(Node *p_node) {

    if (!editor) {
        EditorNode::add_io_error("EditorScript::add_root_node: " + TTR("Write your logic in the _run() method."));
        return;
    }

    if (editor->get_edited_scene()) {
        EditorNode::add_io_error("EditorScript::add_root_node: " + TTR("There is an edited scene already."));
        return;
    }

    //editor->set_edited_scene(p_node);
}

EditorInterface *EditorScript::get_editor_interface() {

    return EditorInterface::get_singleton();
}

Node *EditorScript::get_scene() {

    if (!editor) {
        EditorNode::add_io_error("EditorScript::get_scene: " + TTR("Write your logic in the _run() method."));
        return nullptr;
    }

    return editor->get_edited_scene();
}

void EditorScript::_run() {

    Ref<Script> s(refFromRefPtr<Script>(get_script()));

    ERR_FAIL_COND(not s);
    if (!get_script_instance()) {
        EditorNode::add_io_error(FormatSN(
                TTR("Couldn't instance script:\n %s\nDid you forget the 'tool' keyword?").asCString(), s->get_path().to_string()));
        return;
    }

    Variant::CallError ce;
    ce.error = Variant::CallError::CALL_OK;
    get_script_instance()->call("_run", nullptr, 0, ce);
    if (ce.error != Variant::CallError::CALL_OK) {
        EditorNode::add_io_error(FormatSN(
                TTR("Couldn't run script:\n %s\nDid you forget the '_run' method?").asCString(), s->get_path().to_string()));
    }
}

void EditorScript::set_editor(EditorNode *p_editor) {

    editor = p_editor;
}

void EditorScript::_bind_methods() {

    MethodBinder::bind_method(D_METHOD("add_root_node", {"node"}), &EditorScript::add_root_node);
    MethodBinder::bind_method(D_METHOD("get_scene"), &EditorScript::get_scene);
    MethodBinder::bind_method(D_METHOD("get_editor_interface"), &EditorScript::get_editor_interface);
    BIND_VMETHOD(MethodInfo("_run"));
}

EditorScript::EditorScript() {

    editor = nullptr;
}
