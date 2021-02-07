/*************************************************************************/
/*  color_picker.cpp                                                     */
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

#include "color_picker.h"

#include "core/callable_method_pointer.h"
#include "core/os/input.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/string_formatter.h"
#include "core/translation_helpers.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/style_box.h"
#include "core/method_bind.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_scale.h"
#include "editor/editor_settings.h"
#endif
#include "scene/main/viewport.h"

IMPL_GDCLASS(ColorPicker)
IMPL_GDCLASS(ColorPickerButton)

void ColorPicker::_notification(int p_what) {

    switch (p_what) {
        case NOTIFICATION_THEME_CHANGED: {

            btn_pick->set_button_icon(get_theme_icon("screen_picker", "ColorPicker"));
            bt_add_preset->set_button_icon(get_theme_icon("add_preset"));

            _update_controls();
        } break;
        case NOTIFICATION_ENTER_TREE: {

            btn_pick->set_button_icon(get_theme_icon("screen_picker", "ColorPicker"));
            bt_add_preset->set_button_icon(get_theme_icon("add_preset"));

            _update_color();

#ifdef TOOLS_ENABLED
            if (Engine::get_singleton()->is_editor_hint()) {
                PoolColorArray saved_presets = EditorSettings::get_singleton()->get_project_metadataT("color_picker", "presets", PoolColorArray());

                for (int i = 0; i < saved_presets.size(); i++) {
                    add_preset(saved_presets[i]);
                }
            }
#endif
        } break;
        case NOTIFICATION_PARENTED: {

            for (int i = 0; i < 4; i++)
                set_margin((Margin)i, get_theme_constant("margin"));
        } break;
        case NOTIFICATION_VISIBILITY_CHANGED: {

            Popup *p = object_cast<Popup>(get_parent());
            if (p)
                p->set_size(Size2(get_combined_minimum_size().width + get_theme_constant("margin") * 2, get_combined_minimum_size().height + get_theme_constant("margin") * 2));
        } break;
        case MainLoop::NOTIFICATION_WM_QUIT_REQUEST: {

            if (screen != nullptr && screen->is_visible())
                screen->hide();
        } break;
    }
}

void ColorPicker::set_focus_on_line_edit() {

    c_text->call_deferred([ct=c_text] {ct->grab_focus();});
}

void ColorPicker::_update_controls() {

    const char *rgb[3] = { "R", "G", "B" };
    const char *hsv[3] = { "H", "S", "V" };

    if (hsv_mode_enabled) {
        for (int i = 0; i < 3; i++)
            labels[i]->set_text(hsv[i]);
    } else {
        for (int i = 0; i < 3; i++)
            labels[i]->set_text(rgb[i]);
    }

    if (hsv_mode_enabled) {
        set_raw_mode(false);
        btn_raw->set_disabled(true);
    } else if (raw_mode_enabled) {
        set_hsv_mode(false);
        btn_hsv->set_disabled(true);
    } else {
        btn_raw->set_disabled(false);
        btn_hsv->set_disabled(false);
    }

    if (edit_alpha) {
        values[3]->show();
        scroll[3]->show();
        labels[3]->show();
    } else {
        values[3]->hide();
        scroll[3]->hide();
        labels[3]->hide();
    }
}

void ColorPicker::_set_pick_color(const Color &p_color, bool p_update_sliders) {

    color = p_color;
    if (color != last_hsv) {
        h = color.get_h();
        s = color.get_s();
        v = color.get_v();
        last_hsv = color;
    }

    if (!is_inside_tree())
        return;

    _update_color(p_update_sliders);
}

void ColorPicker::set_pick_color(const Color &p_color) {

    _set_pick_color(p_color, true); //because setters can't have more arguments
}

void ColorPicker::set_edit_alpha(bool p_show) {

    edit_alpha = p_show;
    _update_controls();

    if (!is_inside_tree())
        return;

    _update_color();
    sample->update();
}

bool ColorPicker::is_editing_alpha() const {

    return edit_alpha;
}

void ColorPicker::_value_changed(double) {

    if (updating)
        return;

    if (hsv_mode_enabled) {
        color.set_hsv(scroll[0]->get_value() / 360.0f,
                scroll[1]->get_value() / 100.0f,
                scroll[2]->get_value() / 100.0f,
                scroll[3]->get_value() / 255.0f);
    } else {
        for (int i = 0; i < 4; i++) {
            color.component(i) = scroll[i]->get_value() / (raw_mode_enabled ? 1.0f : 255.0f);
        }
    }

    _set_pick_color(color, false);
    emit_signal("color_changed", color);
}

void ColorPicker::_html_entered(StringView p_html) {

    if (updating || text_is_constructor || !c_text->is_visible())
        return;

    float last_alpha = color.a;
    color = Color::html(p_html);
    if (!is_editing_alpha())
        color.a = last_alpha;

    if (!is_inside_tree())
        return;

    set_pick_color(color);
    emit_signal("color_changed", color);
}

void ColorPicker::_update_color(bool p_update_sliders) {

    updating = true;

    if (p_update_sliders) {

        if (hsv_mode_enabled) {
            for (int i = 0; i < 4; i++) {
                scroll[i]->set_step(1.0);
            }

            scroll[0]->set_max(359);
            scroll[0]->set_value(h * 360.0);
            scroll[1]->set_max(100);
            scroll[1]->set_value(s * 100.0);
            scroll[2]->set_max(100);
            scroll[2]->set_value(v * 100.0);
            scroll[3]->set_max(255);
            scroll[3]->set_value(color.a * 255.0);
        } else {
            for (int i = 0; i < 4; i++) {
                if (raw_mode_enabled) {
                    scroll[i]->set_step(0.01);
                    scroll[i]->set_max(100);
                    if (i == 3)
                        scroll[i]->set_max(1);
                    scroll[i]->set_value(color.component(i));
                } else {
                    scroll[i]->set_step(1);
                    const float byte_value = color.component(i) * 255.0f;
                    scroll[i]->set_max(next_power_of_2(M_MAX(255, byte_value)) - 1);
                    scroll[i]->set_value(byte_value);
                }
            }
        }
    }

    _update_text_value();

    sample->update();
    uv_edit->update();
    w_edit->update();
    updating = false;
}

void ColorPicker::_update_presets() {
    presets_per_row = 10;
    Size2 size = bt_add_preset->get_size();
    Size2 preset_size = Size2(MIN(size.width * presets.size(), presets_per_row * size.width), size.height * (Math::ceil((float)presets.size() / presets_per_row)));
    preset->set_custom_minimum_size(preset_size);

    preset_container->set_custom_minimum_size(preset_size);
    preset->draw_rect(Rect2(Point2(), preset_size), Color(1, 1, 1, 0));

    for (int i = 0; i < presets.size(); i++) {
        int x = (i % presets_per_row) * size.width;
        int y = (Math::floor((float)i / presets_per_row)) * size.height;
        preset->draw_rect(Rect2(Point2(x, y), size), presets[i]);
    }
    _notification(NOTIFICATION_VISIBILITY_CHANGED);
}

void ColorPicker::_text_type_toggled() {

    text_is_constructor = !text_is_constructor;
    if (text_is_constructor) {
        text_type->set_text("");
        text_type->set_button_icon(get_theme_icon("Script", "EditorIcons"));

        c_text->set_editable(false);
    } else {
        text_type->set_text("#");
        text_type->set_button_icon(Ref<Texture>());

        c_text->set_editable(true);
    }
    _update_color();
}

Color ColorPicker::get_pick_color() const {

    return color;
}

void ColorPicker::add_preset(const Color &p_color) {
    auto iter = presets.find(p_color);
    if (iter!=presets.end()) {
        auto tmp = *iter;
        presets.erase(iter);
        presets.emplace_back(tmp);
    } else {
        presets.push_back(p_color);
    }
    preset->update();

#ifdef TOOLS_ENABLED
    if (Engine::get_singleton()->is_editor_hint()) {
        PoolColorArray arr_to_save = get_presets();
        EditorSettings::get_singleton()->set_project_metadata("color_picker", "presets", arr_to_save);
    }
#endif
}

void ColorPicker::erase_preset(const Color &p_color) {
    auto iter = presets.find(p_color);
    if (presets.end()!=iter) {
        presets.erase(iter);
        preset->update();

#ifdef TOOLS_ENABLED
        if (Engine::get_singleton()->is_editor_hint()) {
            PoolColorArray arr_to_save = get_presets();
            EditorSettings::get_singleton()->set_project_metadata("color_picker", "presets", arr_to_save);
        }
#endif
    }
}

PoolColorArray ColorPicker::get_presets() const {

    PoolColorArray arr;
    arr.resize(presets.size());
    for (int i = 0; i < presets.size(); i++) {
        arr.set(i, presets[i]);
    }
    return arr;
}

void ColorPicker::set_hsv_mode(bool p_enabled) {

    if (hsv_mode_enabled == p_enabled || raw_mode_enabled)
        return;
    hsv_mode_enabled = p_enabled;
    if (btn_hsv->is_pressed() != p_enabled)
        btn_hsv->set_pressed(p_enabled);

    if (!is_inside_tree())
        return;

    _update_controls();
    _update_color();
}

bool ColorPicker::is_hsv_mode() const {

    return hsv_mode_enabled;
}

void ColorPicker::set_raw_mode(bool p_enabled) {

    if (raw_mode_enabled == p_enabled || hsv_mode_enabled)
        return;
    raw_mode_enabled = p_enabled;
    if (btn_raw->is_pressed() != p_enabled)
        btn_raw->set_pressed(p_enabled);

    if (!is_inside_tree())
        return;

    _update_controls();
    _update_color();
}

bool ColorPicker::is_raw_mode() const {

    return raw_mode_enabled;
}

void ColorPicker::set_deferred_mode(bool p_enabled) {
    deferred_mode_enabled = p_enabled;
}

bool ColorPicker::is_deferred_mode() const {
    return deferred_mode_enabled;
}

void ColorPicker::_update_text_value() {
    bool visible = true;
    if (text_is_constructor) {
        String t = FormatVE("Color(%f, %f, %f",color.r,color.g,color.b);
        if (edit_alpha && color.a < 1)
            t += ", " + StringUtils::num(color.a) + ")";
        else
            t += ')';
        c_text->set_text(t);
    }

    if (color.r > 1 || color.g > 1 || color.b > 1 || color.r < 0 || color.g < 0 || color.b < 0) {
        visible = false;
    } else if (!text_is_constructor) {
        c_text->set_text(color.to_html(edit_alpha && color.a < 1));
    }

    text_type->set_visible(visible);
    c_text->set_visible(visible);
}

void ColorPicker::_sample_draw() {
    const Rect2 r = Rect2(Point2(), Size2(uv_edit->get_size().width, sample->get_size().height * 0.95f));
    if (color.a < 1.0f) {
        sample->draw_texture_rect(get_theme_icon("preset_bg", "ColorPicker"), r, true);
    }
    sample->draw_rect(r, color);
    if (color.r > 1 || color.g > 1 || color.b > 1) {
        // Draw an indicator to denote that the color is "overbright" and can't be displayed accurately in the preview
        sample->draw_texture(get_theme_icon("overbright_indicator", "ColorPicker"), Point2());
    }
}

void ColorPicker::_hsv_draw(int p_which, Control *c) {
    if (!c)
        return;
    if (p_which == 0) {
        Point2 points[4] {
            Vector2(),
            Vector2(c->get_size().x, 0),
            c->get_size(),
            Vector2(0, c->get_size().y)
        };
        PoolVector<Color> colors;
        colors.push_back(Color(1, 1, 1, 1));
        colors.push_back(Color(1, 1, 1, 1));
        colors.push_back(Color(0, 0, 0, 1));
        colors.push_back(Color(0, 0, 0, 1));
        c->draw_polygon(points, colors);
        PoolVector<Color> colors2;
        colors2.push_back(Color::from_hsv(h, 1, 1,0));
        colors2.push_back(Color::from_hsv(h, 1, 1,1));
        colors2.push_back(Color::from_hsv(h, 1, 0,1));
        colors2.push_back(Color::from_hsv(h, 1, 0,0));
        c->draw_polygon(points, colors2);
        int x = CLAMP(c->get_size().x * s, 0, c->get_size().x);
        int y = CLAMP(c->get_size().y - c->get_size().y * v, 0, c->get_size().y);
        Color col = color;
        col.a = 1;
        c->draw_line(Point2(x, 0), Point2(x, c->get_size().y), col.inverted());
        c->draw_line(Point2(0, y), Point2(c->get_size().x, y), col.inverted());
        c->draw_line(Point2(x, y), Point2(x, y), Color(1, 1, 1), 2);
    } else if (p_which == 1) {
        Ref<Texture> hue = get_theme_icon("color_hue", "ColorPicker");
        c->draw_texture_rect(hue, Rect2(Point2(), c->get_size()));
        int y = c->get_size().y - c->get_size().y * (1.0f - h);
        Color col = Color();
        col.set_hsv(h, 1, 1);
        c->draw_line(Point2(0, y), Point2(c->get_size().x, y), col.inverted());
    }
}

void ColorPicker::_uv_input(const Ref<InputEvent> &p_event) {

    Ref<InputEventMouseButton> bev = dynamic_ref_cast<InputEventMouseButton>(p_event);

    if (bev) {
        if (bev->is_pressed() && bev->get_button_index() == BUTTON_LEFT) {
            changing_color = true;
            float x = CLAMP((float)bev->get_position().x, 0, uv_edit->get_size().width);
            float y = CLAMP((float)bev->get_position().y, 0, uv_edit->get_size().height);
            s = x / uv_edit->get_size().width;
            v = 1.0f - y / uv_edit->get_size().height;
            color.set_hsv(h, s, v, color.a);
            last_hsv = color;
            set_pick_color(color);
            _update_color();
            if (!deferred_mode_enabled)
                emit_signal("color_changed", color);
        } else if (deferred_mode_enabled && !bev->is_pressed() && bev->get_button_index() == BUTTON_LEFT) {
            emit_signal("color_changed", color);
            changing_color = false;
        } else {
            changing_color = false;
        }
    }

    Ref<InputEventMouseMotion> mev = dynamic_ref_cast<InputEventMouseMotion>(p_event);

    if (mev) {
        if (!changing_color)
            return;
        float x = CLAMP((float)mev->get_position().x, 0, uv_edit->get_size().width);
        float y = CLAMP((float)mev->get_position().y, 0, uv_edit->get_size().height);
        s = x / uv_edit->get_size().width;
        v = 1.0 - y / uv_edit->get_size().height;
        color.set_hsv(h, s, v, color.a);
        last_hsv = color;
        set_pick_color(color);
        _update_color();
        if (!deferred_mode_enabled)
            emit_signal("color_changed", color);
    }
}

void ColorPicker::_w_input(const Ref<InputEvent> &p_event) {

    Ref<InputEventMouseButton> bev = dynamic_ref_cast<InputEventMouseButton>(p_event);

    if (bev) {

        if (bev->is_pressed() && bev->get_button_index() == BUTTON_LEFT) {
            changing_color = true;
            float y = CLAMP((float)bev->get_position().y, 0, w_edit->get_size().height);
            h = y / w_edit->get_size().height;
        } else {
            changing_color = false;
        }
        color.set_hsv(h, s, v, color.a);
        last_hsv = color;
        set_pick_color(color);
        _update_color();
        if (!deferred_mode_enabled)
            emit_signal("color_changed", color);
        else if (!bev->is_pressed() && bev->get_button_index() == BUTTON_LEFT)
            emit_signal("color_changed", color);
    }

    Ref<InputEventMouseMotion> mev = dynamic_ref_cast<InputEventMouseMotion>(p_event);

    if (mev) {

        if (!changing_color)
            return;
        float y = CLAMP((float)mev->get_position().y, 0, w_edit->get_size().height);
        h = y / w_edit->get_size().height;
        color.set_hsv(h, s, v, color.a);
        last_hsv = color;
        set_pick_color(color);
        _update_color();
        if (!deferred_mode_enabled)
            emit_signal("color_changed", color);
    }
}

void ColorPicker::_preset_input(const Ref<InputEvent> &p_event) {

    Ref<InputEventMouseButton> bev = dynamic_ref_cast<InputEventMouseButton>(p_event);

    if (bev) {

        int index = 0;
        if (bev->is_pressed() && bev->get_button_index() == BUTTON_LEFT) {
            for (int i = 0; i < presets.size(); i++) {
                int x = (i % presets_per_row) * bt_add_preset->get_size().x;
                int y = (Math::floor((float)i / presets_per_row)) * bt_add_preset->get_size().y;
                if (bev->get_position().x > x && bev->get_position().x < x + preset->get_size().x &&
                        bev->get_position().y > y && bev->get_position().y < y + preset->get_size().y) {
                    index = i;
                }
            }
            set_pick_color(presets[index]);
            _update_color();
            emit_signal("color_changed", color);
        } else if (bev->is_pressed() && bev->get_button_index() == BUTTON_RIGHT && presets_enabled) {
            index = bev->get_position().x / (preset->get_size().x / presets.size());
            Color clicked_preset = presets[index];
            erase_preset(clicked_preset);
            emit_signal("preset_removed", clicked_preset);
            bt_add_preset->show();
        }
    }

    Ref<InputEventMouseMotion> mev = dynamic_ref_cast<InputEventMouseMotion>(p_event);

    if (mev) {

        int index = mev->get_position().x * presets.size();
        if (preset->get_size().x != 0) {
            index /= preset->get_size().x;
        }
        if (index < 0 || index >= presets.size()) return;
        preset->set_tooltip_utf8("Color: #" + presets[index].to_html(presets[index].a < 1) +
                            "\n"
                            "LMB: Set color\n"
                            "RMB: Remove preset");
    }
}

void ColorPicker::_screen_input(const Ref<InputEvent> &p_event) {

    Ref<InputEventMouseButton> bev = dynamic_ref_cast<InputEventMouseButton>(p_event);
    if (bev && bev->get_button_index() == BUTTON_LEFT && !bev->is_pressed()) {
        emit_signal("color_changed", color);
        screen->hide();
    }

    Ref<InputEventMouseMotion> mev = dynamic_ref_cast<InputEventMouseMotion>(p_event);
    if (mev) {
        Viewport *r = get_tree()->get_root();
        if (!r->get_visible_rect().has_point(Point2(mev->get_global_position().x, mev->get_global_position().y)))
            return;

        Ref<Image> img = r->get_texture()->get_data();
        if (img && !img->is_empty()) {
            img->lock();
            Vector2 ofs = mev->get_global_position() - r->get_visible_rect().get_position();
            Color c = img->get_pixel(ofs.x, r->get_visible_rect().size.height - ofs.y);
            img->unlock();
            set_pick_color(c);
        }
    }
}

void ColorPicker::_add_preset_pressed() {
    add_preset(color);
    emit_signal("preset_added", color);
}

void ColorPicker::_screen_pick_pressed() {
    Viewport *r = get_tree()->get_root();
    if (!screen) {
        screen = memnew(Control);
        r->add_child(screen);
        screen->set_as_top_level(true);
        screen->set_anchors_and_margins_preset(Control::PRESET_WIDE);
        screen->set_default_cursor_shape(CURSOR_POINTING_HAND);
        screen->connect("gui_input",callable_mp(this, &ClassName::_screen_input));
        // It immediately toggles off in the first press otherwise.
        //screen->call_deferred("connect", "hide", Variant(btn_pick), "set_pressed", Variant::fromVector(Span<const Variant>(varray(false))));

        screen->call_deferred([sc=screen,pick=btn_pick] {
            sc->connect("hide", callable_gen(pick,[pick]() {
                pick->set_pressed(false);}));
            });

    }
    screen->raise();
    screen->show_modal();
}

void ColorPicker::_focus_enter() {
    bool has_ctext_focus = c_text->has_focus();
    if (has_ctext_focus) {
        c_text->select_all();
    } else {
        c_text->select(0, 0);
    }

    for (int i = 0; i < 4; i++) {
        if (values[i]->get_line_edit()->has_focus() && !has_ctext_focus) {
            values[i]->get_line_edit()->select_all();
        } else {
            values[i]->get_line_edit()->select(0, 0);
        }
    }
}

void ColorPicker::_focus_exit() {
    for (int i = 0; i < 4; i++) {
        if (!values[i]->get_line_edit()->get_menu()->is_visible())
            values[i]->get_line_edit()->select(0, 0);
    }
    c_text->select(0, 0);
}

void ColorPicker::_html_focus_exit() {
    if (c_text->get_menu()->is_visible())
        return;
    _html_entered(c_text->get_text());
    _focus_exit();
}

void ColorPicker::set_presets_enabled(bool p_enabled) {
    presets_enabled = p_enabled;
    if (!p_enabled) {
        bt_add_preset->set_disabled(true);
        bt_add_preset->set_focus_mode(FOCUS_NONE);
    } else {
        bt_add_preset->set_disabled(false);
        bt_add_preset->set_focus_mode(FOCUS_ALL);
    }
}

bool ColorPicker::are_presets_enabled() const {
    return presets_enabled;
}

void ColorPicker::set_presets_visible(bool p_visible) {
    presets_visible = p_visible;
    preset_separator->set_visible(p_visible);
    preset_container->set_visible(p_visible);
    preset_container2->set_visible(p_visible);
}

bool ColorPicker::are_presets_visible() const {
    return presets_visible;
}

void ColorPicker::_bind_methods() {

    MethodBinder::bind_method(D_METHOD("set_pick_color", {"color"}), &ColorPicker::set_pick_color);
    MethodBinder::bind_method(D_METHOD("get_pick_color"), &ColorPicker::get_pick_color);
    MethodBinder::bind_method(D_METHOD("set_hsv_mode", {"mode"}), &ColorPicker::set_hsv_mode);
    MethodBinder::bind_method(D_METHOD("is_hsv_mode"), &ColorPicker::is_hsv_mode);
    MethodBinder::bind_method(D_METHOD("set_raw_mode", {"mode"}), &ColorPicker::set_raw_mode);
    MethodBinder::bind_method(D_METHOD("is_raw_mode"), &ColorPicker::is_raw_mode);
    MethodBinder::bind_method(D_METHOD("set_deferred_mode", {"mode"}), &ColorPicker::set_deferred_mode);
    MethodBinder::bind_method(D_METHOD("is_deferred_mode"), &ColorPicker::is_deferred_mode);
    MethodBinder::bind_method(D_METHOD("set_edit_alpha", {"show"}), &ColorPicker::set_edit_alpha);
    MethodBinder::bind_method(D_METHOD("is_editing_alpha"), &ColorPicker::is_editing_alpha);
    MethodBinder::bind_method(D_METHOD("set_presets_enabled", {"enabled"}), &ColorPicker::set_presets_enabled);
    MethodBinder::bind_method(D_METHOD("are_presets_enabled"), &ColorPicker::are_presets_enabled);
    MethodBinder::bind_method(D_METHOD("set_presets_visible", {"visible"}), &ColorPicker::set_presets_visible);
    MethodBinder::bind_method(D_METHOD("are_presets_visible"), &ColorPicker::are_presets_visible);
    MethodBinder::bind_method(D_METHOD("add_preset", {"color"}), &ColorPicker::add_preset);
    MethodBinder::bind_method(D_METHOD("erase_preset", {"color"}), &ColorPicker::erase_preset);
    MethodBinder::bind_method(D_METHOD("get_presets"), &ColorPicker::get_presets);

    ADD_PROPERTY(PropertyInfo(VariantType::COLOR, "color"), "set_pick_color", "get_pick_color");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "edit_alpha"), "set_edit_alpha", "is_editing_alpha");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "hsv_mode"), "set_hsv_mode", "is_hsv_mode");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "raw_mode"), "set_raw_mode", "is_raw_mode");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "deferred_mode"), "set_deferred_mode", "is_deferred_mode");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "presets_enabled"), "set_presets_enabled", "are_presets_enabled");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "presets_visible"), "set_presets_visible", "are_presets_visible");

    ADD_SIGNAL(MethodInfo("color_changed", PropertyInfo(VariantType::COLOR, "color")));
    ADD_SIGNAL(MethodInfo("preset_added", PropertyInfo(VariantType::COLOR, "color")));
    ADD_SIGNAL(MethodInfo("preset_removed", PropertyInfo(VariantType::COLOR, "color")));
}

ColorPicker::ColorPicker() :
        BoxContainer(true) {

    updating = true;
    edit_alpha = true;
    text_is_constructor = false;
    hsv_mode_enabled = false;
    raw_mode_enabled = false;
    deferred_mode_enabled = false;
    changing_color = false;
    presets_enabled = true;
    presets_visible = true;
    screen = nullptr;

    HBoxContainer *hb_edit = memnew(HBoxContainer);
    add_child(hb_edit);
    hb_edit->set_v_size_flags(SIZE_EXPAND_FILL);

    uv_edit = memnew(Control);
    hb_edit->add_child(uv_edit);
    uv_edit->connect("gui_input",callable_mp(this, &ClassName::_uv_input));
    uv_edit->set_mouse_filter(MOUSE_FILTER_PASS);
    uv_edit->set_h_size_flags(SIZE_EXPAND_FILL);
    uv_edit->set_v_size_flags(SIZE_EXPAND_FILL);
    uv_edit->set_custom_minimum_size(Size2(get_theme_constant("sv_width"), get_theme_constant("sv_height")));
    uv_edit->connect("draw",callable_mp(this, &ClassName::_hsv_draw), make_binds(0, Variant(uv_edit)));

    w_edit = memnew(Control);
    hb_edit->add_child(w_edit);
    w_edit->set_custom_minimum_size(Size2(get_theme_constant("h_width"), 0));
    w_edit->set_h_size_flags(SIZE_FILL);
    w_edit->set_v_size_flags(SIZE_EXPAND_FILL);
    w_edit->connect("gui_input",callable_mp(this, &ClassName::_w_input));
    w_edit->connect("draw",callable_mp(this, &ClassName::_hsv_draw), make_binds(1, Variant(w_edit)));

    HBoxContainer *hb_smpl = memnew(HBoxContainer);
    add_child(hb_smpl);

    sample = memnew(TextureRect);
    hb_smpl->add_child(sample);
    sample->set_h_size_flags(SIZE_EXPAND_FILL);
    sample->connect("draw",callable_mp(this, &ClassName::_sample_draw));

    btn_pick = memnew(ToolButton);
    hb_smpl->add_child(btn_pick);
    btn_pick->set_toggle_mode(true);
    btn_pick->set_tooltip(TTR("Pick a color from the editor window."));
    btn_pick->connect("pressed",callable_mp(this, &ClassName::_screen_pick_pressed));

    VBoxContainer *vbl = memnew(VBoxContainer);
    add_child(vbl);

    add_child(memnew(HSeparator));

    VBoxContainer *vbr = memnew(VBoxContainer);
    add_child(vbr);
    vbr->set_h_size_flags(SIZE_EXPAND_FILL);

    for (int i = 0; i < 4; i++) {

        HBoxContainer *hbc = memnew(HBoxContainer);

        labels[i] = memnew(Label());
        labels[i]->set_custom_minimum_size(Size2(get_theme_constant("label_width"), 0));
        labels[i]->set_v_size_flags(SIZE_SHRINK_CENTER);
        hbc->add_child(labels[i]);

        scroll[i] = memnew(HSlider);
        scroll[i]->set_v_size_flags(SIZE_SHRINK_CENTER);
        scroll[i]->set_focus_mode(FOCUS_NONE);
        hbc->add_child(scroll[i]);

        values[i] = memnew(SpinBox);
        scroll[i]->share(values[i]);
        hbc->add_child(values[i]);
        values[i]->get_line_edit()->connect("focus_entered",callable_mp(this, &ClassName::_focus_enter));
        values[i]->get_line_edit()->connect("focus_exited",callable_mp(this, &ClassName::_focus_exit));

        scroll[i]->set_min(0);
        scroll[i]->set_page(0);
        scroll[i]->set_h_size_flags(SIZE_EXPAND_FILL);

        scroll[i]->connect("value_changed",callable_mp(this, &ClassName::_value_changed));

        vbr->add_child(hbc);
    }
    labels[3]->set_text("A");

    HBoxContainer *hhb = memnew(HBoxContainer);
    vbr->add_child(hhb);

    btn_hsv = memnew(CheckButton);
    hhb->add_child(btn_hsv);
    btn_hsv->set_text(TTR("HSV"));
    btn_hsv->connect("toggled",callable_mp(this, &ClassName::set_hsv_mode));

    btn_raw = memnew(CheckButton);
    hhb->add_child(btn_raw);
    btn_raw->set_text(TTR("Raw"));
    btn_raw->connect("toggled",callable_mp(this, &ClassName::set_raw_mode));

    text_type = memnew(Button);
    hhb->add_child(text_type);
    text_type->set_text("#");
    text_type->set_tooltip(TTR("Switch between hexadecimal and code values."));
    if (Engine::get_singleton()->is_editor_hint()) {

#ifdef TOOLS_ENABLED
        text_type->set_custom_minimum_size(Size2(28 * EDSCALE, 0)); // Adjust for the width of the "Script" icon.
#endif
        text_type->connect("pressed",callable_mp(this, &ClassName::_text_type_toggled));
    } else {

        text_type->set_flat(true);
        text_type->set_mouse_filter(MOUSE_FILTER_IGNORE);
    }

    c_text = memnew(LineEdit);
    hhb->add_child(c_text);
    c_text->set_h_size_flags(SIZE_EXPAND_FILL);
    c_text->connect("text_entered",callable_mp(this, &ClassName::_html_entered));
    c_text->connect("focus_entered",callable_mp(this, &ClassName::_focus_enter));
    c_text->connect("focus_exited",callable_mp(this, &ClassName::_html_focus_exit));

    _update_controls();
    updating = false;

    set_pick_color(Color(1, 1, 1));

    preset_separator = memnew(HSeparator);
    add_child(preset_separator);

    preset_container = memnew(HBoxContainer);
    preset_container->set_h_size_flags(SIZE_EXPAND_FILL);
    add_child(preset_container);

    preset = memnew(TextureRect);
    preset_container->add_child(preset);
    preset->connect("gui_input",callable_mp(this, &ClassName::_preset_input));
    preset->connect("draw",callable_mp(this, &ClassName::_update_presets));

    preset_container2 = memnew(HBoxContainer);
    preset_container2->set_h_size_flags(SIZE_EXPAND_FILL);
    add_child(preset_container2);
    bt_add_preset = memnew(Button);
    preset_container2->add_child(bt_add_preset);
    bt_add_preset->set_tooltip(TTR("Add current color as a preset."));
    bt_add_preset->connect("pressed",callable_mp(this, &ClassName::_add_preset_pressed));
}

/////////////////

void ColorPickerButton::_color_changed(const Color &p_color) {

    color = p_color;
    update();
    emit_signal("color_changed", color);
}

void ColorPickerButton::_modal_closed() {

    emit_signal("popup_closed");
}

void ColorPickerButton::pressed() {

    _update_picker();
    popup->set_position(get_global_position() - picker->get_combined_minimum_size() * get_global_transform().get_scale());
    popup->set_scale(get_global_transform().get_scale());
    popup->popup();
    picker->set_focus_on_line_edit();
}

void ColorPickerButton::_notification(int p_what) {

    switch (p_what) {
        case NOTIFICATION_DRAW: {

            const Ref<StyleBox> normal = get_theme_stylebox("normal");
            const Rect2 r = Rect2(normal->get_offset(), get_size() - normal->get_minimum_size());
            draw_texture_rect(Control::get_theme_icon("bg", "ColorPickerButton"), r, true);
            draw_rect(r, color);
            if (color.r > 1 || color.g > 1 || color.b > 1) {
                // Draw an indicator to denote that the color is "overbright" and can't be displayed accurately in the preview
                draw_texture(Control::get_theme_icon("overbright_indicator", "ColorPicker"), normal->get_offset());
            }
        } break;
        case MainLoop::NOTIFICATION_WM_QUIT_REQUEST: {

            if (popup)
                popup->hide();
        } break;
    }

    if (p_what == NOTIFICATION_VISIBILITY_CHANGED) {
        if (popup && !is_visible_in_tree()) {
            popup->hide();
        }
    }
}

void ColorPickerButton::set_pick_color(const Color &p_color) {

    color = p_color;
    if (picker) {
        picker->set_pick_color(p_color);
    }

    update();
}
Color ColorPickerButton::get_pick_color() const {

    return color;
}

void ColorPickerButton::set_edit_alpha(bool p_show) {

    edit_alpha = p_show;
    if (picker) {
        picker->set_edit_alpha(p_show);
    }
}

bool ColorPickerButton::is_editing_alpha() const {

    return edit_alpha;
}

ColorPicker *ColorPickerButton::get_picker() {

    _update_picker();
    return picker;
}

PopupPanel *ColorPickerButton::get_popup() {

    _update_picker();
    return popup;
}

void ColorPickerButton::_update_picker() {
    if (!picker) {
        popup = memnew(PopupPanel);
        picker = memnew(ColorPicker);
        popup->add_child(picker);
        add_child(popup);
        picker->connect("color_changed",callable_mp(this, &ClassName::_color_changed));
        popup->connect("modal_closed",callable_mp(this, &ClassName::_modal_closed));
        popup->connect("about_to_show",callable_mp((BaseButton *)this, &BaseButton::set_pressed), varray(true));
        popup->connect("popup_hide",callable_mp((BaseButton *)this, &BaseButton::set_pressed), varray(false));
        picker->set_pick_color(color);
        picker->set_edit_alpha(edit_alpha);
        emit_signal("picker_created");
    }
}

void ColorPickerButton::_bind_methods() {

    MethodBinder::bind_method(D_METHOD("set_pick_color", {"color"}), &ColorPickerButton::set_pick_color);
    MethodBinder::bind_method(D_METHOD("get_pick_color"), &ColorPickerButton::get_pick_color);
    MethodBinder::bind_method(D_METHOD("get_picker"), &ColorPickerButton::get_picker);
    MethodBinder::bind_method(D_METHOD("get_popup"), &ColorPickerButton::get_popup);
    MethodBinder::bind_method(D_METHOD("set_edit_alpha", {"show"}), &ColorPickerButton::set_edit_alpha);
    MethodBinder::bind_method(D_METHOD("is_editing_alpha"), &ColorPickerButton::is_editing_alpha);

    ADD_SIGNAL(MethodInfo("color_changed", PropertyInfo(VariantType::COLOR, "color")));
    ADD_SIGNAL(MethodInfo("popup_closed"));
    ADD_SIGNAL(MethodInfo("picker_created"));
    ADD_PROPERTY(PropertyInfo(VariantType::COLOR, "color"), "set_pick_color", "get_pick_color");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "edit_alpha"), "set_edit_alpha", "is_editing_alpha");
}

ColorPickerButton::ColorPickerButton() {

    // Initialization is now done deferred,
    // this improves performance in the inspector as the color picker
    // can be expensive to initialize.
    picker = nullptr;
    popup = nullptr;
    edit_alpha = true;

    set_toggle_mode(true);
}
