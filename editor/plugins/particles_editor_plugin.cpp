/*************************************************************************/
/*  particles_editor_plugin.cpp                                          */
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

#include "particles_editor_plugin.h"

#include "core/callable_method_pointer.h"
#include "core/method_bind.h"
#include "core/io/resource_loader.h"
#include "core/translation_helpers.h"
#include "core/string_formatter.h"
#include "core/resource/resource_manager.h"

#include "editor/editor_node.h"
#include "editor/plugins/node_3d_editor_plugin.h"
#include "editor/scene_tree_dock.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/particles_material.h"

#include "editor/scene_tree_editor.h"

IMPL_GDCLASS(ParticlesEditorBase)
IMPL_GDCLASS(ParticlesEditor)
IMPL_GDCLASS(ParticlesEditorPlugin)

bool ParticlesEditorBase::_generate(PoolVector<Vector3> &points, PoolVector<Vector3> &normals) {

    bool use_normals = emission_fill->get_selected() == 1;

    if (emission_fill->get_selected() < 2) {

        float area_accum = 0;
        Map<float, int> triangle_area_map;

        for (int i = 0; i < geometry.size(); i++) {

            float area = geometry[i].get_area();
            if (area < CMP_EPSILON)
                continue;
            triangle_area_map[area_accum] = i;
            area_accum += area;
        }

        if (triangle_area_map.empty() || area_accum == 0.0f) {

            EditorNode::get_singleton()->show_warning(TTR("The geometry's faces don't contain any area."));
            return false;
        }

        int emissor_count = emission_amount->get_value();

        for (int i = 0; i < emissor_count; i++) {

            float areapos = Math::random(0.0f, area_accum);

            Map<float, int>::iterator E = triangle_area_map.lower_bound(areapos);
            ERR_FAIL_COND_V(E==triangle_area_map.end(), false);
            int index = E->second;
            ERR_FAIL_INDEX_V(index, geometry.size(), false);

            // ok FINALLY get face
            Face3 face = geometry[index];
            //now compute some position inside the face...

            Vector3 pos = face.get_random_point_inside();

            points.push_back(pos);

            if (use_normals) {
                Vector3 normal = face.get_plane().normal;
                normals.push_back(normal);
            }
        }
    } else {

        int gcount = geometry.size();

        if (gcount == 0) {

            EditorNode::get_singleton()->show_warning(TTR("The geometry doesn't contain any faces."));
            return false;
        }

        AABB aabb;

        for (int i = 0; i < gcount; i++) {

            for (int j = 0; j < 3; j++) {

                if (i == 0 && j == 0)
                    aabb.position = geometry[i].vertex[j];
                else
                    aabb.expand_to(geometry[i].vertex[j]);
            }
        }

        int emissor_count = emission_amount->get_value();

        for (int i = 0; i < emissor_count; i++) {

            int attempts = 5;

            for (int j = 0; j < attempts; j++) {

                Vector3 dir;
                dir[Math::rand() % 3] = 1.0;
                Vector3 ofs = (Vector3(1, 1, 1) - dir) * Vector3(Math::randf(), Math::randf(), Math::randf()) * aabb.size + aabb.position;

                Vector3 ofsv = ofs + aabb.size * dir;

                //space it a little
                ofs -= dir;
                ofsv += dir;

                float max = -1e7, min = 1e7;

                for (int k = 0; k < gcount; k++) {

                    const Face3 &f3 = geometry[k];

                    Vector3 res;
                    if (f3.intersects_segment(ofs, ofsv, &res)) {

                        res -= ofs;
                        float d = dir.dot(res);

                        if (d < min)
                            min = d;
                        if (d > max)
                            max = d;
                    }
                }

                if (max < min)
                    continue; //lost attempt

                float val = min + (max - min) * Math::randf();

                Vector3 point = ofs + dir * val;

                points.push_back(point);
                break;
            }
        }
    }

    return true;
}

void ParticlesEditorBase::_node_selected(const NodePath &p_path) {

    Node *sel = get_node(p_path);
    if (!sel)
        return;

    if (!sel->is_class("Node3D")) {

        EditorNode::get_singleton()->show_warning(FormatSN(TTR("\"%s\" doesn't inherit from Node3D.").asCString(), sel->get_name().asCString()));
        return;
    }

    VisualInstance3D *vi = object_cast<VisualInstance3D>(sel);
    if (!vi) {

        EditorNode::get_singleton()->show_warning(FormatSN(TTR("\"%s\" doesn't contain geometry.").asCString(), sel->get_name().asCString()));
        return;
    }

    geometry = vi->get_faces(VisualInstance3D::FACES_SOLID);

    if (geometry.empty()) {

        EditorNode::get_singleton()->show_warning(FormatSN(TTR("\"%s\" doesn't contain face geometry.").asCString(), sel->get_name().asCString()));
        return;
    }

    Transform geom_xform = base_node->get_global_transform().affine_inverse() * vi->get_global_transform();

    int gc = geometry.size();
    for (int i = 0; i < gc; i++) {
        for (int j = 0; j < 3; j++) {
            geometry[i].vertex[j] = geom_xform.xform(geometry[i].vertex[j]);
        }
    }

    emission_dialog->popup_centered(Size2(300, 130));
}



ParticlesEditorBase::ParticlesEditorBase() {

    emission_dialog = memnew(ConfirmationDialog);
    emission_dialog->set_title(TTR("Create Emitter"));
    add_child(emission_dialog);
    VBoxContainer *emd_vb = memnew(VBoxContainer);
    emission_dialog->add_child(emd_vb);

    emission_amount = memnew(SpinBox);
    emission_amount->set_min(1);
    emission_amount->set_max(100000);
    emission_amount->set_value(512);
    emd_vb->add_margin_child(TTR("Emission Points:"), emission_amount);

    emission_fill = memnew(OptionButton);
    emission_fill->add_item(TTR("Surface Points"));
    emission_fill->add_item(TTR("Surface Points+Normal (Directed)"));
    emission_fill->add_item(TTR("Volume"));
    emd_vb->add_margin_child(TTR("Emission Source: "), emission_fill);

    emission_dialog->get_ok()->set_text(TTR("Create"));
    emission_dialog->connect("confirmed",callable_mp(this, &ClassName::_generate_emission_points));

    emission_tree_dialog = memnew(SceneTreeDialog);
    add_child(emission_tree_dialog);
    emission_tree_dialog->connect("selected",callable_mp(this, &ClassName::_node_selected));

}

void ParticlesEditor::_node_removed(Node *p_node) {

    if (p_node == node) {
        node = nullptr;
        hide();
    }
}

void ParticlesEditor::_notification(int p_notification) {

    if (p_notification == NOTIFICATION_ENTER_TREE) {
        options->set_button_icon(options->get_popup()->get_theme_icon("GPUParticles3D", "EditorIcons"));
        get_tree()->connect("node_removed",callable_mp(this, &ClassName::_node_removed));
    }
}

void ParticlesEditor::_menu_option(int p_option) {

    switch (p_option) {

        case MENU_OPTION_GENERATE_AABB: {
            float gen_time = node->get_lifetime();

            if (gen_time < 1.0f)
                generate_seconds->set_value(1.0);
            else
                generate_seconds->set_value(trunc(gen_time) + 1.0);
            generate_aabb->popup_centered_minsize();
        } break;
        case MENU_OPTION_CREATE_EMISSION_VOLUME_FROM_NODE: {
            Ref<ParticlesMaterial> material = dynamic_ref_cast<ParticlesMaterial>(node->get_process_material());
            if (not material) {
                EditorNode::get_singleton()->show_warning(TTR("A processor material of type 'ParticlesMaterial' is required."));
                return;
            }

            emission_tree_dialog->popup_centered_ratio();

        } break;
        case MENU_OPTION_CONVERT_TO_CPU_PARTICLES: {

            CPUParticles3D *cpu_particles = memnew(CPUParticles3D);
            cpu_particles->convert_from_particles(node);
            cpu_particles->set_name(node->get_name());
            cpu_particles->set_transform(node->get_transform());
            cpu_particles->set_visible(node->is_visible());
            cpu_particles->set_pause_mode(node->get_pause_mode());

            UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();
            ur->create_action(TTR("Convert to CPUParticles3D"));
            ur->add_do_method(EditorNode::get_singleton()->get_scene_tree_dock(), "replace_node", Variant(node), Variant(cpu_particles), true, false);
            ur->add_do_reference(cpu_particles);
            ur->add_undo_method(EditorNode::get_singleton()->get_scene_tree_dock(), "replace_node", Variant(cpu_particles), Variant(node), false, false);
            ur->add_undo_reference(node);
            ur->commit_action();

        } break;
        case MENU_OPTION_RESTART: {

            node->restart();

        } break;
    }
}

void ParticlesEditor::_generate_aabb() {

    float time = generate_seconds->get_value();

    float running = 0.0;

    EditorProgress ep(("gen_aabb"), TTR("Generating AABB"), int(time));

    bool was_emitting = node->is_emitting();
    if (!was_emitting) {
        node->set_emitting(true);
        OS::get_singleton()->delay_usec(1000);
    }

    AABB rect;

    while (running < time) {

        uint64_t ticks = OS::get_singleton()->get_ticks_usec();
        ep.step("Generating...", int(running), true);
        OS::get_singleton()->delay_usec(1000);

        AABB capture = node->capture_aabb();
        if (rect == AABB())
            rect = capture;
        else
            rect.merge_with(capture);

        running += (OS::get_singleton()->get_ticks_usec() - ticks) / 1000000.0;
    }

    if (!was_emitting) {
        node->set_emitting(false);
    }

    UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();
    ur->create_action(TTR("Generate Visibility AABB"));
    ur->add_do_method(node, "set_visibility_aabb", rect);
    ur->add_undo_method(node, "set_visibility_aabb", node->get_visibility_aabb());
    ur->commit_action();
}

void ParticlesEditor::edit(GPUParticles3D *p_particles) {

    base_node = p_particles;
    node = p_particles;
}

void ParticlesEditor::_generate_emission_points() {

    /// hacer codigo aca
    PoolVector<Vector3> points;
    PoolVector<Vector3> normals;

    if (!_generate(points, normals)) {
        return;
    }

    int point_count = points.size();

    int w = 2048;
    int h = point_count / 2048 + 1;

    PoolVector<uint8_t> point_img;
    point_img.resize(w * h * 3 * sizeof(float));

    {
        PoolVector<uint8_t>::Write iw = point_img.write();
        memset(iw.ptr(), 0, w * h * 3 * sizeof(float));
        PoolVector<Vector3>::Read r = points.read();
        float *wf = (float *)iw.ptr();
        for (int i = 0; i < point_count; i++) {
            wf[i * 3 + 0] = r[i].x;
            wf[i * 3 + 1] = r[i].y;
            wf[i * 3 + 2] = r[i].z;
        }
    }

    Ref<Image> image(make_ref_counted<Image>(w, h, false, ImageData::FORMAT_RGBF, point_img));

    Ref<ImageTexture> tex(make_ref_counted<ImageTexture>());
    tex->create_from_image(image, Texture::FLAG_FILTER);

    Ref<ParticlesMaterial> material = dynamic_ref_cast<ParticlesMaterial>(node->get_process_material());
    ERR_FAIL_COND(not material);

    if (normals.size() > 0) {

        material->set_emission_shape(ParticlesMaterial::EMISSION_SHAPE_DIRECTED_POINTS);
        material->set_emission_point_count(point_count);
        material->set_emission_point_texture(tex);

        PoolVector<uint8_t> point_img2;
        point_img2.resize(w * h * 3 * sizeof(float));

        {
            PoolVector<uint8_t>::Write iw = point_img2.write();
            memset(iw.ptr(), 0, w * h * 3 * sizeof(float));
            PoolVector<Vector3>::Read r = normals.read();
            float *wf = (float *)iw.ptr();
            for (int i = 0; i < point_count; i++) {
                wf[i * 3 + 0] = r[i].x;
                wf[i * 3 + 1] = r[i].y;
                wf[i * 3 + 2] = r[i].z;
            }
        }

        Ref<Image> image2(make_ref_counted<Image>(w, h, false, ImageData::FORMAT_RGBF, point_img2));

        Ref<ImageTexture> tex2(make_ref_counted<ImageTexture>());
        tex2->create_from_image(image2, Texture::FLAG_FILTER);

        material->set_emission_normal_texture(tex2);
    } else {

        material->set_emission_shape(ParticlesMaterial::EMISSION_SHAPE_POINTS);
        material->set_emission_point_count(point_count);
        material->set_emission_point_texture(tex);
    }
}



ParticlesEditor::ParticlesEditor() {

    node = nullptr;
    particles_editor_hb = memnew(HBoxContainer);
    Node3DEditor::get_singleton()->add_control_to_menu_panel(particles_editor_hb);
    options = memnew(MenuButton);
    options->set_switch_on_hover(true);
    particles_editor_hb->add_child(options);
    particles_editor_hb->hide();

    options->set_text(TTR("Particles"));
    options->get_popup()->add_item(TTR("Generate Visibility AABB"), MENU_OPTION_GENERATE_AABB);
    options->get_popup()->add_separator();
    options->get_popup()->add_item(TTR("Create Emission Points From Node"), MENU_OPTION_CREATE_EMISSION_VOLUME_FROM_NODE);
    options->get_popup()->add_separator();
    options->get_popup()->add_item(TTR("Convert to CPUParticles3D"), MENU_OPTION_CONVERT_TO_CPU_PARTICLES);
    options->get_popup()->add_separator();
    options->get_popup()->add_item(TTR("Restart"), MENU_OPTION_RESTART);

    options->get_popup()->connect("id_pressed",callable_mp(this, &ClassName::_menu_option));

    generate_aabb = memnew(ConfirmationDialog);
    generate_aabb->set_title(TTR("Generate Visibility AABB"));
    VBoxContainer *genvb = memnew(VBoxContainer);
    generate_aabb->add_child(genvb);
    generate_seconds = memnew(SpinBox);
    genvb->add_margin_child(TTR("Generation Time (sec):"), generate_seconds);
    generate_seconds->set_min(0.1);
    generate_seconds->set_max(25);
    generate_seconds->set_value(2);

    add_child(generate_aabb);

    generate_aabb->connect("confirmed",callable_mp(this, &ClassName::_generate_aabb));
}

void ParticlesEditorPlugin::edit(Object *p_object) {

    particles_editor->edit(object_cast<GPUParticles3D>(p_object));
}

bool ParticlesEditorPlugin::handles(Object *p_object) const {

    return p_object->is_class("Particles");
}

void ParticlesEditorPlugin::make_visible(bool p_visible) {

    if (p_visible) {
        particles_editor->show();
        particles_editor->particles_editor_hb->show();
    } else {
        particles_editor->particles_editor_hb->hide();
        particles_editor->hide();
        particles_editor->edit(nullptr);
    }
}

ParticlesEditorPlugin::ParticlesEditorPlugin(EditorNode *p_node) {

    editor = p_node;
    particles_editor = memnew(ParticlesEditor);
    editor->get_viewport()->add_child(particles_editor);

    particles_editor->hide();
}

ParticlesEditorPlugin::~ParticlesEditorPlugin() = default;
