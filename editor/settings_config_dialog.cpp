/*************************************************************************/
/*  settings_config_dialog.cpp                                           */
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

#include "editor_log.h"
#include "settings_config_dialog.h"
#include "script_editor_debugger.h"

#include "core/callable_method_pointer.h"
#include "core/method_bind.h"
#include "core/os/keyboard.h"
#include "core/project_settings.h"
#include "core/string_formatter.h"
#include "core/translation_helpers.h"
#include "core/string_utils.h"
#include "editor_file_system.h"
#include "editor_node.h"
#include "editor/editor_scale.h"
#include "editor_settings.h"
#include "scene/gui/margin_container.h"
#include "scene/resources/style_box.h"

IMPL_GDCLASS(EditorSettingsDialog)

void EditorSettingsDialog::ok_pressed() {

    if (!EditorSettings::get_singleton())
        return;

    _settings_save();
    timer->stop();
}

void EditorSettingsDialog::_settings_changed() {

    timer->start();
}

void EditorSettingsDialog::_settings_property_edited(const String &p_name) {

    String full_name = inspector->get_full_item_path(p_name);

    if (full_name == "interface/theme/accent_color" || full_name == "interface/theme/base_color" || full_name == "interface/theme/contrast") {
        EditorSettings::get_singleton()->set_manually("interface/theme/preset", "Custom"); // set preset to Custom
    } else if (StringUtils::begins_with(full_name,"text_editor/highlighting")) {
        EditorSettings::get_singleton()->set_manually("text_editor/theme/color_theme", "Custom");
    }
}

void EditorSettingsDialog::_settings_save() {

    EditorSettings::get_singleton()->notify_changes();
    EditorSettings::get_singleton()->save();
}

void EditorSettingsDialog::cancel_pressed() {

    if (!EditorSettings::get_singleton())
        return;

    EditorSettings::get_singleton()->notify_changes();
}

void EditorSettingsDialog::popup_edit_settings() {

    if (!EditorSettings::get_singleton())
        return;

    EditorSettings::get_singleton()->list_text_editor_themes(); // make sure we have an up to date list of themes

    inspector->edit(EditorSettings::get_singleton());
    inspector->get_inspector()->update_tree();

    search_box->select_all();
    search_box->grab_focus();

    _update_shortcuts();
    set_process_unhandled_input(true);

    // Restore valid window bounds or pop up at default size.
    Rect2 saved_size = EditorSettings::get_singleton()->get_project_metadata("dialog_bounds", "editor_settings", Rect2()).as<Rect2>();
    if (saved_size != Rect2()) {
        popup(saved_size);
    } else {
        popup_centered_clamped(Size2(900, 700) * EDSCALE, 0.8f);
    }

    _focus_current_search_box();
}

void EditorSettingsDialog::_filter_shortcuts(StringView p_filter) {
    shortcut_filter = p_filter;
    _update_shortcuts();
}

void EditorSettingsDialog::_undo_redo_callback(void *p_self, StringView p_name) {
    EditorNode::get_log()->add_message_utf8(p_name, EditorLog::MSG_TYPE_EDITOR);
}

void EditorSettingsDialog::_notification(int p_what) {

    switch (p_what) {
        case NOTIFICATION_READY: {
            ScriptEditorDebugger *sed = ScriptEditor::get_singleton()->get_debugger();
            undo_redo->set_method_notify_callback(sed->_method_changeds, sed);
            undo_redo->set_property_notify_callback(sed->_property_changeds, sed);
            undo_redo->set_commit_notify_callback(_undo_redo_callback, this);
        } break;
        case NOTIFICATION_ENTER_TREE: {
            _update_icons();
        } break;
        case NOTIFICATION_POPUP_HIDE: {
            EditorSettings::get_singleton()->set_project_metadata("dialog_bounds", "editor_settings", get_rect());
            set_process_unhandled_input(false);
        } break;
        case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
            _update_icons();
            // Update theme colors.
            inspector->update_category_list();
            _update_shortcuts();
        } break;
    }
}

void EditorSettingsDialog::_unhandled_input(const Ref<InputEvent> &p_event) {

    const Ref<InputEventKey> k(dynamic_ref_cast<InputEventKey>(p_event));

    if (k && is_window_modal_on_top() && k->is_pressed()) {
        bool handled = false;

        if (ED_IS_SHORTCUT("editor/undo", p_event)) {
            String action(undo_redo->get_current_action_name());
            if (!action.empty())
                EditorNode::get_log()->add_message_utf8("Undo: " + action, EditorLog::MSG_TYPE_EDITOR);
            undo_redo->undo();
            handled = true;
        }
        if (ED_IS_SHORTCUT("editor/redo", p_event)) {
            undo_redo->redo();
            String action(undo_redo->get_current_action_name());
            if (!action.empty())
                EditorNode::get_log()->add_message_utf8("Redo: " + action, EditorLog::MSG_TYPE_EDITOR);
            handled = true;
        }
        if (k->get_keycode_with_modifiers() == (KEY_MASK_CMD | KEY_F)) {
            _focus_current_search_box();
            handled = true;
        }
        if (handled) {
            accept_event();
        }
    }
}

void EditorSettingsDialog::_update_icons() {

    search_box->set_right_icon(get_icon("Search", "EditorIcons"));
    search_box->set_clear_button_enabled(true);
    shortcut_search_box->set_right_icon(get_icon("Search", "EditorIcons"));
    shortcut_search_box->set_clear_button_enabled(true);

    restart_close_button->set_button_icon(get_icon("Close", "EditorIcons"));
    restart_container->add_style_override("panel", get_stylebox("bg", "Tree"));
    restart_icon->set_texture(get_icon("StatusWarning", "EditorIcons"));
    restart_label->add_color_override("font_color", get_color("warning_color", "Editor"));
}

void EditorSettingsDialog::_update_shortcuts() {

    Map<String, bool> collapsed;

    if (shortcuts->get_root() && shortcuts->get_root()->get_children()) {
        for (TreeItem *item = shortcuts->get_root()->get_children(); item; item = item->get_next()) {
            collapsed[item->get_text(0)] = item->is_collapsed();
        }
    }

    shortcuts->clear();

    Vector<String> slist;
    EditorSettings::get_singleton()->get_shortcut_list(&slist);
    TreeItem *root = shortcuts->create_item();

    Map<String, TreeItem *> sections;

    for (const String &E : slist) {

        Ref<ShortCut> sc = EditorSettings::get_singleton()->get_shortcut(E);
        if (!sc->has_meta("original"))
            continue;

        Ref<InputEvent> original(sc->get_meta("original"));

        String section_name(StringUtils::get_slice(E,"/", 0));

        TreeItem *section;

        if (sections.contains(section_name)) {
            section = sections[section_name];
        } else {
            section = shortcuts->create_item(root);

            String item_name = StringUtils::capitalize(section_name);
            section->set_text_utf8(0, item_name);

            if (collapsed.contains(item_name)) {
                section->set_collapsed(collapsed[item_name]);
            }

            sections[section_name] = section;
            section->set_custom_bg_color(0, get_color("prop_subsection", "Editor"));
            section->set_custom_bg_color(1, get_color("prop_subsection", "Editor"));
        }
        // Don't match unassigned shortcuts when searching for assigned keys in search results.
        // This prevents all unassigned shortcuts from appearing when searching a string like "no".

        if (StringUtils::is_subsequence_of(shortcut_filter, sc->get_name(), StringUtils::CaseInsensitive) ||
                ( sc->get_as_text() != "None" && StringUtils::is_subsequence_of(shortcut_filter, sc->get_as_text(), StringUtils::CaseInsensitive))) {
            TreeItem *item = shortcuts->create_item(section);

            item->set_text_utf8(0, sc->get_name());
            item->set_text_utf8(1, sc->get_as_text());
            if (!sc->is_shortcut(original) && !(not sc->get_shortcut() && not original)) {
                item->add_button(1, get_icon("Reload", "EditorIcons"), 2);
            }
            if (sc->get_as_text() == "None") {
                // Fade out unassigned shortcut labels for easier visual grepping.
                item->set_custom_color(1, get_color("font_color", "Label") * Color(1, 1, 1, 0.5));
            }
            item->add_button(1, get_icon("Edit", "EditorIcons"), 0);
            item->add_button(1, get_icon("Close", "EditorIcons"), 1);
            item->set_tooltip(0, StringName(E));
            item->set_metadata(0, E);
        }
    }

    // remove sections with no shortcuts
    for (eastl::pair<const String,TreeItem *> &E : sections) {
        TreeItem *section = E.second;
        if (section->get_children() == nullptr) {
            root->remove_child(section);
        }
    }
}

void EditorSettingsDialog::_shortcut_button_pressed(Object *p_item, int p_column, int p_idx) {

    TreeItem *ti = object_cast<TreeItem>(p_item);
    ERR_FAIL_COND(!ti);

    String item = ti->get_metadata(0).as<String>();
    Ref<ShortCut> sc = EditorSettings::get_singleton()->get_shortcut(item);

    if (p_idx == 0) {
        press_a_key_label->set_text(TTR("Press a Key..."));
        last_wait_for_key = Ref<InputEventKey>();
        press_a_key->popup_centered(Size2(250, 80) * EDSCALE);
        press_a_key->grab_focus();
        press_a_key->get_ok()->set_focus_mode(FOCUS_NONE);
        press_a_key->get_cancel()->set_focus_mode(FOCUS_NONE);
        shortcut_configured = item;

    } else if (p_idx == 1) { //erase
        if (not sc)
            return; //pointless, there is nothing

        undo_redo->create_action(TTR("Erase Shortcut"));
        undo_redo->add_do_method(sc.get(), "set_shortcut", Ref<InputEvent>());
        undo_redo->add_undo_method(sc.get(), "set_shortcut", sc->get_shortcut());
        undo_redo->add_do_method(this, "_update_shortcuts");
        undo_redo->add_undo_method(this, "_update_shortcuts");
        undo_redo->add_do_method(this, "_settings_changed");
        undo_redo->add_undo_method(this, "_settings_changed");
        undo_redo->commit_action();
    } else if (p_idx == 2) { //revert to original
        if (not sc)
            return; //pointless, there is nothing

        Ref<InputEvent> original(sc->get_meta("original"));

        undo_redo->create_action(TTR("Restore Shortcut"));
        undo_redo->add_do_method(sc.get(), "set_shortcut", original);
        undo_redo->add_undo_method(sc.get(), "set_shortcut", sc->get_shortcut());
        undo_redo->add_do_method(this, "_update_shortcuts");
        undo_redo->add_undo_method(this, "_update_shortcuts");
        undo_redo->add_do_method(this, "_settings_changed");
        undo_redo->add_undo_method(this, "_settings_changed");
        undo_redo->commit_action();
    }
}

void EditorSettingsDialog::_wait_for_key(const Ref<InputEvent> &p_event) {

    Ref<InputEventKey> k = dynamic_ref_cast<InputEventKey>(p_event);
    //TODO: SEGS: removed shortcut internationalization, was it really needed ?
    if (k && k->is_pressed() && k->get_keycode() != 0) {

        last_wait_for_key = k;
        const String str = keycode_get_string(k->get_keycode_with_modifiers());
        press_a_key_label->set_text(StringName(str));
        press_a_key->accept_event();
    }
}

void EditorSettingsDialog::_press_a_key_confirm() {

    if (not last_wait_for_key)
        return;

    Ref<InputEventKey> ie(make_ref_counted<InputEventKey>());
    ie->set_keycode(last_wait_for_key->get_keycode());
    ie->set_shift(last_wait_for_key->get_shift());
    ie->set_control(last_wait_for_key->get_control());
    ie->set_alt(last_wait_for_key->get_alt());
    ie->set_metakey(last_wait_for_key->get_metakey());

    Ref<ShortCut> sc = EditorSettings::get_singleton()->get_shortcut(shortcut_configured);

    undo_redo->create_action(TTR("Change Shortcut") + " '" + shortcut_configured + "'");
    undo_redo->add_do_method(sc.get(), "set_shortcut", ie);
    undo_redo->add_undo_method(sc.get(), "set_shortcut", sc->get_shortcut());
    undo_redo->add_do_method(this, "_update_shortcuts");
    undo_redo->add_undo_method(this, "_update_shortcuts");
    undo_redo->add_do_method(this, "_settings_changed");
    undo_redo->add_undo_method(this, "_settings_changed");
    undo_redo->commit_action();
}

void EditorSettingsDialog::_tabs_tab_changed(int p_tab) {

    _focus_current_search_box();
}

void EditorSettingsDialog::_focus_current_search_box() {

    Control *tab = tabs->get_current_tab_control();
    LineEdit *current_search_box = nullptr;
    if (tab == tab_general)
        current_search_box = search_box;
    else if (tab == tab_shortcuts)
        current_search_box = shortcut_search_box;

    if (current_search_box) {
        current_search_box->grab_focus();
        current_search_box->select_all();
    }
}

void EditorSettingsDialog::_editor_restart() {
    EditorNode::get_singleton()->save_all_scenes();
    EditorNode::get_singleton()->restart_editor();
}

void EditorSettingsDialog::_editor_restart_request() {
    restart_container->show();
}

void EditorSettingsDialog::_editor_restart_close() {
    restart_container->hide();
}

void EditorSettingsDialog::_bind_methods() {

    MethodBinder::bind_method(D_METHOD("_unhandled_input"), &EditorSettingsDialog::_unhandled_input);
    MethodBinder::bind_method(D_METHOD("_settings_save"), &EditorSettingsDialog::_settings_save);
    MethodBinder::bind_method(D_METHOD("_settings_changed"), &EditorSettingsDialog::_settings_changed);
    MethodBinder::bind_method(D_METHOD("_settings_property_edited"), &EditorSettingsDialog::_settings_property_edited);
    MethodBinder::bind_method(D_METHOD("_shortcut_button_pressed"), &EditorSettingsDialog::_shortcut_button_pressed);
    MethodBinder::bind_method(D_METHOD("_filter_shortcuts"), &EditorSettingsDialog::_filter_shortcuts);
    MethodBinder::bind_method(D_METHOD("_update_shortcuts"), &EditorSettingsDialog::_update_shortcuts);
    MethodBinder::bind_method(D_METHOD("_press_a_key_confirm"), &EditorSettingsDialog::_press_a_key_confirm);
    MethodBinder::bind_method(D_METHOD("_wait_for_key"), &EditorSettingsDialog::_wait_for_key);
    MethodBinder::bind_method(D_METHOD("_tabs_tab_changed"), &EditorSettingsDialog::_tabs_tab_changed);

    MethodBinder::bind_method(D_METHOD("_editor_restart_request"), &EditorSettingsDialog::_editor_restart_request);
    MethodBinder::bind_method(D_METHOD("_editor_restart"), &EditorSettingsDialog::_editor_restart);
    MethodBinder::bind_method(D_METHOD("_editor_restart_close"), &EditorSettingsDialog::_editor_restart_close);
}

EditorSettingsDialog::EditorSettingsDialog() {

    set_title(TTR("Editor Settings"));
    set_resizable(true);
    undo_redo = memnew(UndoRedo);

    tabs = memnew(TabContainer);
    tabs->set_tab_align(TabContainer::ALIGN_LEFT);
    tabs->connect("tab_changed",callable_mp(this, &ClassName::_tabs_tab_changed));
    add_child(tabs);

    // General Tab


    tab_general = memnew(VBoxContainer);
    tabs->add_child(tab_general);
    tab_general->set_name(TTR("General"));

    HBoxContainer *hbc = memnew(HBoxContainer);
    hbc->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    tab_general->add_child(hbc);

    search_box = memnew(LineEdit);
    search_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    hbc->add_child(search_box);

    inspector = memnew(SectionedInspector);
    inspector->get_inspector()->set_use_filter(true);
    inspector->register_search_box(search_box);
    inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    inspector->get_inspector()->set_undo_redo(undo_redo);
    tab_general->add_child(inspector);
    inspector->get_inspector()->connect("property_edited",callable_mp(this, &ClassName::_settings_property_edited));
    inspector->get_inspector()->connect("restart_requested",callable_mp(this, &ClassName::_editor_restart_request));

    restart_container = memnew(PanelContainer);
    tab_general->add_child(restart_container);
    HBoxContainer *restart_hb = memnew(HBoxContainer);
    restart_container->add_child(restart_hb);
    restart_icon = memnew(TextureRect);
    restart_icon->set_v_size_flags(SIZE_SHRINK_CENTER);
    restart_hb->add_child(restart_icon);
    restart_label = memnew(Label);
    restart_label->set_text(TTR("The editor must be restarted for changes to take effect."));
    restart_hb->add_child(restart_label);
    restart_hb->add_spacer();
    Button *restart_button = memnew(Button);
    restart_button->connect("pressed",callable_mp(this, &ClassName::_editor_restart));
    restart_hb->add_child(restart_button);
    restart_button->set_text(TTR("Save & Restart"));
    restart_close_button = memnew(ToolButton);
    restart_close_button->connect("pressed",callable_mp(this, &ClassName::_editor_restart_close));
    restart_hb->add_child(restart_close_button);
    restart_container->hide();

    // Shortcuts Tab

    tab_shortcuts = memnew(VBoxContainer);
    tabs->add_child(tab_shortcuts);
    tab_shortcuts->set_name(TTR("Shortcuts"));

    hbc = memnew(HBoxContainer);
    hbc->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    tab_shortcuts->add_child(hbc);

    shortcut_search_box = memnew(LineEdit);
    shortcut_search_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    hbc->add_child(shortcut_search_box);
    shortcut_search_box->connect("text_changed",callable_mp(this, &ClassName::_filter_shortcuts));

    shortcuts = memnew(Tree);
    tab_shortcuts->add_child(shortcuts, true);
    shortcuts->set_v_size_flags(SIZE_EXPAND_FILL);
    shortcuts->set_columns(2);
    shortcuts->set_hide_root(true);
    shortcuts->set_column_titles_visible(true);
    shortcuts->set_column_title(0, TTR("Name"));
    shortcuts->set_column_title(1, TTR("Binding"));
    shortcuts->connect("button_pressed",callable_mp(this, &ClassName::_shortcut_button_pressed));

    press_a_key = memnew(ConfirmationDialog);
    press_a_key->set_focus_mode(FOCUS_ALL);
    add_child(press_a_key);

    Label *l = memnew(Label);
    l->set_text(TTR("Press a Key..."));
    l->set_anchors_and_margins_preset(Control::PRESET_WIDE);
    l->set_align(Label::ALIGN_CENTER);
    l->set_margin(Margin::Top, 20);
    l->set_anchor_and_margin(Margin::Bottom, ANCHOR_BEGIN, 30);
    press_a_key_label = l;
    press_a_key->add_child(l);
    press_a_key->connect("gui_input",callable_mp(this, &ClassName::_wait_for_key));
    press_a_key->connect("confirmed",callable_mp(this, &ClassName::_press_a_key_confirm));

    set_hide_on_ok(true);

    timer = memnew(Timer);
    timer->set_wait_time(1.5);
    timer->connect("timeout",callable_mp(this, &ClassName::_settings_save));
    timer->set_one_shot(true);
    add_child(timer);
    EditorSettings::get_singleton()->connect("settings_changed",callable_mp(this, &ClassName::_settings_changed));
    get_ok()->set_text(TTR("Close"));

    updating = false;
}

EditorSettingsDialog::~EditorSettingsDialog() {
    memdelete(undo_redo);
}
