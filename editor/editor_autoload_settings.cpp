/*************************************************************************/
/*  editor_autoload_settings.cpp                                         */
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

#include "editor_autoload_settings.h"
#include "project_settings_editor.h"

#include "core/method_bind.h"
#include "core/global_constants.h"
#include "core/project_settings.h"
#include "core/string_formatter.h"
#include "editor_node.h"
#include "editor_scale.h"
#include "scene/main/viewport.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/packed_scene.h"

#include "EASTL/sort.h"
#define PREVIEW_LIST_MAX_SIZE 10

IMPL_GDCLASS(EditorAutoloadSettings)

void EditorAutoloadSettings::_notification(int p_what) {

    if (p_what == NOTIFICATION_ENTER_TREE) {

        Vector<String> afn;
        ResourceLoader::get_recognized_extensions_for_type("Script", afn);
        ResourceLoader::get_recognized_extensions_for_type("PackedScene", afn);

        EditorFileDialog *file_dialog = autoload_add_path->get_file_dialog();

        for (const String &E : afn) {

            file_dialog->add_filter("*." + E);
        }

        for (AutoLoadInfo &info : autoload_cache) {
            if (info.node && info.in_editor) {
                get_tree()->get_root()->call_deferred("add_child", Variant(info.node));
            }
        }
    }
}

bool EditorAutoloadSettings::_autoload_name_is_valid(const StringName &p_name, String *r_error) {

    if (!StringUtils::is_valid_identifier(p_name)) {
        if (r_error)
            *r_error = TTR("Invalid name.") + "\n" + TTR("Valid characters:") + " a-z, A-Z, 0-9 or _";

        return false;
    }

    if (ClassDB::class_exists(p_name)) {
        if (r_error)
            *r_error = TTR("Invalid name.") + "\n" + TTR("Must not collide with an existing engine class name.");

        return false;
    }

    for (int i = 0; i < int(VariantType::VARIANT_MAX); i++) {
        if (Variant::get_type_name(VariantType(i)) == p_name) {
            if (r_error)
                *r_error = TTR("Invalid name.") + "\n" + TTR("Must not collide with an existing built-in type name.");

            return false;
        }
    }

    for (int i = 0; i < GlobalConstants::get_global_constant_count(); i++) {
        if (GlobalConstants::get_global_constant_name(i) == p_name) {
            if (r_error)
                *r_error = TTR("Invalid name.") + "\n" + TTR("Must not collide with an existing global constant name.");

            return false;
        }
    }

    for (int i = 0; i < ScriptServer::get_language_count(); i++) {
        Vector<String> keywords;
        ScriptServer::get_language(i)->get_reserved_words(&keywords);
        for (const String &E : keywords) {
            if (E == p_name) {
                if (r_error)
                    *r_error = TTR("Invalid name.") + "\n" + TTR("Keyword cannot be used as an autoload name.");

                return false;
            }
        }
    }

    return true;
}

void EditorAutoloadSettings::_autoload_add() {

    if (autoload_add(StringName(autoload_add_name->get_text()), autoload_add_path->get_line_edit()->get_text()))
        autoload_add_path->get_line_edit()->set_text("");

    autoload_add_name->set_text("");
    add_autoload->set_disabled(true);
}

void EditorAutoloadSettings::_autoload_selected() {

    TreeItem *ti = tree->get_selected();

    if (!ti)
        return;

    selected_autoload = "autoload/" + ti->get_text(0);
}

void EditorAutoloadSettings::_autoload_edited() {

    if (updating_autoload)
        return;

    TreeItem *ti = tree->get_edited();
    int column = tree->get_edited_column();

    UndoRedo *undo_redo = EditorNode::get_undo_redo();

    if (column == 0) {
        String name = ti->get_text(0);
        StringView old_name = StringUtils::get_slice(selected_autoload,"/", 1);

        if (name == old_name)
            return;

        String error;
        if (!_autoload_name_is_valid(StringName(name), &error)) {
            ti->set_text_utf8(0, old_name);
            EditorNode::get_singleton()->show_warning(StringName(error));
            return;
        }

        if (ProjectSettings::get_singleton()->has_setting(StringName("autoload/" + name))) {
            ti->set_text_utf8(0, old_name);
            EditorNode::get_singleton()->show_warning(FormatSN(TTR("Autoload '%s' already exists!").asCString(), name.c_str()));
            return;
        }

        updating_autoload = true;

        name = "autoload/" + name;

        int order = ProjectSettings::get_singleton()->get_order(StringName(selected_autoload));
        String path = ProjectSettings::get_singleton()->get(StringName(selected_autoload));

        undo_redo->create_action(TTR("Rename Autoload"));

        undo_redo->add_do_property(ProjectSettings::get_singleton(), name, path);
        undo_redo->add_do_method(ProjectSettings::get_singleton(), "set_order", name, order);
        undo_redo->add_do_method(ProjectSettings::get_singleton(), "clear", selected_autoload);

        undo_redo->add_undo_property(ProjectSettings::get_singleton(), selected_autoload, path);
        undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_order", selected_autoload, order);
        undo_redo->add_undo_method(ProjectSettings::get_singleton(), "clear", name);

        undo_redo->add_do_method(this, "call_deferred", "update_autoload");
        undo_redo->add_undo_method(this, "call_deferred", "update_autoload");

        undo_redo->add_do_method(this, "emit_signal", autoload_changed);
        undo_redo->add_undo_method(this, "emit_signal", autoload_changed);

        undo_redo->commit_action();

        selected_autoload = name;
    } else if (column == 2) {
        updating_autoload = true;

        bool checked = ti->is_checked(2);
        StringName base("autoload/" + ti->get_text(0));

        int order = ProjectSettings::get_singleton()->get_order(base);
        String path = ProjectSettings::get_singleton()->get(base);

        if (StringUtils::begins_with(path,"*"))
            path = StringUtils::substr(path,1, path.length());

        // Singleton autoloads are represented with a leading "*" in their path.
        if (checked)
            path = "*" + path;

        undo_redo->create_action(TTR("Toggle AutoLoad Globals"));

        undo_redo->add_do_property(ProjectSettings::get_singleton(), base, path);
        undo_redo->add_undo_property(ProjectSettings::get_singleton(), base, ProjectSettings::get_singleton()->get(base));

        undo_redo->add_do_method(ProjectSettings::get_singleton(), "set_order", base, order);
        undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_order", base, order);

        undo_redo->add_do_method(this, "call_deferred", "update_autoload");
        undo_redo->add_undo_method(this, "call_deferred", "update_autoload");

        undo_redo->add_do_method(this, "emit_signal", autoload_changed);
        undo_redo->add_undo_method(this, "emit_signal", autoload_changed);

        undo_redo->commit_action();
    }

    updating_autoload = false;
}

void EditorAutoloadSettings::_autoload_button_pressed(Object *p_item, int p_column, int p_button) {

    TreeItem *ti = object_cast<TreeItem>(p_item);

    StringName name("autoload/" + ti->get_text(0));

    UndoRedo *undo_redo = EditorNode::get_undo_redo();

    switch (p_button) {
        case BUTTON_OPEN: {
            _autoload_open(ti->get_text(1));
        } break;
        case BUTTON_MOVE_UP:
        case BUTTON_MOVE_DOWN: {

            TreeItem *swap = nullptr;

            if (p_button == BUTTON_MOVE_UP) {
                swap = ti->get_prev();
            } else {
                swap = ti->get_next();
            }

            if (!swap)
                return;

            StringName swap_name("autoload/" + swap->get_text(0));

            int order = ProjectSettings::get_singleton()->get_order(name);
            int swap_order = ProjectSettings::get_singleton()->get_order(swap_name);

            undo_redo->create_action(TTR("Move Autoload"));

            undo_redo->add_do_method(ProjectSettings::get_singleton(), "set_order", name, swap_order);
            undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_order", name, order);

            undo_redo->add_do_method(ProjectSettings::get_singleton(), "set_order", swap_name, order);
            undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_order", swap_name, swap_order);

            undo_redo->add_do_method(this, "update_autoload");
            undo_redo->add_undo_method(this, "update_autoload");

            undo_redo->add_do_method(this, "emit_signal", autoload_changed);
            undo_redo->add_undo_method(this, "emit_signal", autoload_changed);

            undo_redo->commit_action();
        } break;
        case BUTTON_DELETE: {

            int order = ProjectSettings::get_singleton()->get_order(name);

            undo_redo->create_action(TTR("Remove Autoload"));

            undo_redo->add_do_property(ProjectSettings::get_singleton(), name, Variant());

            undo_redo->add_undo_property(ProjectSettings::get_singleton(), name, ProjectSettings::get_singleton()->get(name));
            undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_persisting", name, true);
            undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_order", order);

            undo_redo->add_do_method(this, "update_autoload");
            undo_redo->add_undo_method(this, "update_autoload");

            undo_redo->add_do_method(this, "emit_signal", autoload_changed);
            undo_redo->add_undo_method(this, "emit_signal", autoload_changed);

            undo_redo->commit_action();
        } break;
    }
}

void EditorAutoloadSettings::_autoload_activated() {
    TreeItem *ti = tree->get_selected();
    if (!ti)
        return;
    _autoload_open(ti->get_text(1));
}
#if 0
void EditorAutoloadSettings::_autoload_open(StringView fpath) {
    if (ResourceLoader::get_resource_type(fpath) == "PackedScene") {
        EditorNode::get_singleton()->open_request(fpath);
    } else {
        EditorNode::get_singleton()->load_resource(fpath);
    }
    ProjectSettingsEditor::get_singleton()->hide();
}
#endif
void EditorAutoloadSettings::_autoload_file_callback(StringView p_path) {
    using namespace PathUtils;
    using namespace StringUtils;
    // Convert the file name to PascalCase, which is the convention for classes in GDScript.
    const String class_name = capitalize(get_basename(get_file(p_path))).replaced(" ", "");

    // If the name collides with a built-in class, prefix the name to make it possible to add without having to edit the name.
    // The prefix is subjective, but it provides better UX than leaving the Add button disabled :)
    const String prefix = ClassDB::class_exists(StringName(class_name)) ? "Global" : "";

    autoload_add_name->set_text(prefix + class_name);
    add_autoload->set_disabled(false);
}

void EditorAutoloadSettings::_autoload_text_entered(StringView p_name) {

    if (!autoload_add_path->get_line_edit()->get_text().empty() && _autoload_name_is_valid(StringName(p_name), NULL)) {
        _autoload_add();
    }
}

void EditorAutoloadSettings::_autoload_path_text_changed(StringView p_path) {

    add_autoload->set_disabled(
            p_path.empty() || !_autoload_name_is_valid(StringName(autoload_add_name->get_text()), NULL));
}

void EditorAutoloadSettings::_autoload_text_changed(StringView p_name) {

    add_autoload->set_disabled(
            autoload_add_path->get_line_edit()->get_text().empty() || !_autoload_name_is_valid(StringName(p_name), NULL));
}
#if 0
Node *EditorAutoloadSettings::_create_autoload(StringView p_path) {
    RES res(ResourceLoader::load(p_path));
    ERR_FAIL_COND_V_MSG(not res, nullptr, String("Can't autoload: ") + p_path + ".");
    Node *n = nullptr;
    if (res->is_class("PackedScene")) {
        Ref<PackedScene> ps = dynamic_ref_cast<PackedScene>(res);
        n = ps->instance();
    } else if (res->is_class("Script")) {
        Ref<Script> s = dynamic_ref_cast<Script>(res);
        StringName ibt = s->get_instance_base_type();
        bool valid_type = ClassDB::is_parent_class(ibt, "Node");
        ERR_FAIL_COND_V_MSG(!valid_type, nullptr, String("Script does not inherit a Node: ") + p_path + ".");

        Object *obj = ClassDB::instance(ibt);

        ERR_FAIL_COND_V_MSG(obj == nullptr, nullptr, "Cannot instance script for autoload, expected 'Node' inheritance, got: " + String(ibt) + ".");

        n = object_cast<Node>(obj);
        n->set_script(s.get_ref_ptr());
    }

    ERR_FAIL_COND_V_MSG(!n, nullptr, String("Path in autoload not a node or script: ") + p_path + ".");

    return n;
}
#endif
void EditorAutoloadSettings::update_autoload() {

    if (updating_autoload)
        return;

    updating_autoload = true;

    Map<StringView, AutoLoadInfo> to_remove;
    Vector<AutoLoadInfo *> to_add;

    for (AutoLoadInfo &info : autoload_cache) {
        to_remove.emplace(info.name, info);
    }

    autoload_cache.clear();

    tree->clear();
    TreeItem *root = tree->create_item();

    Vector<PropertyInfo> props;
    ProjectSettings::get_singleton()->get_property_list(&props);

    for (const PropertyInfo &pi : props) {

        if (!StringUtils::begins_with(pi.name,"autoload/"))
            continue;

        String name(StringUtils::get_slice(pi.name,"/", 1));
        String path = ProjectSettings::get_singleton()->get(pi.name);

        if (name.empty())
            continue;

        AutoLoadInfo info;
        info.is_singleton = StringUtils::begins_with(path,"*");

        if (info.is_singleton) {
            path = StringUtils::substr(path,1, path.length());
        }

        info.name = StringName(name);
        info.path = path;
        info.order = ProjectSettings::get_singleton()->get_order(pi.name);

        bool need_to_add = true;
        if (to_remove.contains(name)) {
            AutoLoadInfo &old_info = to_remove[name];
            if (old_info.path == info.path) {
                // Still the same resource, check status
                info.node = old_info.node;
                if (info.node) {
                    Ref<Script> scr = refFromRefPtr<Script>(info.node->get_script());
                    info.in_editor = scr && scr->is_tool();
                    if (info.is_singleton == old_info.is_singleton && info.in_editor == old_info.in_editor) {
                        to_remove.erase(name);
                        need_to_add = false;
                    } else {
                        info.node = nullptr;
                    }
                }
            }
        }

        autoload_cache.push_back(info);

        if (need_to_add) {
            to_add.push_back(&autoload_cache.back());
        }

        TreeItem *item = tree->create_item(root);
        item->set_text_utf8(0, name);
        item->set_editable(0, true);

        item->set_text_utf8(1, path);
        item->set_selectable(1, true);

        item->set_cell_mode(2, TreeItem::CELL_MODE_CHECK);
        item->set_editable(2, true);
        item->set_text(2, TTR("Enable"));
        item->set_checked(2, info.is_singleton);
        item->add_button(3, get_icon("Load", "EditorIcons"), BUTTON_OPEN);
        item->add_button(3, get_icon("MoveUp", "EditorIcons"), BUTTON_MOVE_UP);
        item->add_button(3, get_icon("MoveDown", "EditorIcons"), BUTTON_MOVE_DOWN);
        item->add_button(3, get_icon("Remove", "EditorIcons"), BUTTON_DELETE);
        item->set_selectable(3, false);
    }

    // Remove deleted/changed autoloads
    for (eastl::pair<const StringView,AutoLoadInfo> &E : to_remove) {
        AutoLoadInfo &info = E.second;
        if (info.is_singleton) {
            for (int i = 0; i < ScriptServer::get_language_count(); i++) {
                ScriptServer::get_language(i)->remove_named_global_constant(info.name);
            }
        }
        if (info.in_editor) {
            ERR_CONTINUE(!info.node);
            get_tree()->get_root()->call_deferred("remove_child", Variant(info.node));
        }

        if (info.node) {
            info.node->queue_delete();
            info.node = nullptr;
        }
    }

    // Load new/changed autoloads
    Vector<Node *> nodes_to_add;
    for (AutoLoadInfo *info : to_add) {

        info->node = _create_autoload(info->path);

        ERR_CONTINUE(!info->node);
        info->node->set_name(info->name);

        Ref<Script> scr = refFromRefPtr<Script>(info->node->get_script());
        info->in_editor = scr && scr->is_tool();

        if (info->in_editor) {
            //defer so references are all valid on _ready()
            nodes_to_add.push_back(info->node);
        }

        if (info->is_singleton) {
            for (int i = 0; i < ScriptServer::get_language_count(); i++) {
                ScriptServer::get_language(i)->add_named_global_constant(info->name, Variant(info->node));
            }
        }

        if (!info->in_editor && !info->is_singleton) {
            // No reason to keep this node
            memdelete(info->node);
            info->node = nullptr;
        }
    }

    for (Node * E : nodes_to_add) {
        get_tree()->get_root()->add_child(E);
    }

    updating_autoload = false;
}

Variant EditorAutoloadSettings::get_drag_data_fw(const Point2 &p_point, Control *p_control) {

    if (autoload_cache.size() <= 1)
        return false;

    PoolVector<String> autoloads;

    TreeItem *next = tree->get_next_selected(nullptr);

    while (next) {
        autoloads.push_back(next->get_text(0));
        next = tree->get_next_selected(next);
    }

    if (autoloads.size() == 0 || autoloads.size() == autoload_cache.size())
        return Variant();

    VBoxContainer *preview = memnew(VBoxContainer);

    int max_size = MIN(PREVIEW_LIST_MAX_SIZE, autoloads.size());

    for (int i = 0; i < max_size; i++) {
        Label *label = memnew(Label(StringName(autoloads[i])));
        label->set_self_modulate(Color(1, 1, 1, Math::lerp(1, 0, float(i) / PREVIEW_LIST_MAX_SIZE)));

        preview->add_child(label);
    }

    tree->set_drop_mode_flags(Tree::DROP_MODE_INBETWEEN);
    tree->set_drag_preview(preview);

    Dictionary drop_data;
    drop_data["type"] = "autoload";
    drop_data["autoloads"] = autoloads;

    return drop_data;
}

bool EditorAutoloadSettings::can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_control) const {
    if (updating_autoload)
        return false;

    Dictionary drop_data = p_data;

    if (!drop_data.has("type"))
        return false;

    if (drop_data.has("type")) {
        TreeItem *ti = tree->get_item_at_position(p_point);

        if (!ti)
            return false;

        int section = tree->get_drop_section_at_position(p_point);

        return section >= -1;
    }

    return false;
}

void EditorAutoloadSettings::drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_control) {

    TreeItem *ti = tree->get_item_at_position(p_point);

    if (!ti)
        return;

    int section = tree->get_drop_section_at_position(p_point);

    if (section < -1)
        return;

    String name;
    bool move_to_back = false;

    if (section < 0) {
        name = ti->get_text(0);
    } else if (ti->get_next()) {
        name = ti->get_next()->get_text(0);
    } else {
        name = ti->get_text(0);
        move_to_back = true;
    }

    int order = ProjectSettings::get_singleton()->get_order(StringName("autoload/" + name));

    AutoLoadInfo aux;
    auto E = autoload_cache.end();

    if (!move_to_back) {
        aux.order = order;
        E = autoload_cache.find(aux);
    }

    Dictionary drop_data = p_data;
    PoolVector<String> autoloads = drop_data["autoloads"].as<PoolVector<String>>();

    Vector<int> orders;
    orders.reserve(autoload_cache.size());
    if(move_to_back) {
        for (int i = 0; i < autoloads.size(); i++) {
            aux.order = ProjectSettings::get_singleton()->get_order(StringName("autoload/" + autoloads[i]));

            auto I = autoload_cache.find(aux);
            auto tmp = *I;
            autoload_cache.erase(I);
            autoload_cache.emplace_back(tmp);
        }
    }
    else {
        for (int i = 0; i < autoloads.size(); i++) {
            if (E==autoload_cache.end())
                break; // everything is before end()

            aux.order = ProjectSettings::get_singleton()->get_order(StringName("autoload/" + autoloads[i]));

            auto I = autoload_cache.find(aux);
            if(I<E) // already before
                continue;
            if(I==E)
                ++E; // reached E, move goalpost
            else { // we know that I is after E so iterator E is not invalidated
                auto tmp = *I;
                autoload_cache.erase(I);
                autoload_cache.insert(E,tmp);
            }
        }
    }


    for (const AutoLoadInfo &F : autoload_cache) {
        orders.emplace_back(F.order);
    }
    eastl::sort(orders.begin(),orders.end());

    UndoRedo *undo_redo = EditorNode::get_undo_redo();

    undo_redo->create_action(TTR("Rearrange Autoloads"));

    int i = 0;

    for (AutoLoadInfo &F : autoload_cache) {
        undo_redo->add_do_method(ProjectSettings::get_singleton(), "set_order", String("autoload/") + F.name, orders[i++]);
        undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_order", String("autoload/") + F.name, F.order);
    }

    orders.clear();

    undo_redo->add_do_method(this, "update_autoload");
    undo_redo->add_undo_method(this, "update_autoload");

    undo_redo->add_do_method(this, "emit_signal", autoload_changed);
    undo_redo->add_undo_method(this, "emit_signal", autoload_changed);

    undo_redo->commit_action();
}

bool EditorAutoloadSettings::autoload_add(const StringName &p_name, StringView p_path) {

    String name(p_name);

    String error;
    if (!_autoload_name_is_valid(p_name, &error)) {
        EditorNode::get_singleton()->show_warning(StringName(error));
        return false;
    }

    const StringView path = p_path;
    if (!FileAccess::exists(path)) {
        EditorNode::get_singleton()->show_warning(TTR("Invalid path.") + "\n" + TTR("File does not exist."));
        return false;
    }

    if (!StringUtils::begins_with(path,"res://")) {
        EditorNode::get_singleton()->show_warning(TTR("Invalid path.") + "\n" + TTR("Not in resource path."));
        return false;
    }

    name = "autoload/" + name;

    UndoRedo *undo_redo = EditorNode::get_undo_redo();

    undo_redo->create_action(TTR("Add AutoLoad"));
    // Singleton autoloads are represented with a leading "*" in their path.
    undo_redo->add_do_property(ProjectSettings::get_singleton(), name, String("*") + path);

    if (ProjectSettings::get_singleton()->has_setting(StringName(name))) {
        undo_redo->add_undo_property(ProjectSettings::get_singleton(), name, ProjectSettings::get_singleton()->get(StringName(name)));
    } else {
        undo_redo->add_undo_property(ProjectSettings::get_singleton(), name, Variant());
    }

    undo_redo->add_do_method(this, "update_autoload");
    undo_redo->add_undo_method(this, "update_autoload");

    undo_redo->add_do_method(this, "emit_signal", autoload_changed);
    undo_redo->add_undo_method(this, "emit_signal", autoload_changed);

    undo_redo->commit_action();

    return true;
}

void EditorAutoloadSettings::autoload_remove(const StringName &p_name) {

    StringName name(String("autoload/") + p_name);

    UndoRedo *undo_redo = EditorNode::get_undo_redo();

    int order = ProjectSettings::get_singleton()->get_order(name);

    undo_redo->create_action(TTR("Remove Autoload"));

    undo_redo->add_do_property(ProjectSettings::get_singleton(), name, Variant());

    undo_redo->add_undo_property(ProjectSettings::get_singleton(), name, ProjectSettings::get_singleton()->get(name));
    undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_persisting", name, true);
    undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_order", order);

    undo_redo->add_do_method(this, "update_autoload");
    undo_redo->add_undo_method(this, "update_autoload");

    undo_redo->add_do_method(this, "emit_signal", autoload_changed);
    undo_redo->add_undo_method(this, "emit_signal", autoload_changed);

    undo_redo->commit_action();
}

void EditorAutoloadSettings::_bind_methods() {

    MethodBinder::bind_method("_autoload_add", &EditorAutoloadSettings::_autoload_add);
    MethodBinder::bind_method("_autoload_selected", &EditorAutoloadSettings::_autoload_selected);
    MethodBinder::bind_method("_autoload_edited", &EditorAutoloadSettings::_autoload_edited);
    MethodBinder::bind_method("_autoload_button_pressed", &EditorAutoloadSettings::_autoload_button_pressed);
    MethodBinder::bind_method("_autoload_activated", &EditorAutoloadSettings::_autoload_activated);
    MethodBinder::bind_method("_autoload_path_text_changed", &EditorAutoloadSettings::_autoload_path_text_changed);
    MethodBinder::bind_method("_autoload_text_entered", &EditorAutoloadSettings::_autoload_text_entered);
    MethodBinder::bind_method("_autoload_text_changed", &EditorAutoloadSettings::_autoload_text_changed);
    MethodBinder::bind_method("_autoload_open", &EditorAutoloadSettings::_autoload_open);
    MethodBinder::bind_method("_autoload_file_callback", &EditorAutoloadSettings::_autoload_file_callback);

    MethodBinder::bind_method("get_drag_data_fw", &EditorAutoloadSettings::get_drag_data_fw);
    MethodBinder::bind_method("can_drop_data_fw", &EditorAutoloadSettings::can_drop_data_fw);
    MethodBinder::bind_method("drop_data_fw", &EditorAutoloadSettings::drop_data_fw);

    MethodBinder::bind_method("update_autoload", &EditorAutoloadSettings::update_autoload);
    MethodBinder::bind_method("autoload_add", &EditorAutoloadSettings::autoload_add);
    MethodBinder::bind_method("autoload_remove", &EditorAutoloadSettings::autoload_remove);

    ADD_SIGNAL(MethodInfo("autoload_changed"));
}

EditorAutoloadSettings::EditorAutoloadSettings() {

    // Make first cache
    Vector<PropertyInfo> props;
    ProjectSettings::get_singleton()->get_property_list(&props);
    for (const PropertyInfo &pi : props) {

        if (!StringUtils::begins_with(pi.name,"autoload/"))
            continue;

        String name(StringUtils::get_slice(pi.name,"/", 1));
        String path = ProjectSettings::get_singleton()->get(pi.name);

        if (name.empty())
            continue;

        AutoLoadInfo info;
        info.is_singleton = StringUtils::begins_with(path,"*");

        if (info.is_singleton) {
            path = StringUtils::substr(path,1, path.length());
        }

        info.name = StringName(name);
        info.path = path;
        info.order = ProjectSettings::get_singleton()->get_order(pi.name);

        if (info.is_singleton) {
            // Make sure name references work before parsing scripts
            for (int i = 0; i < ScriptServer::get_language_count(); i++) {
                ScriptServer::get_language(i)->add_named_global_constant(info.name, Variant());
            }
        }

        autoload_cache.push_back(info);
    }

    for (AutoLoadInfo &info :autoload_cache) {

        info.node = _create_autoload(info.path);

        if (info.node) {
            Ref<Script> scr(refFromRefPtr<Script>(info.node->get_script()));
            info.in_editor = scr && scr->is_tool();
            info.node->set_name(info.name);
        }

        if (info.is_singleton) {
            for (int i = 0; i < ScriptServer::get_language_count(); i++) {
                ScriptServer::get_language(i)->add_named_global_constant(info.name, Variant(info.node));
            }
        }

        if (!info.is_singleton && !info.in_editor && info.node != nullptr) {
            memdelete(info.node);
            info.node = nullptr;
        }
    }

    autoload_changed = "autoload_changed";

    updating_autoload = false;
    selected_autoload = "";

    HBoxContainer *hbc = memnew(HBoxContainer);
    add_child(hbc);

    Label *l = memnew(Label);
    l->set_text(TTR("Path:"));
    hbc->add_child(l);

    autoload_add_path = memnew(EditorLineEditFileChooser);
    autoload_add_path->set_h_size_flags(SIZE_EXPAND_FILL);
    autoload_add_path->get_file_dialog()->set_mode(EditorFileDialog::MODE_OPEN_FILE);
    autoload_add_path->get_file_dialog()->connect("file_selected", this, "_autoload_file_callback");
    autoload_add_path->get_line_edit()->connect("text_changed", this, "_autoload_path_text_changed");
    hbc->add_child(autoload_add_path);

    l = memnew(Label);
    l->set_text(TTR("Node Name:"));
    hbc->add_child(l);

    autoload_add_name = memnew(LineEdit);
    autoload_add_name->set_h_size_flags(SIZE_EXPAND_FILL);
    autoload_add_name->connect("text_entered", this, "_autoload_text_entered");
    autoload_add_name->connect("text_changed", this, "_autoload_text_changed");
    hbc->add_child(autoload_add_name);

    add_autoload = memnew(Button);
    add_autoload->set_text(TTR("Add"));
    add_autoload->connect("pressed", this, "_autoload_add");
    // The button will be enabled once a valid name is entered (either automatically or manually).
    add_autoload->set_disabled(true);
    hbc->add_child(add_autoload);

    tree = memnew(Tree);
    tree->set_hide_root(true);
    tree->set_select_mode(Tree::SELECT_MULTI);
    tree->set_allow_reselect(true);

    tree->set_drag_forwarding(this);

    tree->set_columns(4);
    tree->set_column_titles_visible(true);

    tree->set_column_title(0, TTR("Name"));
    tree->set_column_expand(0, true);
    tree->set_column_min_width(0, 100);

    tree->set_column_title(1, TTR("Path"));
    tree->set_column_expand(1, true);
    tree->set_column_min_width(1, 100);

    tree->set_column_title(2, TTR("Singleton"));
    tree->set_column_expand(2, false);
    tree->set_column_min_width(2, 80 * EDSCALE);

    tree->set_column_expand(3, false);
    tree->set_column_min_width(3, 120 * EDSCALE);

    tree->connect("cell_selected", this, "_autoload_selected");
    tree->connect("item_edited", this, "_autoload_edited");
    tree->connect("button_pressed", this, "_autoload_button_pressed");
    tree->connect("item_activated", this, "_autoload_activated");
    tree->set_v_size_flags(SIZE_EXPAND_FILL);

    add_child(tree, true);
}

EditorAutoloadSettings::~EditorAutoloadSettings() {
    for (AutoLoadInfo &info : autoload_cache) {
        if (info.node && !info.in_editor) {
            memdelete(info.node);
        }
    }
}
