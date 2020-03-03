/*************************************************************************/
/*  editor_folding.cpp                                                   */
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

#include "editor_folding.h"

#include "core/object_tooling.h"
#include "core/os/file_access.h"
#include "core/pool_vector.h"
#include "core/property_info.h"
#include "editor_inspector.h"
#include "editor_settings.h"
#include "core/string_utils.h"
#include "core/set.h"

PoolVector<String> EditorFolding::_get_unfolds(const Object *p_object) {

    PoolVector<String> sections;
    sections.resize(p_object->get_tooling_interface()->editor_get_section_folding().size());
    if (not sections.empty()) {
        PoolVector<String>::Write w = sections.write();
        int idx = 0;
        for (const String &E : p_object->get_tooling_interface()->editor_get_section_folding()) {
            w[idx++] = E;
        }
    }

    return sections;
}

void EditorFolding::save_resource_folding(const RES &p_resource, StringView p_path) {
    Ref<ConfigFile> config(make_ref_counted<ConfigFile>());
    PoolVector<String> unfolds = _get_unfolds(p_resource.get());
    config->set_value("folding", "sections_unfolded", unfolds);

    String file = String(PathUtils::get_file(p_path)) + "-folding-" + StringUtils::md5_text(p_path) + ".cfg";
    file = PathUtils::plus_file(EditorSettings::get_singleton()->get_project_settings_dir(),file);
    config->save(file);
}

void EditorFolding::_set_unfolds(Object *p_object, const PoolVector<String> &p_unfolds) {

    int uc = p_unfolds.size();
    PoolVector<String>::Read r = p_unfolds.read();
    p_object->get_tooling_interface()->editor_clear_section_folding();
    for (int i = 0; i < uc; i++) {
        p_object->get_tooling_interface()->editor_set_section_unfold(r[i], true);
    }
}

void EditorFolding::load_resource_folding(const RES& p_resource, StringView p_path) {

    Ref<ConfigFile> config(make_ref_counted<ConfigFile>());

    String file(String(PathUtils::get_file(p_path)) + "-folding-" + StringUtils::md5_text(p_path) + ".cfg");
    file = PathUtils::plus_file(EditorSettings::get_singleton()->get_project_settings_dir(),file);

    if (config->load(file) != OK) {
        return;
    }

    PoolVector<String> unfolds;

    if (config->has_section_key("folding", "sections_unfolded")) {
        unfolds = config->get_value("folding", "sections_unfolded").as<PoolVector<String>>();
    }
    _set_unfolds(p_resource.get(), unfolds);
}

void EditorFolding::_fill_folds(const Node *p_root, const Node *p_node, Array &p_folds, Array &resource_folds, Array &nodes_folded, Set<RES> &resources) {
    if (p_root != p_node) {
        if (!p_node->get_owner()) {
            return; //not owned, bye
        }
        if (p_node->get_owner() != p_root && !p_root->is_editable_instance(p_node)) {
            return;
        }
    }

    if (p_node->is_displayed_folded()) {
        nodes_folded.push_back(p_root->get_path_to(p_node));
    }
    PoolVector<String> unfolds = _get_unfolds(p_node);

    if (not unfolds.empty()) {
        p_folds.push_back(p_root->get_path_to(p_node));
        p_folds.push_back(unfolds);
    }

    Vector<PropertyInfo> plist;
    p_node->get_property_list(&plist);
    for (const PropertyInfo &E : plist) {
        if (!(E.usage & PROPERTY_USAGE_EDITOR) || E.type != VariantType::OBJECT)
            continue;

        RES res(p_node->get(E.name));
        if (!res || resources.contains(res) || res->get_path().empty() || PathUtils::is_resource_file(res->get_path()))
            continue;

        PoolVector<String> res_unfolds = _get_unfolds(res.get());
        resource_folds.push_back(res->get_path());
        resource_folds.push_back(res_unfolds);
        resources.insert(res);
    }

    for (int i = 0; i < p_node->get_child_count(); i++) {
        _fill_folds(p_root, p_node->get_child(i), p_folds, resource_folds, nodes_folded, resources);
    }
}
void EditorFolding::save_scene_folding(const Node *p_scene, StringView p_path) {
    FileAccessRef file_check = FileAccess::create(FileAccess::ACCESS_RESOURCES);
    if (!file_check->file_exists(p_path)) //This can happen when creating scene from FilesystemDock. It has path, but no file.
        return;

    Ref<ConfigFile> config(make_ref_counted<ConfigFile>());

    Array unfolds, res_unfolds;
    Set<RES> resources;
    Array nodes_folded;
    _fill_folds(p_scene, p_scene, unfolds, res_unfolds, nodes_folded, resources);

    config->set_value("folding", "node_unfolds", unfolds);
    config->set_value("folding", "resource_unfolds", res_unfolds);
    config->set_value("folding", "nodes_folded", nodes_folded);

    String file = String(PathUtils::get_file(p_path)) + "-folding-" + StringUtils::md5_text(p_path) + ".cfg";
    file = PathUtils::plus_file(EditorSettings::get_singleton()->get_project_settings_dir(),file);
    config->save(file);
}
void EditorFolding::load_scene_folding(Node *p_scene, StringView p_path) {

    Ref<ConfigFile> config(make_ref_counted<ConfigFile>());

    String path = EditorSettings::get_singleton()->get_project_settings_dir();
    String file = String(PathUtils::get_file(p_path)) + "-folding-" + StringUtils::md5_text(p_path) + ".cfg";
    file = PathUtils::plus_file(EditorSettings::get_singleton()->get_project_settings_dir(),file);

    if (config->load(file) != OK) {
        return;
    }

    Array unfolds = config->get_folding("node_unfolds");
    Array res_unfolds = config->get_folding("folding", "resource_unfolds");
    Array nodes_folded = config->get_folding("folding", "nodes_folded");

    ERR_FAIL_COND(unfolds.size() & 1);
    ERR_FAIL_COND(res_unfolds.size() & 1);

    for (int i = 0; i < unfolds.size(); i += 2) {
        NodePath path2 = unfolds[i];
        PoolVector<String> un = unfolds[i + 1].as<PoolVector<String>>();
        Node *node = p_scene->get_node_or_null(path2);
        if (!node) {
            continue;
        }
        _set_unfolds(node, un);
    }

    for (int i = 0; i < res_unfolds.size(); i += 2) {
        String path2 = res_unfolds[i];
        RES res;
        if (ResourceCache::has(path2)) {
            res = RES(ResourceCache::get(path2));
        }
        if (not res) {
            continue;
        }

        PoolVector<String> unfolds2 = res_unfolds[i + 1].as<PoolVector<String>>();
        _set_unfolds(res.get(), unfolds2);
    }

    for (int i = 0; i < nodes_folded.size(); i++) {
        NodePath fold_path = nodes_folded[i];
        if (p_scene->has_node(fold_path)) {
            Node *node = p_scene->get_node(fold_path);
            node->set_display_folded(true);
        }
    }
}

bool EditorFolding::has_folding_data(StringView p_path) {

    String file = String(PathUtils::get_file(p_path)) + "-folding-" + StringUtils::md5_text(p_path) + ".cfg";
    file = PathUtils::plus_file(EditorSettings::get_singleton()->get_project_settings_dir(),file);
    return FileAccess::exists(file);
}

void EditorFolding::_do_object_unfolds(Object *p_object, Set<RES> &resources) {

    Vector<PropertyInfo> plist;
    p_object->get_property_list(&plist);
    String group_base;
    String group;

    Set<String> unfold_group;

    for (PropertyInfo &E : plist) {

        if (E.usage & PROPERTY_USAGE_CATEGORY) {
            group = "";
            group_base = "";
        }
        if (E.usage & PROPERTY_USAGE_GROUP) {
            group = E.name;
            group_base = E.hint_string;
            if (StringUtils::ends_with(group_base,"_")) {
                group_base = StringUtils::substr(group_base,0, group_base.length() - 1);
            }
        }

        //can unfold
        if (E.usage & PROPERTY_USAGE_EDITOR) {

            if (!group.empty()) { //group
                if (group_base.empty() || StringUtils::begins_with(E.name,group_base)) {
                    bool can_revert = EditorPropertyRevert::can_property_revert(p_object, E.name);
                    if (can_revert) {
                        unfold_group.insert(group);
                    }
                }
            } else { //path
                int last = StringUtils::find_last(E.name,'/');
                if (last != -1) {
                    bool can_revert = EditorPropertyRevert::can_property_revert(p_object, E.name);
                    if (can_revert) {
                        unfold_group.insert(StringUtils::substr(E.name,0, last));
                    }
                }
            }
        }

        if (E.type == VariantType::OBJECT) {
            RES res(p_object->get(E.name));
            if (res && !resources.contains(res) && not res->get_path().empty() && !PathUtils::is_resource_file(res->get_path())) {

                resources.insert(res);
                _do_object_unfolds(res.get(), resources);
            }
        }
    }

    for (const String &E : unfold_group) {
        p_object->get_tooling_interface()->editor_set_section_unfold(E, true);
    }
}

void EditorFolding::_do_node_unfolds(Node *p_root, Node *p_node, Set<RES> &resources) {
    if (p_root != p_node) {
        if (!p_node->get_owner()) {
            return; //not owned, bye
        }
        if (p_node->get_owner() != p_root && !p_root->is_editable_instance(p_node)) {
            return;
        }
    }

    _do_object_unfolds(p_node, resources);

    for (int i = 0; i < p_node->get_child_count(); i++) {
        _do_node_unfolds(p_root, p_node->get_child(i), resources);
    }
}

void EditorFolding::unfold_scene(Node *p_scene) {

    Set<RES> resources;
    _do_node_unfolds(p_scene, p_scene, resources);
}

EditorFolding::EditorFolding() {
}
