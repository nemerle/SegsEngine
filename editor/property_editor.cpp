/*************************************************************************/
/*  property_editor.cpp                                                  */
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

#include "filesystem_dock.h"
#include "property_editor.h"

#include "core/class_db.h"
#include "core/io/image_loader.h"
#include "core/io/marshalls.h"
#include "core/io/resource_loader.h"
#include "core/math/expression.h"
#include "core/method_bind.h"
#include "core/object_db.h"
#include "core/os/input.h"
#include "core/os/keyboard.h"
#include "core/pair.h"
#include "core/print_string.h"
#include "core/project_settings.h"
#include "core/string_formatter.h"
#include "core/resources_subsystem/resource_manager.h"
#include "editor/array_property_edit.h"
#include "editor/create_dialog.h"
#include "editor/dictionary_property_edit.h"
#include "editor/editor_export.h"
#include "editor/editor_file_system.h"
#include "editor/editor_help.h"
#include "editor/editor_node.h"
#include "editor/editor_scale.h"
#include "editor/editor_settings.h"
#include "editor/multi_node_edit.h"
#include "editor/property_selector.h"
#include "editor/scene_tree_dock.h"
#include "scene/gui/label.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/font.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/style_box.h"
#include "scene/scene_string_names.h"

IMPL_GDCLASS(EditorResourceConversionPlugin)
IMPL_GDCLASS(CustomPropertyEditor)

void EditorResourceConversionPlugin::_bind_methods() {

    MethodInfo mi;
    mi.name = "_convert";
    mi.return_val.type = VariantType::OBJECT;
    mi.return_val.class_name = "Resource";
    mi.return_val.hint = PropertyHint::ResourceType;
    mi.return_val.hint_string = "Resource";
    mi.arguments.push_back(mi.return_val);
    mi.arguments[0].name = "resource";

    BIND_VMETHOD(mi)

    mi.name = "_handles";
    mi.return_val = PropertyInfo(VariantType::BOOL, "");

    BIND_VMETHOD(MethodInfo(VariantType::STRING, "_converts_to"));
}

StringName EditorResourceConversionPlugin::converts_to() const {

    if (get_script_instance())
        return get_script_instance()->call("_converts_to");

    return StringName();
}

bool EditorResourceConversionPlugin::handles(const Ref<Resource> &p_resource) const {

    if (get_script_instance())
        return get_script_instance()->call("_handles", p_resource);

    return false;
}

Ref<Resource> EditorResourceConversionPlugin::convert(const Ref<Resource> &p_resource) const {

    if (get_script_instance())
        return refFromRefPtr<Resource>(get_script_instance()->call("_convert", p_resource));

    return Ref<Resource>();
}

void CustomPropertyEditor::_notification(int p_what) {

    if (p_what == NOTIFICATION_DRAW) {

        RID ci = get_canvas_item();
        get_stylebox("panel", "PopupMenu")->draw(ci, Rect2(Point2(), get_size()));
    }
    if (p_what == MainLoop::NOTIFICATION_WM_QUIT_REQUEST) {
        hide();
    }
}

void CustomPropertyEditor::_menu_option(int p_which) {

    switch (type) {

        case VariantType::INT: {

            if (hint == PropertyHint::Flags) {

                int val = v;

                if (val & 1 << p_which) {

                    val &= ~(1 << p_which);
                } else {
                    val |= 1 << p_which;
                }

                v = val;
                emit_signal("variant_changed");
            } else if (hint == PropertyHint::Enum) {

                v = menu->get_item_metadata(p_which);
                emit_signal("variant_changed");
            }
        } break;
        case VariantType::STRING: {

            if (hint == PropertyHint::Enum) {

                v = StringUtils::get_slice(hint_text,',', p_which);
                emit_signal("variant_changed");
            }
        } break;
        case VariantType::OBJECT: {

            switch (p_which) {
                case OBJ_MENU_LOAD: {

                    file->set_mode(EditorFileDialog::MODE_OPEN_FILE);
                    String type = hint == PropertyHint::ResourceType ? hint_text : String();

                    Vector<String> extensions;
                    for (int i = 0; i < StringUtils::get_slice_count(type,','); i++) {

                        ResourceLoader::get_recognized_extensions_for_type(StringUtils::get_slice(type,',', i), extensions);
                    }

                    Set<String> valid_extensions;
                    for (const String &E : extensions) {
                        valid_extensions.insert(E);
                    }

                    file->clear_filters();
                    for (const String &E : valid_extensions) {

                        file->add_filter("*." + E + " ; " + StringUtils::to_upper(E));
                    }

                    file->popup_centered_ratio();
                } break;

                case OBJ_MENU_EDIT: {

                    RefPtr RefPtr = v;

                    if (!RefPtr.is_null()) {

                        emit_signal("resource_edit_request");
                        hide();
                    }
                } break;
                case OBJ_MENU_CLEAR: {

                    v = Variant();
                    emit_signal("variant_changed");
                    hide();
                } break;

                case OBJ_MENU_MAKE_UNIQUE: {

                    RefPtr refPtr = v;
                    Ref<Resource> res_orig(refFromRefPtr<Resource>(refPtr));
                    if (not res_orig)
                        return;

                    Vector<PropertyInfo> property_list;
                    res_orig->get_property_list(&property_list);
                    Vector<Pair<StringName, Variant> > propvalues;

                    for (const PropertyInfo &pi : property_list) {

                        Pair<StringName, Variant> p;
                        if (pi.usage & PROPERTY_USAGE_STORAGE) {

                            p.first = pi.name;
                            p.second = res_orig->get(pi.name);
                        }

                        propvalues.push_back(p);
                    }

                    StringName orig_type(res_orig->get_class());

                    Object *inst = ClassDB::instance(orig_type);

                    Ref<Resource> res(object_cast<Resource>(inst));

                    ERR_FAIL_COND(not res);

                    for (Pair<StringName, Variant> &p : propvalues) {
                        res->set(p.first, p.second);
                    }

                    v = Variant(res.get_ref_ptr());
                    emit_signal("variant_changed");
                    hide();
                } break;

                case OBJ_MENU_COPY: {

                    EditorSettings::get_singleton()->set_resource_clipboard(refFromRefPtr<Resource>(v));

                } break;
                case OBJ_MENU_PASTE: {

                    v = EditorSettings::get_singleton()->get_resource_clipboard();
                    emit_signal("variant_changed");

                } break;
                case OBJ_MENU_NEW_SCRIPT: {

                    if (object_cast<Node>(owner))
                        EditorNode::get_singleton()->get_scene_tree_dock()->open_script_dialog(object_cast<Node>(owner), false);

                } break;
                case OBJ_MENU_EXTEND_SCRIPT: {

                    if (object_cast<Node>(owner))
                        EditorNode::get_singleton()->get_scene_tree_dock()->open_script_dialog(object_cast<Node>(owner), true);

                } break;
                case OBJ_MENU_SHOW_IN_FILE_SYSTEM: {
                    RES r(v);
                    FileSystemDock *file_system_dock = EditorNode::get_singleton()->get_filesystem_dock();
                    file_system_dock->navigate_to_path(r->get_path().to_string());
                    // Ensure that the FileSystem dock is visible.
                    TabContainer *tab_container = (TabContainer *)file_system_dock->get_parent_control();
                    tab_container->set_current_tab(file_system_dock->get_position_in_parent());
                } break;
                default: {

                    if (p_which >= CONVERT_BASE_ID) {

                        int to_type = p_which - CONVERT_BASE_ID;

                        Vector<Ref<EditorResourceConversionPlugin> > conversions = EditorNode::get_singleton()->find_resource_conversion_plugin(RES(v));

                        ERR_FAIL_INDEX(to_type, conversions.size());

                        Ref<Resource> new_res = conversions[to_type]->convert(refFromRefPtr<Resource>(v));

                        v = new_res;
                        emit_signal("variant_changed");
                        break;
                    }
                    ERR_FAIL_COND(inheritors_array.empty());

                    StringName intype(inheritors_array[p_which - TYPE_BASE_ID]);

                    if (intype == "ViewportTexture") {

                        scene_tree->set_title(TTR("Pick a Viewport"));
                        scene_tree->popup_centered_ratio();
                        picking_viewport = true;
                        return;
                    }

                    Object *obj = ClassDB::instance(intype);

                    if (!obj) {
                        if (ScriptServer::is_global_class(intype)) {
                            obj = EditorNode::get_editor_data().script_class_instance(intype);
                        } else {
                            obj = EditorNode::get_editor_data().instance_custom_type(intype, "Resource");
                        }
                    }

                    ERR_BREAK(!obj);
                    Resource *res = object_cast<Resource>(obj);
                    ERR_BREAK(!res);
                    if (owner && hint == PropertyHint::ResourceType && hint_text == "Script") {
                        //make visual script the right type
                        res->call_va("set_instance_base_type", owner->get_class());
                    }

                    v = Variant(Ref<Resource>(res));
                    emit_signal("variant_changed");

                } break;
            }

        } break;
        default: {
        }
    }
}

void CustomPropertyEditor::hide_menu() {
    menu->hide();
}

Variant CustomPropertyEditor::get_variant() const {

    return v;
}

UIString CustomPropertyEditor::get_name() const {

    return name;
}

bool CustomPropertyEditor::edit(Object *p_owner, StringView p_name, VariantType p_type, const Variant &p_variant, PropertyHint p_hint, StringView p_hint_text) {

    using namespace eastl;

    owner = p_owner;
    updating = true;
    name = StringUtils::from_utf8(p_name);
    v = p_variant;
    field_names.clear();
    hint = PropertyHint(p_hint);
    hint_text = p_hint_text;
    type_button->hide();
    if (color_picker)
        color_picker->hide();
    texture_preview->hide();
    inheritors_array.clear();
    text_edit->hide();
    easing_draw->hide();
    spinbox->hide();
    slider->hide();
    menu->clear();
    menu->set_size(Size2(1, 1) * EDSCALE);

    for (int i = 0; i < MAX_VALUE_EDITORS; i++) {

        value_editor[i]->hide();
        value_label[i]->hide();
        if (i < 4)
            scroll[i]->hide();
    }

    for (auto & action_button : action_buttons) {

        action_button->hide();
    }

    checks20gc->hide();
    for (auto & i : checks20)
        i->hide();

    type = p_variant.get_type() != VariantType::NIL && p_variant.get_type() != VariantType::_RID && p_type != VariantType::OBJECT ? p_variant.get_type() : p_type;

    switch (type) {

        case VariantType::BOOL: {

            checks20gc->show();

            CheckBox *c = checks20[0];
            c->set_text("True");
            checks20gc->set_position(Vector2(4, 4) * EDSCALE);
            c->set_pressed(v);
            c->show();

            checks20gc->set_size(checks20gc->get_minimum_size());
            set_size(checks20gc->get_position() + checks20gc->get_size() + c->get_size() + Vector2(4, 4) * EDSCALE);

        } break;
        case VariantType::INT:
        case VariantType::REAL: {

            if (hint == PropertyHint::Range) {

                int c = StringUtils::get_slice_count(hint_text,',');
                float min = 0, max = 100, step = type == VariantType::REAL ? .01f : 1;
                if (c >= 1) {

                    if (!StringUtils::get_slice(hint_text,',', 0).empty())
                        min = StringUtils::to_double(StringUtils::get_slice(hint_text,',', 0));
                }
                if (c >= 2) {

                    if (!StringUtils::get_slice(hint_text,',', 1).empty())
                        max = StringUtils::to_double(StringUtils::get_slice(hint_text,',', 1));
                }

                if (c >= 3) {

                    if (!StringUtils::get_slice(hint_text,',', 2).empty())
                        step = StringUtils::to_double(StringUtils::get_slice(hint_text,',', 2));
                }

                if (c >= 4 && StringUtils::get_slice(hint_text,',', 3) == "slider"_sv) {
                    slider->set_min(min);
                    slider->set_max(max);
                    slider->set_step(step);
                    slider->set_value(v);
                    slider->show();
                    set_size(Size2(110, 30) * EDSCALE);
                } else {
                    spinbox->set_min(min);
                    spinbox->set_max(max);
                    spinbox->set_step(step);
                    spinbox->set_value(v);
                    spinbox->show();
                    set_size(Size2(70, 35) * EDSCALE);
                }

            } else if (hint == PropertyHint::Enum) {

                Vector<StringView> options = StringUtils::split(hint_text,',');
                int current_val = 0;
                for (int i = 0; i < options.size(); i++) {
                    Vector<StringView> text_split = StringUtils::split(options[i],':');
                    if (text_split.size() != 1)
                        current_val = StringUtils::to_int(text_split[1]);
                    menu->add_item(StringName(text_split[0]));
                    menu->set_item_metadata(i, current_val);
                    current_val += 1;
                }
                menu->set_position(get_position());
                menu->popup();
                hide();
                updating = false;
                return false;

            } else if (hint == PropertyHint::Layers2DPhysics || hint == PropertyHint::Layers2DRenderer || hint == PropertyHint::Layers3DPhysics || hint == PropertyHint::Layers3DRenderer) {

                String basename;
                switch (hint) {
                    case PropertyHint::Layers2DRenderer:
                        basename = "layer_names/2d_render";
                        break;
                    case PropertyHint::Layers2DPhysics:
                        basename = "layer_names/2d_physics";
                        break;
                    case PropertyHint::Layers3DRenderer:
                        basename = "layer_names/3d_render";
                        break;
                    case PropertyHint::Layers3DPhysics:
                        basename = "layer_names/3d_physics";
                        break;
                }

                checks20gc->show();
                uint32_t flgs = v;
                for (int i = 0; i < 2; i++) {

                    Point2 ofs(4, 4);
                    ofs.y += 22 * i;
                    for (int j = 0; j < 10; j++) {

                        int idx = i * 10 + j;
                        CheckBox *c = checks20[idx];
                        c->set_text(ProjectSettings::get_singleton()->get(StringName(basename + "/layer_" + itos(idx + 1))));
                        c->set_pressed(flgs & 1 << i * 10 + j);
                        c->show();
                    }
                }

                show();

                checks20gc->set_position(Vector2(4, 4) * EDSCALE);
                checks20gc->set_size(checks20gc->get_minimum_size());

                set_size(Vector2(4, 4) * EDSCALE + checks20gc->get_position() + checks20gc->get_size());

            } else if (hint == PropertyHint::ExpEasing) {

                easing_draw->set_anchor_and_margin(Margin::Left, ANCHOR_BEGIN, 5 * EDSCALE);
                easing_draw->set_anchor_and_margin(Margin::Right, ANCHOR_END, -5 * EDSCALE);
                easing_draw->set_anchor_and_margin(Margin::Top, ANCHOR_BEGIN, 5 * EDSCALE);
                easing_draw->set_anchor_and_margin(Margin::Bottom, ANCHOR_END, -30 * EDSCALE);
                type_button->set_anchor_and_margin(Margin::Left, ANCHOR_BEGIN, 3 * EDSCALE);
                type_button->set_anchor_and_margin(Margin::Right, ANCHOR_END, -3 * EDSCALE);
                type_button->set_anchor_and_margin(Margin::Top, ANCHOR_END, -25 * EDSCALE);
                type_button->set_anchor_and_margin(Margin::Bottom, ANCHOR_END, -7 * EDSCALE);
                type_button->set_text(TTR("Preset..."));
                type_button->get_popup()->clear();
                type_button->get_popup()->add_item(TTR("Linear"), EASING_LINEAR);
                type_button->get_popup()->add_item(TTR("Ease In"), EASING_EASE_IN);
                type_button->get_popup()->add_item(TTR("Ease Out"), EASING_EASE_OUT);
                if (hint_text != "attenuation") {
                    type_button->get_popup()->add_item(TTR("Zero"), EASING_ZERO);
                    type_button->get_popup()->add_item(TTR("Easing In-Out"), EASING_IN_OUT);
                    type_button->get_popup()->add_item(TTR("Easing Out-In"), EASING_OUT_IN);
                }

                type_button->show();
                easing_draw->show();
                set_size(Size2(200, 150) * EDSCALE);
            } else if (hint == PropertyHint::Flags) {
                Vector<StringView> flags = StringUtils::split(hint_text,',');
                for (int i = 0; i < flags.size(); i++) {
                    StringView flag = flags[i];
                    if (flag.empty())
                        continue;
                    menu->add_check_item_utf8(flag, i);
                    int f = v;
                    if (f & 1 << i)
                        menu->set_item_checked(menu->get_item_index(i), true);
                }
                menu->set_position(get_position());
                menu->popup();
                hide();
                updating = false;
                return false;

            } else {
                Vector<StringName> names;
                names.push_back(StringName("value:"));
                config_value_editors(1, 1, 50, names);
                value_editor[0]->set_text(StringUtils::num(v));
            }

        } break;
        case VariantType::STRING: {

            if (hint == PropertyHint::File || hint == PropertyHint::GlobalFile) {

                const StringName names[2] {
                    TTR("File..."),
                    TTR("Clear")
                };
                config_action_buttons(names);

            } else if (hint == PropertyHint::Dir || hint == PropertyHint::GlobalDir) {

                const StringName names[2] {
                    TTR("Dir..."),
                    TTR("Clear")
                };
                config_action_buttons(names);
            } else if (hint == PropertyHint::Enum) {

                Vector<StringView> options = StringUtils::split(hint_text,',');
                for (int i = 0; i < options.size(); i++) {
                    menu->add_item(StringName(options[i]), i);
                }
                menu->set_position(get_position());
                menu->popup();
                hide();
                updating = false;
                return false;

            } else if (hint == PropertyHint::MultilineText) {

                text_edit->show();
                text_edit->set_text(v);
                text_edit->deselect();

                int button_margin = get_constant("button_margin", "Dialogs");
                int margin = get_constant("margin", "Dialogs");

                action_buttons[0]->set_anchor(Margin::Left, ANCHOR_END);
                action_buttons[0]->set_anchor(Margin::Top, ANCHOR_END);
                action_buttons[0]->set_anchor(Margin::Right, ANCHOR_END);
                action_buttons[0]->set_anchor(Margin::Bottom, ANCHOR_END);
                action_buttons[0]->set_begin(Point2(-70 * EDSCALE, -button_margin + 5 * EDSCALE));
                action_buttons[0]->set_end(Point2(-margin, -margin));
                action_buttons[0]->set_text(TTR("Close"));
                action_buttons[0]->show();

            } else if (hint == PropertyHint::TypeString) {

                if (!create_dialog) {
                    create_dialog = memnew(CreateDialog);
                    create_dialog->connect("create", this, "_create_dialog_callback");
                    add_child(create_dialog);
                }

                if (!hint_text.empty()) {
                    create_dialog->set_base_type(StringName(hint_text));
                } else {
                    create_dialog->set_base_type(StringName("Object"));
                }

                create_dialog->popup_create(false);
                hide();
                updating = false;
                return false;

            } else if (hint == PropertyHint::MethodOfVariantType) {
#define MAKE_PROPSELECT                                                          \
    if (!property_select) {                                                      \
        property_select = memnew(PropertySelector);                              \
        property_select->connect("selected", this, "_create_selected_property"); \
        add_child(property_select);                                              \
    }                                                                            \
    hide();

                MAKE_PROPSELECT;

                VariantType type = VariantType::NIL;
                for (int i = 0; i < (int)VariantType::VARIANT_MAX; i++) {
                    if (hint_text == Variant::get_type_name(VariantType(i))) {
                        type = VariantType(i);
                    }
                }
                if (type != VariantType::NIL)
                    property_select->select_method_from_basic_type(type, v);
                updating = false;
                return false;

            } else if (hint == PropertyHint::MethodOfBaseType) {
                MAKE_PROPSELECT

                property_select->select_method_from_base_type(StringName(hint_text), v);

                updating = false;
                return false;

            } else if (hint == PropertyHint::MethodOfInstance) {

                MAKE_PROPSELECT

                Object *instance = ObjectDB::get_instance(StringUtils::to_int64(hint_text));
                if (instance)
                    property_select->select_method_from_instance(instance, v);
                updating = false;
                return false;

            } else if (hint == PropertyHint::MethodOfScript) {
                MAKE_PROPSELECT

                Object *obj = ObjectDB::get_instance(StringUtils::to_int64(hint_text));
                if (object_cast<Script>(obj)) {
                    property_select->select_method_from_script(Ref<Script>(object_cast<Script>(obj)), v);
                }

                updating = false;
                return false;

            } else if (hint == PropertyHint::PropertyOfVariantType) {

                MAKE_PROPSELECT
                VariantType type = VariantType::NIL;
                StringView tname(hint_text);
                if (StringUtils::contains(tname,'.'))
                    tname = StringUtils::get_slice(tname,".", 0);
                for (int i = 0; i < (int)VariantType::VARIANT_MAX; i++) {
                    if (tname == StringView(Variant::get_type_name(VariantType(i)))) {
                        type = VariantType(VariantType(i));
                    }
                }

                if (type != VariantType::NIL)
                    property_select->select_property_from_basic_type(type, v);

                updating = false;
                return false;

            } else if (hint == PropertyHint::PropertyOfBaseType) {

                MAKE_PROPSELECT

                property_select->select_property_from_base_type(StringName(hint_text), v);

                updating = false;
                return false;

            } else if (hint == PropertyHint::PropertyOfInstance) {

                MAKE_PROPSELECT

                Object *instance = ObjectDB::get_instance(StringUtils::to_int64(hint_text));
                if (instance)
                    property_select->select_property_from_instance(instance, v);

                updating = false;
                return false;

            } else if (hint == PropertyHint::PropertyOfScript) {
                MAKE_PROPSELECT

                Object *obj = ObjectDB::get_instance(StringUtils::to_int64(hint_text));
                if (object_cast<Script>(obj)) {
                    property_select->select_property_from_script(Ref<Script>(object_cast<Script>(obj)), v);
                }

                updating = false;
                return false;

            } else {
                Vector<StringName> names;
                names.push_back(StringName("string:"));
                config_value_editors(1, 1, 50, names);
                value_editor[0]->set_text_uistring(v);
            }

        } break;
        case VariantType::VECTOR2: {

            field_names.push_back("x");
            field_names.push_back("y");
            config_value_editors_utf8(2, 2, 10, field_names);
            Vector2 vec = v;
            value_editor[0]->set_text(StringUtils::num(vec.x));
            value_editor[1]->set_text(StringUtils::num(vec.y));
        } break;
        case VariantType::RECT2: {

            field_names.push_back("x");
            field_names.push_back("y");
            field_names.push_back("w");
            field_names.push_back("h");
            config_value_editors_utf8(4, 4, 10, field_names);
            Rect2 r = v;
            value_editor[0]->set_text(StringUtils::num(r.position.x));
            value_editor[1]->set_text(StringUtils::num(r.position.y));
            value_editor[2]->set_text(StringUtils::num(r.size.x));
            value_editor[3]->set_text(StringUtils::num(r.size.y));
        } break;
        case VariantType::VECTOR3: {

            field_names.push_back("x");
            field_names.push_back("y");
            field_names.push_back("z");
            config_value_editors_utf8(3, 3, 10, field_names);
            Vector3 vec = v;
            value_editor[0]->set_text(StringUtils::num(vec.x));
            value_editor[1]->set_text(StringUtils::num(vec.y));
            value_editor[2]->set_text(StringUtils::num(vec.z));
        } break;
        case VariantType::PLANE: {

            field_names.push_back("x");
            field_names.push_back("y");
            field_names.push_back("z");
            field_names.push_back("d");
            config_value_editors_utf8(4, 4, 10, field_names);
            Plane plane = v;
            value_editor[0]->set_text(StringUtils::num(plane.normal.x));
            value_editor[1]->set_text(StringUtils::num(plane.normal.y));
            value_editor[2]->set_text(StringUtils::num(plane.normal.z));
            value_editor[3]->set_text(StringUtils::num(plane.d));

        } break;
        case VariantType::QUAT: {

            field_names.push_back("x");
            field_names.push_back("y");
            field_names.push_back("z");
            field_names.push_back("w");
            config_value_editors_utf8(4, 4, 10, field_names);
            Quat q = v;
            value_editor[0]->set_text(StringUtils::num(q.x));
            value_editor[1]->set_text(StringUtils::num(q.y));
            value_editor[2]->set_text(StringUtils::num(q.z));
            value_editor[3]->set_text(StringUtils::num(q.w));

        } break;
        case VariantType::AABB: {

            field_names.push_back("px");
            field_names.push_back("py");
            field_names.push_back("pz");
            field_names.push_back("sx");
            field_names.push_back("sy");
            field_names.push_back("sz");
            config_value_editors_utf8(6, 3, 16, field_names);

            AABB aabb = v;
            value_editor[0]->set_text(StringUtils::num(aabb.position.x));
            value_editor[1]->set_text(StringUtils::num(aabb.position.y));
            value_editor[2]->set_text(StringUtils::num(aabb.position.z));
            value_editor[3]->set_text(StringUtils::num(aabb.size.x));
            value_editor[4]->set_text(StringUtils::num(aabb.size.y));
            value_editor[5]->set_text(StringUtils::num(aabb.size.z));

        } break;
        case VariantType::TRANSFORM2D: {

            field_names.push_back("xx");
            field_names.push_back("xy");
            field_names.push_back("yx");
            field_names.push_back("yy");
            field_names.push_back("ox");
            field_names.push_back("oy");
            config_value_editors_utf8(6, 2, 16, field_names);

            Transform2D basis = v;
            for (int i = 0; i < 6; i++) {

                value_editor[i]->set_text(StringUtils::num(basis.elements[i / 2][i % 2]));
            }

        } break;
        case VariantType::BASIS: {

            field_names.push_back("xx");
            field_names.push_back("xy");
            field_names.push_back("xz");
            field_names.push_back("yx");
            field_names.push_back("yy");
            field_names.push_back("yz");
            field_names.push_back("zx");
            field_names.push_back("zy");
            field_names.push_back("zz");
            config_value_editors_utf8(9, 3, 16, field_names);

            Basis basis = v;
            for (int i = 0; i < 9; i++) {

                value_editor[i]->set_text(StringUtils::num(basis.elements[i / 3][i % 3]));
            }

        } break;
        case VariantType::TRANSFORM: {

            field_names.push_back("xx");
            field_names.push_back("xy");
            field_names.push_back("xz");
            field_names.push_back("xo");
            field_names.push_back("yx");
            field_names.push_back("yy");
            field_names.push_back("yz");
            field_names.push_back("yo");
            field_names.push_back("zx");
            field_names.push_back("zy");
            field_names.push_back("zz");
            field_names.push_back("zo");
            config_value_editors_utf8(12, 4, 16, field_names);

            Transform tr = v;
            for (int i = 0; i < 9; i++) {

                value_editor[i / 3 * 4 + i % 3]->set_text(StringUtils::num(tr.basis.elements[i / 3][i % 3]));
            }

            value_editor[3]->set_text(StringUtils::num(tr.origin.x));
            value_editor[7]->set_text(StringUtils::num(tr.origin.y));
            value_editor[11]->set_text(StringUtils::num(tr.origin.z));

        } break;
        case VariantType::COLOR: {

            if (!color_picker) {
                //late init for performance
                color_picker = memnew(ColorPicker);
                color_picker->set_deferred_mode(true);
                add_child(color_picker);
                color_picker->hide();
                color_picker->connect("color_changed", this, "_color_changed");

                // get default color picker mode from editor settings
                int default_color_mode = EDITOR_GET("interface/inspector/default_color_picker_mode");
                if (default_color_mode == 1)
                    color_picker->set_hsv_mode(true);
                else if (default_color_mode == 2)
                    color_picker->set_raw_mode(true);
            }

            color_picker->show();
            color_picker->set_edit_alpha(hint != PropertyHint::ColorNoAlpha);
            color_picker->set_pick_color(v);
            color_picker->set_focus_on_line_edit();

        } break;

        case VariantType::NODE_PATH: {

            FixedVector<StringName,3> names;
            names.emplace_back(TTR("Assign"));
            names.emplace_back(TTR("Clear"));

            if (owner && owner->is_class("Node") && v.get_type() == VariantType::NODE_PATH && object_cast<Node>(owner)->has_node(v))
                names.emplace_back(TTR("Select Node"));

            config_action_buttons(names);

        } break;
        case VariantType::OBJECT: {

            if (hint != PropertyHint::ResourceType)
                break;

            if (p_name == StringView("script") && hint_text == "Script" && object_cast<Node>(owner)) {
                menu->add_icon_item(get_icon("Script", "EditorIcons"), TTR("New Script"), OBJ_MENU_NEW_SCRIPT);
                menu->add_separator();
            } else if (!hint_text.empty()) {
                int idx = 0;

                Vector<EditorData::CustomType> custom_resources;

                if (EditorNode::get_editor_data().get_custom_types().contains("Resource")) {
                    custom_resources = EditorNode::get_editor_data().get_custom_types().at("Resource");
                }

                for (int i = 0; i <StringUtils::get_slice_count( hint_text,','); i++) {

                    StringName base(StringUtils::get_slice(hint_text,',', i));

                    HashSet<StringName> valid_inheritors;
                    valid_inheritors.insert(base);
                    Vector<StringName> inheritors;
                    ClassDB::get_inheriters_from_class(StringName(StringUtils::strip_edges(base)), &inheritors);

                    for (int j = 0; j < custom_resources.size(); j++) {
                        inheritors.push_back(custom_resources[j].name);
                    }

                    for(const StringName &E : inheritors)
                        valid_inheritors.insert(E);

                    for (const StringName &t : valid_inheritors) {

                        bool is_custom_resource = false;
                        Ref<Texture> icon;
                        if (!custom_resources.empty()) {
                            for (int k = 0; k < custom_resources.size(); k++) {
                                if (custom_resources[k].name == t) {
                                    is_custom_resource = true;
                                    if (custom_resources[k].icon)
                                        icon = custom_resources[k].icon;
                                    break;
                                }
                            }
                        }

                        if (!is_custom_resource && !ClassDB::can_instance(t))
                            continue;

                        inheritors_array.push_back(t);

                        int id = TYPE_BASE_ID + idx;

                        if (not icon && has_icon(t, "EditorIcons")) {
                            icon = get_icon(t, "EditorIcons");
                        }
                        StringName newstr(FormatSN(TTR("New %s").asCString(), t.asCString()));
                        if (icon) {

                            menu->add_icon_item(icon, newstr, id);
                        } else {

                            menu->add_item(newstr, id);
                        }

                        idx++;
                    }
                }

                if (menu->get_item_count())
                    menu->add_separator();
            }

            menu->add_icon_item(get_icon("Load", "EditorIcons"), TTR("Load"), OBJ_MENU_LOAD);

            if (RES(v)) {

                menu->add_icon_item(get_icon("Edit", "EditorIcons"), TTR("Edit"), OBJ_MENU_EDIT);
                menu->add_icon_item(get_icon("Clear", "EditorIcons"), TTR("Clear"), OBJ_MENU_CLEAR);
                menu->add_icon_item(get_icon("Duplicate", "EditorIcons"), TTR("Make Unique"), OBJ_MENU_MAKE_UNIQUE);
                RES r(v);
                if (r && PathUtils::is_resource_file(r->get_path())) {
                    menu->add_separator();
                    menu->add_item(TTR("Show in FileSystem"), OBJ_MENU_SHOW_IN_FILE_SYSTEM);
                }
            }

            RES cb(EditorSettings::get_singleton()->get_resource_clipboard());
            bool paste_valid = false;
            if (cb) {
                if (hint_text.empty())
                    paste_valid = true;
                else
                    for (int i = 0; i < StringUtils::get_slice_count(hint_text,','); i++)
                        if (ClassDB::is_parent_class(cb->get_class_name(), StringName(StringUtils::get_slice(hint_text,',', i)))) {
                            paste_valid = true;
                            break;
                        }
            }

            if (RES(v) || paste_valid) {
                menu->add_separator();

                if (RES(v)) {

                    menu->add_item(TTR("Copy"), OBJ_MENU_COPY);
                }

                if (paste_valid) {

                    menu->add_item(TTR("Paste"), OBJ_MENU_PASTE);
                }
            }

            if (RES(v)) {

                Vector<Ref<EditorResourceConversionPlugin> > conversions = EditorNode::get_singleton()->find_resource_conversion_plugin(RES(v));
                if (!conversions.empty()) {
                    menu->add_separator();
                }
                for (int i = 0; i < conversions.size(); i++) {
                    StringName what = conversions[i]->converts_to();
                    Ref<Texture> icon;
                    if (has_icon(what, "EditorIcons")) {

                        icon = get_icon(what, "EditorIcons");
                    } else {

                        icon = get_icon(what, "Resource");
                    }

                    menu->add_icon_item(icon, FormatSN(TTR("Convert To %s").asCString(), what.asCString()), CONVERT_BASE_ID + i);
                }
            }

            menu->set_position(get_position());
            menu->popup();
            hide();
            updating = false;
            return false;

        } break;
        case VariantType::DICTIONARY: {

        } break;
        case VariantType::POOL_BYTE_ARRAY: {

        } break;
        case VariantType::POOL_INT_ARRAY: {

        } break;
        case VariantType::POOL_REAL_ARRAY: {

        } break;
        case VariantType::POOL_STRING_ARRAY: {

        } break;
        case VariantType::POOL_VECTOR3_ARRAY: {

        } break;
        case VariantType::POOL_COLOR_ARRAY: {

        } break;
        default: {
        }
    }

    updating = false;
    return true;
}

void CustomPropertyEditor::_file_selected(StringView p_file) {

    switch (type) {

        case VariantType::STRING: {

            if (hint == PropertyHint::File || hint == PropertyHint::Dir) {

                v = ProjectSettings::get_singleton()->localize_path(p_file);
                emit_signal("variant_changed");
                hide();
            }

            if (hint == PropertyHint::GlobalFile || hint == PropertyHint::GlobalDir) {

                v = p_file;
                emit_signal("variant_changed");
                hide();
            }

        } break;
        case VariantType::OBJECT: {

            StringName type = hint == PropertyHint::ResourceType ? StringName(hint_text) : StringName();
            HResource res=gResourceManager().load(ResourcePath(p_file));
            //RES res(ResourceLoader::load(p_file, type));
            if (not res) {
                error->set_text(TTR("Error loading file: Not a resource!"));
                error->popup_centered_minsize();
                break;
            }
            v = Variant(Ref<Resource>(res.get()));
            emit_signal("variant_changed");
            hide();
        } break;
        default: {
        }
    }
}

void CustomPropertyEditor::_type_create_selected(int p_idx) {

    if (type == VariantType::INT || type == VariantType::REAL) {

        float newval = 0;
        switch (p_idx) {

            case EASING_LINEAR: {

                newval = 1;
            } break;
            case EASING_EASE_IN: {

                newval = 2.0;
            } break;
            case EASING_EASE_OUT: {
                newval = 0.5;
            } break;
            case EASING_ZERO: {

                newval = 0;
            } break;
            case EASING_IN_OUT: {

                newval = -0.5;
            } break;
            case EASING_OUT_IN: {
                newval = -2.0;
            } break;
        }

        v = newval;
        emit_signal("variant_changed");
        easing_draw->update();

    } else if (type == VariantType::OBJECT) {

        ERR_FAIL_INDEX(p_idx, inheritors_array.size());

        StringName intype = inheritors_array[p_idx];

        Object *obj = ClassDB::instance(intype);

        if (!obj) {
            if (ScriptServer::is_global_class(intype)) {
                obj = EditorNode::get_editor_data().script_class_instance(intype);
            } else {
                obj = EditorNode::get_editor_data().instance_custom_type(intype, "Resource");
            }
        }

        ERR_FAIL_COND(!obj);

        Resource *res = object_cast<Resource>(obj);
        ERR_FAIL_COND(!res);

        v = Variant(Ref<Resource>(res));
        emit_signal("variant_changed");
        hide();
    }
}

void CustomPropertyEditor::_color_changed(const Color &p_color) {

    v = p_color;
    emit_signal("variant_changed");
}

void CustomPropertyEditor::_node_path_selected(NodePath p_path) {

    if (picking_viewport) {

        Node *to_node = get_node(p_path);
        if (!object_cast<Viewport>(to_node)) {
            EditorNode::get_singleton()->show_warning(TTR("Selected node is not a Viewport!"));
            return;
        }

        Ref<ViewportTexture> vt(make_ref_counted<ViewportTexture>());
        vt->set_viewport_path_in_scene(get_tree()->get_edited_scene_root()->get_path_to(to_node));
        vt->setup_local_to_scene();
        v = vt;
        emit_signal("variant_changed");
        return;
    }

    if (hint == PropertyHint::NodePathToEditedNode && !hint_text.empty()) {

        Node *node = get_node(NodePath(hint_text));
        if (node) {

            Node *tonode = node->get_node(p_path);
            if (tonode) {
                p_path = node->get_path_to(tonode);
            }
        }

    } else if (owner) {

        Node *node = nullptr;

        if (owner->is_class("Node"))
            node = object_cast<Node>(owner);
        else if (owner->is_class("ArrayPropertyEdit"))
            node = object_cast<ArrayPropertyEdit>(owner)->get_node();
        else if (owner->is_class("DictionaryPropertyEdit"))
            node = object_cast<DictionaryPropertyEdit>(owner)->get_node();
        if (!node) {
            v = p_path;
            emit_signal("variant_changed");
            call_deferred("hide"); //to not mess with dialogs
            return;
        }

        Node *tonode = node->get_node(p_path);
        if (tonode) {
            p_path = node->get_path_to(tonode);
        }
    }

    v = p_path;
    emit_signal("variant_changed");
    call_deferred("hide"); //to not mess with dialogs
}

void CustomPropertyEditor::_action_pressed(int p_which) {

    if (updating)
        return;

    switch (type) {
        case VariantType::BOOL: {
            v = checks20[0]->is_pressed();
            emit_signal("variant_changed");
        } break;
        case VariantType::INT: {

            if (hint == PropertyHint::Layers2DPhysics || hint == PropertyHint::Layers2DRenderer || hint == PropertyHint::Layers3DPhysics || hint == PropertyHint::Layers3DRenderer) {

                uint32_t f = v;
                if (checks20[p_which]->is_pressed())
                    f |= 1 << p_which;
                else
                    f &= ~(1 << p_which);

                v = f;
                emit_signal("variant_changed");
            }

        } break;
        case VariantType::STRING: {

            if (hint == PropertyHint::MultilineText) {

                hide();

            } else if (hint == PropertyHint::File || hint == PropertyHint::GlobalFile) {
                if (p_which == 0) {

                    if (hint == PropertyHint::File)
                        file->set_access(EditorFileDialog::ACCESS_RESOURCES);
                    else
                        file->set_access(EditorFileDialog::ACCESS_FILESYSTEM);

                    file->set_mode(EditorFileDialog::MODE_OPEN_FILE);
                    file->clear_filters();

                    file->clear_filters();

                    if (!hint_text.empty()) {
                        Vector<StringView> extensions = StringUtils::split(hint_text,',');
                        for (int i = 0; i < extensions.size(); i++) {

                            String filter(extensions[i]);
                            if (StringUtils::begins_with(filter,"."))
                                filter = String("*") + extensions[i];
                            else if (!StringUtils::begins_with(filter,"*"))
                                filter = String("*.") + extensions[i];

                            file->add_filter(filter + " ; " + StringUtils::to_upper(extensions[i]));
                        }
                    }
                    file->popup_centered_ratio();
                } else {

                    v = "";
                    emit_signal("variant_changed");
                    hide();
                }

            } else if (hint == PropertyHint::Dir || hint == PropertyHint::GlobalDir) {

                if (p_which == 0) {

                    if (hint == PropertyHint::Dir)
                        file->set_access(EditorFileDialog::ACCESS_RESOURCES);
                    else
                        file->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
                    file->set_mode(EditorFileDialog::MODE_OPEN_DIR);
                    file->clear_filters();
                    file->popup_centered_ratio();
                } else {

                    v = "";
                    emit_signal("variant_changed");
                    hide();
                }
            }

        } break;
        case VariantType::NODE_PATH: {

            if (p_which == 0) {

                picking_viewport = false;
                scene_tree->set_title(TTR("Pick a Node"));
                scene_tree->popup_centered_ratio();

            } else if (p_which == 1) {

                v = NodePath();
                emit_signal("variant_changed");
                hide();
            } else if (p_which == 2) {

                if (owner->is_class("Node") && v.get_type() == VariantType::NODE_PATH && object_cast<Node>(owner)->has_node(v)) {

                    Node *target_node = object_cast<Node>(owner)->get_node(v);
                    EditorNode::get_singleton()->get_editor_selection()->clear();
                    EditorNode::get_singleton()->get_scene_tree_dock()->set_selected(target_node);
                }

                hide();
            }

        } break;
        case VariantType::OBJECT: {

            if (p_which == 0) {

                ERR_FAIL_COND(inheritors_array.empty());

                StringName intype = inheritors_array[0];

                if (hint == PropertyHint::ResourceType) {

                    Object *obj = ClassDB::instance(intype);

                    if (!obj) {
                        if (ScriptServer::is_global_class(intype)) {
                            obj = EditorNode::get_editor_data().script_class_instance(intype);
                        } else {
                            obj = EditorNode::get_editor_data().instance_custom_type(intype, "Resource");
                        }
                    }

                    ERR_BREAK(!obj);
                    Resource *res = object_cast<Resource>(obj);
                    ERR_BREAK(!res);

                    v = Variant(Ref<Resource>(res));
                    emit_signal("variant_changed");
                    hide();
                }
            } else if (p_which == 1) {

                file->set_access(EditorFileDialog::ACCESS_RESOURCES);
                file->set_mode(EditorFileDialog::MODE_OPEN_FILE);
                Vector<String> extensions;
                StringName type = hint == PropertyHint::ResourceType ? StringName(hint_text) : StringName();

                ResourceLoader::get_recognized_extensions_for_type(type, extensions);
                file->clear_filters();
                for (const String &E : extensions) {

                    file->add_filter("*." + E + " ; " + StringUtils::to_upper(E));
                }

                file->popup_centered_ratio();

            } else if (p_which == 2) {

                RefPtr RefPtr = v;

                if (!RefPtr.is_null()) {

                    emit_signal("resource_edit_request");
                    hide();
                }

            } else if (p_which == 3) {

                v = Variant();
                emit_signal("variant_changed");
                hide();
            } else if (p_which == 4) {

                Ref<Resource> res_orig(refFromVariant<Resource>(v));
                if (not res_orig)
                    return;

                Vector<PropertyInfo> property_list;
                res_orig->get_property_list(&property_list);
                Vector<Pair<StringName, Variant> > propvalues;

                for (PropertyInfo &pi :property_list) {

                    Pair<StringName, Variant> p;

                    if (pi.usage & PROPERTY_USAGE_STORAGE) {

                        p.first = pi.name;
                        p.second = res_orig->get(pi.name);
                    }

                    propvalues.push_back(p);
                }

                Ref<Resource> res(ObjectNS::cast_to<Resource>(ClassDB::instance(res_orig->get_class_name())));

                ERR_FAIL_COND(not res);

                for (Pair<StringName, Variant> &p : propvalues) {
                    res->set(p.first, p.second);
                }

                v = Variant(res);
                emit_signal("variant_changed");
                hide();
            }

        } break;

        default: {
        };
    }
}

void CustomPropertyEditor::_drag_easing(const Ref<InputEvent> &p_ev) {

    Ref<InputEventMouseMotion> mm = dynamic_ref_cast<InputEventMouseMotion>(p_ev);

    if (mm && mm->get_button_mask() & BUTTON_MASK_LEFT) {

        float rel = mm->get_relative().x;
        if (rel == 0)
            return;

        bool flip = hint_text == "attenuation";

        if (flip)
            rel = -rel;

        float val = v;
        if (val == 0)
            return;
        bool sg = val < 0;
        val = Math::absf(val);

        val = Math::log(val) / Math::log((float)2.0);
        //logspace
        val += rel * 0.05f;

        val = Math::pow(2.0f, val);
        if (sg)
            val = -val;

        v = val;
        easing_draw->update();
        emit_signal("variant_changed");
    }
}

void CustomPropertyEditor::_draw_easing() {

    RID ci = easing_draw->get_canvas_item();

    Size2 s = easing_draw->get_size();
    Rect2 r(Point2(), s);
    r = r.grow(3);
    get_stylebox("normal", "LineEdit")->draw(ci, r);

    int points = 48;

    float prev = 1.0;
    float exp = v;
    bool flip = hint_text == "attenuation";

    Ref<Font> f = get_font("font", "Label");
    Color color = get_color("font_color", "Label");

    for (int i = 1; i <= points; i++) {

        float ifl = i / float(points);
        float iflp = (i - 1) / float(points);

        float h = 1.0f - Math::ease(ifl, exp);

        if (flip) {
            ifl = 1.0f - ifl;
            iflp = 1.0f - iflp;
        }

        VisualServer::get_singleton()->canvas_item_add_line(ci, Point2(iflp * s.width, prev * s.height), Point2(ifl * s.width, h * s.height), color);
        prev = h;
    }

    f->draw_ui_string(ci, Point2(10, 10 + f->get_ascent()), UIString::number(exp,'g',2), color);
}

void CustomPropertyEditor::_text_edit_changed() {

    v = text_edit->get_text_utf8();
    emit_signal("variant_changed");
}

void CustomPropertyEditor::_create_dialog_callback() {

    v = create_dialog->get_selected_type();
    emit_signal("variant_changed");
}

void CustomPropertyEditor::_create_selected_property(StringView p_prop) {

    v = p_prop;
    emit_signal("variant_changed");
}

void CustomPropertyEditor::_modified(StringView p_string) {

    if (updating)
        return;

    updating = true;
    switch (type) {
        case VariantType::INT: {
            String text = value_editor[0]->get_text();
            Ref<Expression> expr(make_ref_counted<Expression>());
            Error err = expr->parse(text);
            if (err != OK) {
                v = StringUtils::to_int(value_editor[0]->get_text());
                return;
            } else {
                v = expr->execute(Array(), nullptr, false);
            }
            emit_signal("variant_changed");

        } break;
        case VariantType::REAL: {

            if (hint != PropertyHint::ExpEasing) {
                String text = value_editor[0]->get_text();
                v = _parse_real_expression(text);
                emit_signal("variant_changed");
            }

        } break;
        case VariantType::STRING: {

            v = value_editor[0]->get_text();
            emit_signal("variant_changed");
        } break;
        case VariantType::VECTOR2: {

            Vector2 vec;
            vec.x = _parse_real_expression(value_editor[0]->get_text());
            vec.y = _parse_real_expression(value_editor[1]->get_text());
            v = vec;
            _emit_changed_whole_or_field();

        } break;
        case VariantType::RECT2: {

            Rect2 r2;

            r2.position.x = _parse_real_expression(value_editor[0]->get_text());
            r2.position.y = _parse_real_expression(value_editor[1]->get_text());
            r2.size.x = _parse_real_expression(value_editor[2]->get_text());
            r2.size.y = _parse_real_expression(value_editor[3]->get_text());
            v = r2;
            _emit_changed_whole_or_field();

        } break;

        case VariantType::VECTOR3: {

            Vector3 vec;
            vec.x = _parse_real_expression(value_editor[0]->get_text());
            vec.y = _parse_real_expression(value_editor[1]->get_text());
            vec.z = _parse_real_expression(value_editor[2]->get_text());
            v = vec;
            _emit_changed_whole_or_field();

        } break;
        case VariantType::PLANE: {

            Plane pl;
            pl.normal.x = _parse_real_expression(value_editor[0]->get_text());
            pl.normal.y = _parse_real_expression(value_editor[1]->get_text());
            pl.normal.z = _parse_real_expression(value_editor[2]->get_text());
            pl.d = _parse_real_expression(value_editor[3]->get_text());
            v = pl;
            _emit_changed_whole_or_field();

        } break;
        case VariantType::QUAT: {

            Quat q;
            q.x = _parse_real_expression(value_editor[0]->get_text());
            q.y = _parse_real_expression(value_editor[1]->get_text());
            q.z = _parse_real_expression(value_editor[2]->get_text());
            q.w = _parse_real_expression(value_editor[3]->get_text());
            v = q;
            _emit_changed_whole_or_field();

        } break;
        case VariantType::AABB: {

            Vector3 pos;
            Vector3 size;

            pos.x = _parse_real_expression(value_editor[0]->get_text());
            pos.y = _parse_real_expression(value_editor[1]->get_text());
            pos.z = _parse_real_expression(value_editor[2]->get_text());
            size.x = _parse_real_expression(value_editor[3]->get_text());
            size.y = _parse_real_expression(value_editor[4]->get_text());
            size.z = _parse_real_expression(value_editor[5]->get_text());
            v = AABB(pos, size);
            _emit_changed_whole_or_field();

        } break;
        case VariantType::TRANSFORM2D: {

            Transform2D m;
            for (int i = 0; i < 6; i++) {
                m.elements[i / 2][i % 2] = _parse_real_expression(value_editor[i]->get_text());
            }

            v = m;
            _emit_changed_whole_or_field();

        } break;
        case VariantType::BASIS: {

            Basis m;
            for (int i = 0; i < 9; i++) {
                m.elements[i / 3][i % 3] = _parse_real_expression(value_editor[i]->get_text());
            }

            v = m;
            _emit_changed_whole_or_field();

        } break;
        case VariantType::TRANSFORM: {

            Basis basis;
            for (int i = 0; i < 9; i++) {
                basis.elements[i / 3][i % 3] = _parse_real_expression(value_editor[i / 3 * 4 + i % 3]->get_text());
            }

            Vector3 origin;

            origin.x = _parse_real_expression(value_editor[3]->get_text());
            origin.y = _parse_real_expression(value_editor[7]->get_text());
            origin.z = _parse_real_expression(value_editor[11]->get_text());

            v = Transform(basis, origin);
            _emit_changed_whole_or_field();

        } break;
        case VariantType::COLOR: {

        } break;

        case VariantType::NODE_PATH: {

            v = NodePath(value_editor[0]->get_text());
            emit_signal("variant_changed");
        } break;
        case VariantType::DICTIONARY: {

        } break;
        case VariantType::POOL_BYTE_ARRAY: {

        } break;
        case VariantType::POOL_INT_ARRAY: {

        } break;
        case VariantType::POOL_REAL_ARRAY: {

        } break;
        case VariantType::POOL_STRING_ARRAY: {

        } break;
        case VariantType::POOL_VECTOR3_ARRAY: {

        } break;
        case VariantType::POOL_COLOR_ARRAY: {

        } break;
        default: {
        }
    }

    updating = false;
}

real_t CustomPropertyEditor::_parse_real_expression(StringView text) {
    Ref<Expression> expr(make_ref_counted<Expression>());
    Error err = expr->parse(text);
    real_t out;
    if (err != OK) {
        out = value_editor[0]->get_text_ui().toFloat();
    } else {
        out = expr->execute(Array(), nullptr, false);
    }
    return out;
}

void CustomPropertyEditor::_emit_changed_whole_or_field() {

    if (!Input::get_singleton()->is_key_pressed(KEY_SHIFT)) {
        emit_signal("variant_changed");
    } else {
        emit_signal("variant_field_changed", field_names[focused_value_editor]);
    }
}

void CustomPropertyEditor::_range_modified(double p_value) {
    v = p_value;
    emit_signal("variant_changed");
}

void CustomPropertyEditor::_focus_enter() {
    switch (type) {
        case VariantType::REAL:
        case VariantType::STRING:
        case VariantType::VECTOR2:
        case VariantType::RECT2:
        case VariantType::VECTOR3:
        case VariantType::PLANE:
        case VariantType::QUAT:
        case VariantType::AABB:
        case VariantType::TRANSFORM2D:
        case VariantType::BASIS:
        case VariantType::TRANSFORM: {
            for (int i = 0; i < MAX_VALUE_EDITORS; ++i) {
                if (value_editor[i]->has_focus()) {
                    focused_value_editor = i;
                    value_editor[i]->select_all();
                    break;
                }
            }
        } break;
        default: {
        }
    }
}

void CustomPropertyEditor::_focus_exit() {
    switch (type) {
        case VariantType::REAL:
        case VariantType::STRING:
        case VariantType::VECTOR2:
        case VariantType::RECT2:
        case VariantType::VECTOR3:
        case VariantType::PLANE:
        case VariantType::QUAT:
        case VariantType::AABB:
        case VariantType::TRANSFORM2D:
        case VariantType::BASIS:
        case VariantType::TRANSFORM: {
            for (auto & i : value_editor) {
                i->select(0, 0);
            }
        } break;
        default: {
        }
    }
}

void CustomPropertyEditor::config_action_buttons(Span<const StringName> p_strings) {

    Ref<StyleBox> sb = get_stylebox("panel");
    int margin_top = sb->get_margin(Margin::Top);
    int margin_left = sb->get_margin(Margin::Left);
    int margin_bottom = sb->get_margin(Margin::Bottom);
    int margin_right = sb->get_margin(Margin::Right);

    int max_width = 0;
    int height = 0;

    for (int i = 0; i < MAX_ACTION_BUTTONS; i++) {

        if (i < p_strings.size()) {

            action_buttons[i]->show();
            action_buttons[i]->set_text(p_strings[i]);

            Size2 btn_m_size = action_buttons[i]->get_minimum_size();
            if (btn_m_size.width > max_width)
                max_width = btn_m_size.width;

        } else {
            action_buttons[i]->hide();
        }
    }

    for (int i = 0; i < p_strings.size(); i++) {

        Size2 btn_m_size = action_buttons[i]->get_size();
        action_buttons[i]->set_position(Point2(0, height) + Point2(margin_left, margin_top));
        action_buttons[i]->set_size(Size2(max_width, btn_m_size.height));

        height += btn_m_size.height;
    }
    set_size(Size2(max_width, height) + Size2(margin_left + margin_right, margin_top + margin_bottom));
}

void CustomPropertyEditor::config_value_editors(int p_amount, int p_columns, int p_label_w, const Vector<StringName> &p_strings) {

    int cell_width = 95;
    int cell_height = 25;
    int cell_margin = 5;
    int hor_spacing = 5; // Spacing between labels and their values

    int rows = (p_amount - 1) / p_columns + 1;

    set_size(Size2(cell_margin + p_label_w + (cell_width + cell_margin + p_label_w) * p_columns, cell_margin + (cell_height + cell_margin) * rows) * EDSCALE);

    for (int i = 0; i < MAX_VALUE_EDITORS; i++) {

        int c = i % p_columns;
        int r = i / p_columns;

        if (i < p_amount) {
            value_editor[i]->show();
            value_label[i]->show();
            value_label[i]->set_text(i < p_strings.size() ? p_strings[i] : StringName(""));
            value_editor[i]->set_position(Point2(cell_margin + p_label_w + hor_spacing + (cell_width + cell_margin + p_label_w + hor_spacing) * c, cell_margin + (cell_height + cell_margin) * r) * EDSCALE);
            value_editor[i]->set_size(Size2(cell_width, cell_height));
            value_label[i]->set_position(Point2(cell_margin + (cell_width + cell_margin + p_label_w + hor_spacing) * c, cell_margin + (cell_height + cell_margin) * r) * EDSCALE);
            value_editor[i]->set_editable(!read_only);
        } else {
            value_editor[i]->hide();
            value_label[i]->hide();
        }
    }
}
void CustomPropertyEditor::config_value_editors_utf8(int p_amount, int p_columns, int p_label_w, const Vector<StringView> &p_strings) {

    int cell_width = 95;
    int cell_height = 25;
    int cell_margin = 5;
    int hor_spacing = 5; // Spacing between labels and their values

    int rows = (p_amount - 1) / p_columns + 1;

    set_size(Size2(cell_margin + p_label_w + (cell_width + cell_margin + p_label_w) * p_columns, cell_margin + (cell_height + cell_margin) * rows) * EDSCALE);

    for (size_t i = 0; i < MAX_VALUE_EDITORS; i++) {

        size_t c = i % p_columns;
        size_t r = i / p_columns;

        if (i < p_amount) {
            value_editor[i]->show();
            value_label[i]->show();
            value_label[i]->set_text(i < p_strings.size() ? StringName(p_strings[i]) : "");
            value_editor[i]->set_position(Point2(cell_margin + p_label_w + hor_spacing + (cell_width + cell_margin + p_label_w + hor_spacing) * c, cell_margin + (cell_height + cell_margin) * r) * EDSCALE);
            value_editor[i]->set_size(Size2(cell_width, cell_height));
            value_label[i]->set_position(Point2(cell_margin + (cell_width + cell_margin + p_label_w + hor_spacing) * c, cell_margin + (cell_height + cell_margin) * r) * EDSCALE);
            value_editor[i]->set_editable(!read_only);
        } else {
            value_editor[i]->hide();
            value_label[i]->hide();
        }
    }
}
void CustomPropertyEditor::_bind_methods() {

    MethodBinder::bind_method("_focus_enter", &CustomPropertyEditor::_focus_enter);
    MethodBinder::bind_method("_focus_exit", &CustomPropertyEditor::_focus_exit);
    MethodBinder::bind_method("_modified", &CustomPropertyEditor::_modified);
    MethodBinder::bind_method("_range_modified", &CustomPropertyEditor::_range_modified);
    MethodBinder::bind_method("_action_pressed", &CustomPropertyEditor::_action_pressed);
    MethodBinder::bind_method("_file_selected", &CustomPropertyEditor::_file_selected);
    MethodBinder::bind_method("_type_create_selected", &CustomPropertyEditor::_type_create_selected);
    MethodBinder::bind_method("_node_path_selected", &CustomPropertyEditor::_node_path_selected);
    MethodBinder::bind_method("_color_changed", &CustomPropertyEditor::_color_changed);
    MethodBinder::bind_method("_draw_easing", &CustomPropertyEditor::_draw_easing);
    MethodBinder::bind_method("_drag_easing", &CustomPropertyEditor::_drag_easing);
    MethodBinder::bind_method("_text_edit_changed", &CustomPropertyEditor::_text_edit_changed);
    MethodBinder::bind_method("_menu_option", &CustomPropertyEditor::_menu_option);
    MethodBinder::bind_method("_create_dialog_callback", &CustomPropertyEditor::_create_dialog_callback);
    MethodBinder::bind_method("_create_selected_property", &CustomPropertyEditor::_create_selected_property);

    ADD_SIGNAL(MethodInfo("variant_changed"));
    ADD_SIGNAL(MethodInfo("variant_field_changed", PropertyInfo(VariantType::STRING, "field")));
    ADD_SIGNAL(MethodInfo("resource_edit_request"));
}

CustomPropertyEditor::CustomPropertyEditor() {

    read_only = false;
    updating = false;

    for (int i = 0; i < MAX_VALUE_EDITORS; i++) {

        value_editor[i] = memnew(LineEdit);
        add_child(value_editor[i]);
        value_label[i] = memnew(Label);
        add_child(value_label[i]);
        value_editor[i]->hide();
        value_label[i]->hide();
        value_editor[i]->connect("text_entered", this, "_modified");
        value_editor[i]->connect("focus_entered", this, "_focus_enter");
        value_editor[i]->connect("focus_exited", this, "_focus_exit");
    }
    focused_value_editor = -1;

    for (auto & i : scroll) {

        i = memnew(HScrollBar);
        i->hide();
        i->set_min(0);
        i->set_max(1.0);
        i->set_step(0.01);
        add_child(i);
    }

    checks20gc = memnew(GridContainer);
    add_child(checks20gc);
    checks20gc->set_columns(11);

    for (int i = 0; i < 20; i++) {
        if (i == 5 || i == 15) {
            Control *space = memnew(Control);
            space->set_custom_minimum_size(Size2(20, 0) * EDSCALE);
            checks20gc->add_child(space);
        }

        checks20[i] = memnew(CheckBox);
        checks20[i]->set_toggle_mode(true);
        checks20[i]->set_focus_mode(FOCUS_NONE);
        checks20gc->add_child(checks20[i]);
        checks20[i]->hide();
        checks20[i]->connect("pressed", this, "_action_pressed", make_binds(i));
        checks20[i]->set_tooltip(FormatSN(TTR("Bit %d, val %d.").asCString(), i, 1 << i));
    }

    text_edit = memnew(TextEdit);
    add_child(text_edit);
    text_edit->set_anchors_and_margins_preset(Control::PRESET_WIDE, Control::PRESET_MODE_MINSIZE, 5);
    text_edit->set_margin(Margin::Bottom, -30);

    text_edit->hide();
    text_edit->connect("text_changed", this, "_text_edit_changed");

    for (int i = 0; i < MAX_ACTION_BUTTONS; i++) {

        action_buttons[i] = memnew(Button);
        action_buttons[i]->hide();
        add_child(action_buttons[i]);
        action_buttons[i]->connect("pressed", this, "_action_pressed", {i});
        action_buttons[i]->set_flat(true);
    }

    color_picker = nullptr;

    set_as_toplevel(true);
    file = memnew(EditorFileDialog);
    add_child(file);
    file->hide();

    file->connect("file_selected", this, "_file_selected");
    file->connect("dir_selected", this, "_file_selected");

    error = memnew(ConfirmationDialog);
    error->set_title(TTR("Error!"));
    add_child(error);

    scene_tree = memnew(SceneTreeDialog);
    add_child(scene_tree);
    scene_tree->connect("selected", this, "_node_path_selected");
    scene_tree->get_scene_tree()->set_show_enabled_subscene(true);

    texture_preview = memnew(TextureRect);
    add_child(texture_preview);
    texture_preview->hide();

    easing_draw = memnew(Control);
    add_child(easing_draw);
    easing_draw->hide();
    easing_draw->connect("draw", this, "_draw_easing");
    easing_draw->connect("gui_input", this, "_drag_easing");
    easing_draw->set_default_cursor_shape(Control::CURSOR_MOVE);

    type_button = memnew(MenuButton);
    add_child(type_button);
    type_button->hide();
    type_button->get_popup()->connect("id_pressed", this, "_type_create_selected");

    menu = memnew(PopupMenu);
    menu->set_pass_on_modal_close_click(false);
    add_child(menu);
    menu->connect("id_pressed", this, "_menu_option");

    evaluator = nullptr;

    spinbox = memnew(SpinBox);
    add_child(spinbox);
    spinbox->set_anchors_and_margins_preset(Control::PRESET_WIDE, Control::PRESET_MODE_MINSIZE, 5);
    spinbox->connect("value_changed", this, "_range_modified");

    slider = memnew(HSlider);
    add_child(slider);
    slider->set_anchors_and_margins_preset(Control::PRESET_WIDE, Control::PRESET_MODE_MINSIZE, 5);
    slider->connect("value_changed", this, "_range_modified");

    create_dialog = nullptr;
    property_select = nullptr;
}
