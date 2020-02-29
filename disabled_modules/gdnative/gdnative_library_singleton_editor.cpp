/*************************************************************************/
/*  gdnative_library_singleton_editor.cpp                                */
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

#ifdef TOOLS_ENABLED
#include "gdnative_library_singleton_editor.h"

#include "gdnative.h"
#include "core/method_bind.h"
#include "core/project_settings.h"
#include "core/translation_helpers.h"
#include "core/string.h"

#include "editor/editor_node.h"
IMPL_GDCLASS(GDNativeLibrarySingletonEditor)

Set<String> GDNativeLibrarySingletonEditor::_find_singletons_recursive(EditorFileSystemDirectory *p_dir) {

    Set<String> file_paths;

    // check children

    for (int i = 0; i < p_dir->get_file_count(); i++) {
        String file_name = p_dir->get_file(i);
        String file_type = p_dir->get_file_type(i);

        if (file_type != "GDNativeLibrary") {
            continue;
        }

        Ref<GDNativeLibrary> lib = ResourceLoader::load<GDNativeLibrary>(p_dir->get_file_path(i)));
        if (lib && lib->is_singleton()) {
            file_paths.insert(p_dir->get_file_path(i));
        }
    }

    // check subdirectories
    for (int i = 0; i < p_dir->get_subdir_count(); i++) {
        Set<String> paths = _find_singletons_recursive(p_dir->get_subdir(i));

        for (const String &E : paths) {
            file_paths.insert(E);
        }
    }

    return file_paths;
}

void GDNativeLibrarySingletonEditor::_discover_singletons() {

    EditorFileSystemDirectory *dir = EditorFileSystem::get_singleton()->get_filesystem();

    Set<String> file_paths = _find_singletons_recursive(dir);

    bool changed = false;
    Array current_files;
    if (ProjectSettings::get_singleton()->has_setting("gdnative/singletons")) {
        current_files = ProjectSettings::get_singleton()->get("gdnative/singletons");
    }
    Array files;
    for (const String &E : file_paths) {
        if (!current_files.contains(E)) {
            changed = true;
        }
        files.append(E);
    }

    // Check for removed files
    if (!changed) {
        // Removed singleton
        for (int j = 0; j < current_files.size(); j++) {
            if (!files.contains(current_files[j])) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {

        ProjectSettings::get_singleton()->set("gdnative/singletons", files);
        _update_libraries(); // So singleton options (i.e. disabled) updates too
        ProjectSettings::get_singleton()->save();
    }
}

void GDNativeLibrarySingletonEditor::_update_libraries() {

    updating = true;
    libraries->clear();
    libraries->create_item(); // root item

    Array singletons;
    if (ProjectSettings::get_singleton()->has_setting("gdnative/singletons")) {
        singletons = ProjectSettings::get_singleton()->get("gdnative/singletons");
    }
    Array singletons_disabled;
    if (ProjectSettings::get_singleton()->has_setting("gdnative/singletons_disabled")) {
        singletons_disabled = ProjectSettings::get_singleton()->get("gdnative/singletons_disabled");
    }

    Array updated_disabled;
    for (int i = 0; i < singletons.size(); i++) {
        bool enabled = true;
        String path = singletons[i];
        if (singletons_disabled.contains(path)) {
            enabled = false;
            updated_disabled.push_back(path);
        }
        TreeItem *ti = libraries->create_item(libraries->get_root());
        ti->set_text(0, PathUtils::get_file(path));
        ti->set_tooltip(0, path);
        ti->set_metadata(0, path);
        ti->set_cell_mode(1, TreeItem::CELL_MODE_RANGE);
        ti->set_text(1, String("Disabled,Enabled"));
        ti->set_range(1, enabled ? 1 : 0);
        ti->set_custom_color(1, enabled ? Color(0, 1, 0) : Color(1, 0, 0));
        ti->set_editable(1, true);
    }

    // The singletons list changed, we must update the settings
    if (updated_disabled.size() != singletons_disabled.size())
        ProjectSettings::get_singleton()->set("gdnative/singletons_disabled", updated_disabled);

    updating = false;
}

void GDNativeLibrarySingletonEditor::_item_edited() {
    if (updating)
        return;

    TreeItem *item = libraries->get_edited();
    if (!item)
        return;

    bool enabled = item->get_range(1);
    String path = item->get_metadata(0);

    Array disabled_paths;
    Array undo_paths;
    if (ProjectSettings::get_singleton()->has_setting("gdnative/singletons_disabled")) {
        disabled_paths = ProjectSettings::get_singleton()->get("gdnative/singletons_disabled");
        // Duplicate so redo works (not a reference)
        disabled_paths = disabled_paths.duplicate();
        // For undo, so we can reset the property.
        undo_paths = disabled_paths.duplicate();
    }

    if (enabled) {
        disabled_paths.erase(path);
    } else {
        if (disabled_paths.find(path) == -1)
            disabled_paths.push_back(path);
    }

    undo_redo->create_action(enabled ? TTR("Enabled GDNative Singleton") : TTR("Disabled GDNative Singleton"));
    undo_redo->add_do_property(ProjectSettings::get_singleton(), String("gdnative/singletons_disabled"), disabled_paths);
    undo_redo->add_do_method(this, String("_update_libraries"));
    undo_redo->add_undo_property(ProjectSettings::get_singleton(), String("gdnative/singletons_disabled"), undo_paths);
    undo_redo->add_undo_method(this, String("_update_libraries"));
    undo_redo->commit_action();
}

void GDNativeLibrarySingletonEditor::_notification(int p_what) {

    if (p_what == NOTIFICATION_VISIBILITY_CHANGED) {
        if (is_visible_in_tree()) {
            _update_libraries();
        }
    }
}

void GDNativeLibrarySingletonEditor::_bind_methods() {

    MethodBinder::bind_method(D_METHOD("_item_edited"), &GDNativeLibrarySingletonEditor::_item_edited);
    MethodBinder::bind_method(D_METHOD("_discover_singletons"), &GDNativeLibrarySingletonEditor::_discover_singletons);
    MethodBinder::bind_method(D_METHOD("_update_libraries"), &GDNativeLibrarySingletonEditor::_update_libraries);
}

GDNativeLibrarySingletonEditor::GDNativeLibrarySingletonEditor() {
    undo_redo = EditorNode::get_singleton()->get_undo_redo();
    libraries = memnew(Tree);
    libraries->set_columns(2);
    libraries->set_column_titles_visible(true);
    libraries->set_column_title(0, TTR("Library"));
    libraries->set_column_title(1, TTR("Status"));
    libraries->set_hide_root(true);
    add_margin_child(TTR("Libraries: "), libraries, true);
    updating = false;
    libraries->connect("item_edited", this, "_item_edited");
    EditorFileSystem::get_singleton()->connect("filesystem_changed", this, "_discover_singletons");
}

#endif // TOOLS_ENABLED
