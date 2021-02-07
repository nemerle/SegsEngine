/*************************************************************************/
/*  editor_profiler.cpp                                                  */
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

#include "editor_profiler.h"

#include "core/callable_method_pointer.h"
#include "core/method_bind.h"
#include "core/os/os.h"
#include "core/translation_helpers.h"
#include "editor_scale.h"
#include "editor_settings.h"

IMPL_GDCLASS(EditorProfiler)

void EditorProfiler::_make_metric_ptrs(Metric &m) {

    for (int i = 0; i < m.categories.size(); i++) {
        m.category_ptrs[m.categories[i].signature] = &m.categories[i];
        for (int j = 0; j < m.categories[i].items.size(); j++) {
            m.item_ptrs[m.categories[i].items[j].signature] = &m.categories[i].items[j];
        }
    }
}

void EditorProfiler::add_frame_metric(const Metric &p_metric, bool p_final) {

    ++last_metric;
    if (last_metric >= frame_metrics.size())
        last_metric = 0;

    frame_metrics[last_metric] = p_metric;
    _make_metric_ptrs(frame_metrics[last_metric]);

    updating_frame = true;
    cursor_metric_edit->set_max(frame_metrics[last_metric].frame_number);
    cursor_metric_edit->set_min(M_MAX(frame_metrics[last_metric].frame_number - frame_metrics.size(), 0));

    if (!seeking) {
        cursor_metric_edit->set_value(frame_metrics[last_metric].frame_number);
        if (hover_metric != -1) {
            hover_metric++;
            if (hover_metric >= frame_metrics.size()) {
                hover_metric = 0;
            }
        }
    }
    updating_frame = false;

    if (frame_delay->is_stopped()) {

        frame_delay->set_wait_time(p_final ? 0.1 : 1);
        frame_delay->start();
    }

    if (plot_delay->is_stopped()) {
        plot_delay->set_wait_time(0.1);
        plot_delay->start();
    }
}

void EditorProfiler::clear() {

    int metric_size = EditorSettings::get_singleton()->getT<int>("debugger/profiler_frame_history_size");
    metric_size = CLAMP(metric_size, 60, 1024);
    frame_metrics.clear();
    frame_metrics.resize(metric_size);
    last_metric = -1;
    variables->clear();
    plot_sigs.clear();
    plot_sigs.insert("physics_frame_time");
    plot_sigs.insert("category_frame_time");

    updating_frame = true;
    cursor_metric_edit->set_min(0);
    cursor_metric_edit->set_max(100); // Doesn't make much sense, but we can't have min == max. Doesn't hurt.
    cursor_metric_edit->set_value(0);
    updating_frame = false;
    hover_metric = -1;
    seeking = false;
}

static String _get_percent_txt(float p_value, float p_total) {
    if (p_total == 0.0f) {
        p_total = 0.00001f;
    }
    return String(StringUtils::num(p_value / p_total * 100, 1) + "%");
}

String EditorProfiler::_get_time_as_text(const Metric &m, float p_time, int p_calls) {

    const int dmode = display_mode->get_selected();

    if (dmode == DISPLAY_FRAME_TIME) {
        return StringUtils::pad_decimals(rtos(p_time*1000),2) + " ms";
    } else if (dmode == DISPLAY_AVERAGE_TIME) {
        if (p_calls == 0)
            return "0.00 ms";
        else
            return StringUtils::pad_decimals(rtos(1000*(p_time / p_calls)),2)+" ms";
    } else if (dmode == DISPLAY_FRAME_PERCENT) {
        return _get_percent_txt(p_time, m.frame_time);
    } else if (dmode == DISPLAY_PHYSICS_FRAME_PERCENT) {

        return _get_percent_txt(p_time, m.physics_frame_time);
    }

    return "err";
}

Color EditorProfiler::_get_color_from_signature(const StringName &p_signature) const {

    Color bc = get_theme_color("error_color", "Editor");
    double rot = ABS(double(p_signature.hash()) / double(0x7FFFFFFF));
    Color c;
    c.set_hsv(rot, bc.get_s(), bc.get_v());
    return c.linear_interpolate(get_theme_color("base_color", "Editor"), 0.07f);
}

void EditorProfiler::_item_edited() {

    if (updating_frame)
        return;

    TreeItem *item = variables->get_edited();
    if (!item)
        return;
    StringName signature = item->get_metadata(0).as<StringName>();
    bool checked = item->is_checked(0);

    if (checked)
        plot_sigs.insert(signature);
    else
        plot_sigs.erase(signature);

    if (!frame_delay->is_processing()) {
        frame_delay->set_wait_time(0.1f);
        frame_delay->start();
    }

    _update_plot();
}

void EditorProfiler::_update_plot() {

    const int w = graph->get_size().width;
    const int h = graph->get_size().height;
    const int desired_len = w * h * 4;

    bool reset_texture = false;


    if (graph_image.size() != desired_len) {
        reset_texture = true;
        graph_image.resize(desired_len);
    }

    PoolVector<uint8_t>::Write wr = graph_image.write();
    const Color background_color = get_theme_color("dark_color_2", "Editor");
    const uint8_t clr_val[4] = {
        (uint8_t)Math::fast_ftoi(background_color.r * 255),
        (uint8_t)Math::fast_ftoi(background_color.g * 255),
        (uint8_t)Math::fast_ftoi(background_color.b * 255),
        255
    };
    // Clear the previous frame and set the background color.
    for (int i = 0; i < desired_len; i += 4) {
        wr[i + 0] = clr_val[0];
        wr[i + 1] = clr_val[1];
        wr[i + 2] = clr_val[2];
        wr[i + 3] = clr_val[3];
    }

    //find highest value

    bool use_self = display_time->get_selected() == DISPLAY_SELF_TIME;
    float highest = 0;

    for (int i = 0; i < frame_metrics.size(); i++) {
        const Metric &m = frame_metrics[i];
        if (!m.valid)
            continue;

        for (const StringName &E : plot_sigs) {

            auto F = m.category_ptrs.find(E);
            if (F!=m.category_ptrs.end()) {
                highest = M_MAX(F->second->total_time, highest);
            }

            auto G = m.item_ptrs.find(E);
            if (G!=m.item_ptrs.end()) {
                if (use_self) {
                    highest = M_MAX(G->second->self, highest);
                } else {
                    highest = M_MAX(G->second->total, highest);
                }
            }
        }
    }

    if (highest > 0) {
        //means some data exists..
        highest *= 1.2; //leave some upper room
        graph_height = highest;

        Vector<int> columnv;
        columnv.resize(h * 4);

        int *column = columnv.data();

        Map<StringName, int> plot_prev;
        //Map<StringName,int> plot_max;

        for (int i = 0; i < w; i++) {

            for (int j = 0; j < h * 4; j++) {
                column[j] = 0;
            }

            int current = i * frame_metrics.size() / w;
            int next = (i + 1) * frame_metrics.size() / w;
            if (next > frame_metrics.size()) {
                next = frame_metrics.size();
            }
            if (next == current)
                next = current + 1; //just because for loop must work

            for (const StringName &E : plot_sigs) {

                int plot_pos = -1;

                for (int j = current; j < next; j++) {

                    //wrap
                    int idx = last_metric + 1 + j;
                    while (idx >= frame_metrics.size()) {
                        idx -= frame_metrics.size();
                    }

                    //get
                    const Metric &m = frame_metrics[idx];
                    if (!m.valid)
                        continue; //skip because invalid

                    float value = 0;

                    auto F = m.category_ptrs.find(E);
                    if (F!=m.category_ptrs.end()) {
                        value = F->second->total_time;
                    }

                    const auto G = m.item_ptrs.find(E);
                    if (G!=m.item_ptrs.end()) {
                        if (use_self) {
                            value = G->second->self;
                        } else {
                            value = G->second->total;
                        }
                    }

                    plot_pos = M_MAX(CLAMP(int(value * h / highest), 0, h - 1), plot_pos);
                }

                int prev_plot = plot_pos;
                Map<StringName, int>::iterator H = plot_prev.find(E);
                if (H!=plot_prev.end()) {
                    prev_plot = H->second;
                    H->second = plot_pos;
                } else {
                    plot_prev[E] = plot_pos;
                }

                if (plot_pos == -1 && prev_plot == -1) {
                    //don't bother drawing
                    continue;
                }

                if (prev_plot != -1 && plot_pos == -1) {

                    plot_pos = prev_plot;
                }

                if (prev_plot == -1 && plot_pos != -1) {
                    prev_plot = plot_pos;
                }

                plot_pos = h - plot_pos - 1;
                prev_plot = h - prev_plot - 1;

                if (prev_plot > plot_pos) {
                    SWAP(prev_plot, plot_pos);
                }

                Color col = _get_color_from_signature(E);

                for (int j = prev_plot; j <= plot_pos; j++) {

                    column[j * 4 + 0] += Math::fast_ftoi(CLAMP(col.r * 255, 0, 255));
                    column[j * 4 + 1] += Math::fast_ftoi(CLAMP(col.g * 255, 0, 255));
                    column[j * 4 + 2] += Math::fast_ftoi(CLAMP(col.b * 255, 0, 255));
                    column[j * 4 + 3] += 1;
                }
            }

            for (int j = 0; j < h * 4; j += 4) {

                int a = column[j + 3];
                if (a > 0) {
                    column[j + 0] /= a;
                    column[j + 1] /= a;
                    column[j + 2] /= a;
                }

                const uint8_t red = uint8_t(column[j + 0]);
                const uint8_t green = uint8_t(column[j + 1]);
                const uint8_t blue = uint8_t(column[j + 2]);
                const bool is_filled = red >= 1 || green >= 1 || blue >= 1;
                const int widx = ((j >> 2) * w + i) * 4;

                // If the pixel isn't filled by any profiler line, apply the background color instead.
                wr[widx + 0] = is_filled ? red : clr_val[0];
                wr[widx + 1] = is_filled ? green : clr_val[1];
                wr[widx + 2] = is_filled ? blue : clr_val[2];
                wr[widx + 3] = 255;
            }
        }
    }

    wr.release();

    Ref<Image> img(make_ref_counted<Image>());
    img->create(w, h, false, ImageData::FORMAT_RGBA8, graph_image);

    if (reset_texture) {
        if (not graph_texture) {
            graph_texture = make_ref_counted<ImageTexture>();
        }
        graph_texture->create(img->get_width(), img->get_height(), img->get_format(), Texture::FLAG_VIDEO_SURFACE);
    }

    graph_texture->set_data(img);

    graph->set_texture(graph_texture);
    graph->update();
}

void EditorProfiler::_update_frame() {

    int cursor_metric = _get_cursor_index();

    ERR_FAIL_INDEX(cursor_metric, frame_metrics.size());

    updating_frame = true;
    variables->clear();

    TreeItem *root = variables->create_item();
    const Metric &m = frame_metrics[cursor_metric];

    int dtime = display_time->get_selected();

    for (int i = 0; i < m.categories.size(); i++) {

        TreeItem *category = variables->create_item(root);
        category->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
        category->set_editable(0, true);
        category->set_metadata(0, m.categories[i].signature);
        category->set_text_utf8(0, m.categories[i].name);
        category->set_text_utf8(1, _get_time_as_text(m, m.categories[i].total_time, 1));

        if (plot_sigs.contains(m.categories[i].signature)) {
            category->set_checked(0, true);
            category->set_custom_color(0, _get_color_from_signature(m.categories[i].signature));
        }

        for (int j = m.categories[i].items.size() - 1; j >= 0; j--) {
            const Metric::Category::Item &it = m.categories[i].items[j];

            TreeItem *item = variables->create_item(category);
            item->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
            item->set_editable(0, true);
            item->set_text_utf8(0, it.name);
            item->set_metadata(0, it.signature);
            item->set_metadata(1, it.script);
            item->set_metadata(2, it.line);
            item->set_text_align(2, TreeItem::ALIGN_RIGHT);
            item->set_tooltip(0, StringName(it.script + ":" + itos(it.line)));

            float time = dtime == DISPLAY_SELF_TIME ? it.self : it.total;

            item->set_text_utf8(1, _get_time_as_text(m, time, it.calls));

            item->set_text_utf8(2, itos(it.calls));

            if (plot_sigs.contains(it.signature)) {
                item->set_checked(0, true);
                item->set_custom_color(0, _get_color_from_signature(it.signature));
            }
        }
    }

    updating_frame = false;
}

void EditorProfiler::_activate_pressed() {

    if (activate->is_pressed()) {
        activate->set_button_icon(get_theme_icon("Stop", "EditorIcons"));
        activate->set_text(TTR("Stop"));
    } else {
        activate->set_button_icon(get_theme_icon("Play", "EditorIcons"));
        activate->set_text(TTR("Start"));
    }
    emit_signal("enable_profiling", activate->is_pressed());
}

void EditorProfiler::_clear_pressed() {

    clear();
    _update_plot();
}

void EditorProfiler::_notification(int p_what) {

    if (p_what == NOTIFICATION_ENTER_TREE) {
        activate->set_button_icon(get_theme_icon("Play", "EditorIcons"));
        clear_button->set_button_icon(get_theme_icon("Clear", "EditorIcons"));
    }
}

void EditorProfiler::_graph_tex_draw() {

    if (last_metric < 0)
        return;
    if (seeking) {

        int max_frames = frame_metrics.size();
        int frame = cursor_metric_edit->get_value() - (frame_metrics[last_metric].frame_number - max_frames + 1);
        if (frame < 0)
            frame = 0;

        int cur_x = frame * graph->get_size().x / max_frames;

        graph->draw_line(Vector2(cur_x, 0), Vector2(cur_x, graph->get_size().y), Color(1, 1, 1, 0.8));
    }

    if (hover_metric != -1 && frame_metrics[hover_metric].valid) {

        int max_frames = frame_metrics.size();
        int frame = frame_metrics[hover_metric].frame_number - (frame_metrics[last_metric].frame_number - max_frames + 1);
        if (frame < 0)
            frame = 0;

        int cur_x = frame * graph->get_size().x / max_frames;

        graph->draw_line(Vector2(cur_x, 0), Vector2(cur_x, graph->get_size().y), Color(1, 1, 1, 0.4));
    }
}

void EditorProfiler::_graph_tex_mouse_exit() {

    hover_metric = -1;
    graph->update();
}

void EditorProfiler::_cursor_metric_changed(double) {
    if (updating_frame)
        return;

    graph->update();
    _update_frame();
}

void EditorProfiler::_graph_tex_input(const Ref<InputEvent> &p_ev) {

    if (last_metric < 0)
        return;

    Ref<InputEventMouse> me = dynamic_ref_cast<InputEventMouse>(p_ev);
    Ref<InputEventMouseButton> mb = dynamic_ref_cast<InputEventMouseButton>(p_ev);
    Ref<InputEventMouseMotion> mm = dynamic_ref_cast<InputEventMouseMotion>(p_ev);

    if ( (mb && mb->get_button_index() == BUTTON_LEFT && mb->is_pressed()) || mm) {

        int x = me->get_position().x;
        x = x * frame_metrics.size() / graph->get_size().width;

        bool show_hover = x >= 0 && x < frame_metrics.size();

        if (x < 0) {
            x = 0;
        }

        if (x >= frame_metrics.size()) {
            x = frame_metrics.size() - 1;
        }

        int metric = frame_metrics.size() - x - 1;
        metric = last_metric - metric;
        while (metric < 0) {
            metric += frame_metrics.size();
        }

        if (show_hover) {

            hover_metric = metric;

        } else {
            hover_metric = -1;
        }

        if (mb || mm->get_button_mask() & BUTTON_MASK_LEFT) {
            //cursor_metric=x;
            updating_frame = true;

            //metric may be invalid, so look for closest metric that is valid, this makes snap feel better
            bool valid = false;
            for (int i = 0; i < frame_metrics.size(); i++) {

                if (frame_metrics[metric].valid) {
                    valid = true;
                    break;
                }

                metric++;
                if (metric >= frame_metrics.size())
                    metric = 0;
            }

            if (valid)
                cursor_metric_edit->set_value(frame_metrics[metric].frame_number);

            updating_frame = false;

            if (activate->is_pressed()) {
                if (!seeking) {
                    emit_signal("break_request");
                }
            }

            seeking = true;

            if (!frame_delay->is_processing()) {
                frame_delay->set_wait_time(0.1);
                frame_delay->start();
            }
        }

        graph->update();
    }
}

int EditorProfiler::_get_cursor_index() const {

    if (last_metric < 0)
        return 0;
    if (!frame_metrics[last_metric].valid)
        return 0;

    int diff = frame_metrics[last_metric].frame_number - cursor_metric_edit->get_value();

    int idx = last_metric - diff;
    while (idx < 0) {
        idx += frame_metrics.size();
    }

    return idx;
}

void EditorProfiler::disable_seeking() {

    seeking = false;
    graph->update();
}

void EditorProfiler::_combo_changed(int) {

    _update_frame();
    _update_plot();
}

void EditorProfiler::_bind_methods() {
    ADD_SIGNAL(MethodInfo("enable_profiling", PropertyInfo(VariantType::BOOL, "enable")));
    ADD_SIGNAL(MethodInfo("break_request"));
}

void EditorProfiler::set_enabled(bool p_enable) {

    activate->set_disabled(!p_enable);
}

bool EditorProfiler::is_profiling() {
    return activate->is_pressed();
}

Vector<Vector<String> > EditorProfiler::get_data_as_csv() const {
    Vector<Vector<String> > res;

    if (frame_metrics.empty()) {
        return res;
    }

    // signatures
    Vector<String> signatures;
    const Vector<EditorProfiler::Metric::Category> &categories = frame_metrics[0].categories;

    for (int j = 0; j < categories.size(); j++) {

        const EditorProfiler::Metric::Category &c = categories[j];
        signatures.emplace_back(c.signature);

        for (int k = 0; k < c.items.size(); k++) {
            signatures.emplace_back(c.items[k].signature);
        }
    }
    res.push_back(signatures);

    // values
    Vector<String> values;
    values.resize(signatures.size());

    int index = last_metric;

    for (int i = 0; i < frame_metrics.size(); i++) {

        ++index;

        if (index >= frame_metrics.size()) {
            index = 0;
        }

        if (!frame_metrics[index].valid) {
            continue;
        }
        int it = 0;
        const Vector<EditorProfiler::Metric::Category> &frame_cat = frame_metrics[index].categories;

        for (int j = 0; j < frame_cat.size(); j++) {

            const EditorProfiler::Metric::Category &c = frame_cat[j];
            values[it++] = StringUtils::num_real(c.total_time);

            for (int k = 0; k < c.items.size(); k++) {
                values[it++] = StringUtils::num_real(c.items[k].total);
            }
        }
        res.emplace_back(values);
    }

    return res;
}

EditorProfiler::EditorProfiler() {

    HBoxContainer *hb = memnew(HBoxContainer);
    add_child(hb);
    activate = memnew(Button);
    activate->set_toggle_mode(true);
    activate->set_text(TTR("Start"));
    activate->connect("pressed",callable_mp(this, &ClassName::_activate_pressed));
    hb->add_child(activate);

    clear_button = memnew(Button);
    clear_button->set_text(TTR("Clear"));
    clear_button->connect("pressed",callable_mp(this, &ClassName::_clear_pressed));
    hb->add_child(clear_button);

    hb->add_child(memnew(Label(TTR("Measure:"))));

    display_mode = memnew(OptionButton);
    display_mode->add_item(TTR("Frame Time (sec)"));
    display_mode->add_item(TTR("Average Time (sec)"));
    display_mode->add_item(TTR("Frame %"));
    display_mode->add_item(TTR("Physics Frame %"));
    display_mode->connect("item_selected",callable_mp(this, &ClassName::_combo_changed));

    hb->add_child(display_mode);

    hb->add_child(memnew(Label(TTR("Time:"))));

    display_time = memnew(OptionButton);
    display_time->add_item(TTR("Inclusive"));
    display_time->add_item(TTR("Self"));
    display_time->connect("item_selected",callable_mp(this, &ClassName::_combo_changed));

    hb->add_child(display_time);

    hb->add_spacer();

    hb->add_child(memnew(Label(TTR("Frame #:"))));

    cursor_metric_edit = memnew(SpinBox);
    cursor_metric_edit->set_h_size_flags(SIZE_FILL);
    hb->add_child(cursor_metric_edit);
    cursor_metric_edit->connect("value_changed",callable_mp(this, &ClassName::_cursor_metric_changed));

    hb->add_constant_override("separation", 8 * EDSCALE);

    h_split = memnew(HSplitContainer);
    add_child(h_split);
    h_split->set_v_size_flags(SIZE_EXPAND_FILL);

    variables = memnew(Tree);
    variables->set_custom_minimum_size(Size2(320, 0) * EDSCALE);
    variables->set_hide_folding(true);
    h_split->add_child(variables);
    variables->set_hide_root(true);
    variables->set_columns(3);
    variables->set_column_titles_visible(true);
    variables->set_column_title(0, TTR("Name"));
    variables->set_column_expand(0, true);
    variables->set_column_min_width(0, 60 * EDSCALE);
    variables->set_column_title(1, TTR("Time"));
    variables->set_column_expand(1, false);
    variables->set_column_min_width(1, 100 * EDSCALE);
    variables->set_column_title(2, TTR("Calls"));
    variables->set_column_expand(2, false);
    variables->set_column_min_width(2, 60 * EDSCALE);
    variables->connect("item_edited",callable_mp(this, &ClassName::_item_edited));

    graph = memnew(TextureRect);
    graph->set_expand(true);
    graph->set_mouse_filter(MOUSE_FILTER_STOP);
    graph->connect("draw",callable_mp(this, &ClassName::_graph_tex_draw));
    graph->connect("gui_input",callable_mp(this, &ClassName::_graph_tex_input));
    graph->connect("mouse_exited",callable_mp(this, &ClassName::_graph_tex_mouse_exit));

    h_split->add_child(graph);
    graph->set_h_size_flags(SIZE_EXPAND_FILL);

    int metric_size = CLAMP(EDITOR_DEF_T<int>("debugger/profiler_frame_history_size", 600), 60, 1024);
    frame_metrics.resize(metric_size);
    last_metric = -1;
    hover_metric = -1;

    EDITOR_DEF("debugger/profiler_frame_max_functions", 512);

    frame_delay = memnew(Timer);
    frame_delay->set_wait_time(0.1);
    frame_delay->set_one_shot(true);
    add_child(frame_delay);
    frame_delay->connect("timeout",callable_mp(this, &ClassName::_update_frame));

    plot_delay = memnew(Timer);
    plot_delay->set_wait_time(0.1);
    plot_delay->set_one_shot(true);
    add_child(plot_delay);
    plot_delay->connect("timeout",callable_mp(this, &ClassName::_update_plot));

    plot_sigs.insert("physics_frame_time");
    plot_sigs.insert("category_frame_time");

    seeking = false;
    graph_height = 1;
}
