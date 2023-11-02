/*
 * Copyright (c) 2023, David Ganz <david.g.ganz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "MeasurementTool.h"
#include <LibGUI/Application.h>
#include <LibGUI/BoxLayout.h>
#include <LibGfx/Path.h>

namespace Maps {

static constexpr int HOVERED_CIRLE_RADIUS = 6;
static constexpr Color FINAL_COLOR = Gfx::Color::Yellow;
static constexpr Color CHOOSING_COLOR = Gfx::Color::LightGray;

MeasurementToolUI::MeasurementToolUI()
{
    set_frame_style(Gfx::FrameStyle::RaisedPanel);
    set_layout<GUI::VerticalBoxLayout>(GUI::Margins(5));

    m_perimeter_label = add<GUI::Label>();
    m_perimeter_label->set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_perimeter_label->set_fixed_height(25);

    m_perimeter_box = add<GUI::TextBox>();
    m_perimeter_box->set_mode(GUI::TextBox::Mode::ReadOnly);

    set_is_shape(false);
    set_perimeter_length(0);

    update();
}

void MeasurementToolUI::set_is_shape(bool is_shape)
{
    m_is_shape = is_shape;
    if (m_is_shape)
        m_perimeter_label->set_text("Perimeter:"_string);
    else
        m_perimeter_label->set_text("Length:"_string);
}

void MeasurementToolUI::set_perimeter_length(double length)
{
    m_perimeter_box->set_text(String::formatted("{:.3} m", length).release_value_but_fixme_should_propagate_errors());
}

NonnullRefPtr<GUI::Widget> MeasurementTool::tool_ui()
{
    auto widget = MeasurementToolUI::try_create().release_value_but_fixme_should_propagate_errors();
    m_ui = widget;
    return widget;
}

Tool::EventResult MeasurementTool::doubleclick_event(GUI::MouseEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult MeasurementTool::mousedown_event(GUI::MouseEvent&, MapWidget&)
{
    m_mouse_down = true;
    return Tool::EventResult::None;
}

Tool::EventResult MeasurementTool::mouseup_event(GUI::MouseEvent& event, MapWidget& map)
{
    m_mouse_down = false;

    if (m_adding_points) {
        if (m_is_shape)
            m_adding_points = false;
        else
            m_points.append(make<Point>(map.y_to_latitude(event.y()), map.x_to_longitude(event.x())));

        return Tool::EventResult::Update;
    }

    return Tool::EventResult::None;
}

Tool::EventResult MeasurementTool::mousewheel_event(GUI::MouseEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult MeasurementTool::context_menu_event(GUI::ContextMenuEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult MeasurementTool::mousemove_event(GUI::MouseEvent& event, MapWidget& map)
{
    m_mouse_pos = event.position();

    if (m_adding_points) {
        m_is_shape = m_points.size() > 2 && m_points.first()->is_hovered_by(event.position(), map);
        return Tool::EventResult::Update;
    }

    if (is_dragging()) {
        m_hovered_point->set_position(event.position(), map);
        return Tool::EventResult::SuppressAndUpdate;
    }

    m_hovered_point.clear();

    bool did_change = false;
    if (!m_mouse_down) {
        for (auto& point : m_points) {
            auto hovered = point->is_hovered_by(event.position(), map);
            if (point->hovered != hovered) {
                point->hovered = hovered;
                did_change = true;
            }

            if (point->hovered)
                m_hovered_point = point->make_weak_ptr();
        }
    }

    return did_change ? Tool::EventResult::Update : Tool::EventResult::None;
}

Tool::EventResult MeasurementTool::keyup_event(GUI::KeyEvent& event, MapWidget&)
{
    switch (event.key()) {
    case Key_Escape:
        m_points.clear();
        m_adding_points = true;
        m_is_shape = false;
        return Tool::EventResult::Update;
    case Key_Return:
        m_adding_points = false;
        return Tool::EventResult::Update;
    default:
        return Tool::EventResult::None;
    }
}

Tool::EventResult MeasurementTool::keydown_event(GUI::KeyEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

void MeasurementTool::paint_event(GUI::PaintEvent&, MapWidget& map, GUI::Painter& painter)
{
    if (m_ui)
        m_ui->set_is_shape(m_is_shape);
    update_perimeter_length();

    auto draw_circle = [&painter](Gfx::IntPoint center, int radius, Gfx::Color color) {
        auto rect = Gfx::IntRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
        painter.fill_rect_with_rounded_corners(rect, color, rect.width() / 2);
    };

    if (m_is_shape && !m_points.is_empty()) {
        // draw here to make sure the line is below the points
        painter.draw_line(m_points.last()->to_pixel_coords(map), m_points.first()->to_pixel_coords(map), FINAL_COLOR);

        if (m_adding_points)
            GUI::Application::the()->show_tooltip_immediately("Close Shape"_string, &map);
    } else {
        GUI::Application::the()->hide_tooltip();
    }

    if (m_points.is_empty())
        return;

    Gfx::Path path;
    path.move_to(Gfx::FloatPoint(m_points.first()->to_pixel_coords(map)));

    for (size_t i = 0; i < m_points.size(); i++) {
        auto& point = m_points[i];
        auto pos = point->to_pixel_coords(map);
        if (i > 0)
            painter.draw_line(m_points[i - 1]->to_pixel_coords(map), pos, FINAL_COLOR);

        if (i == m_points.size() - 1 && m_adding_points && !m_is_shape)
            painter.draw_line(pos, m_mouse_pos, CHOOSING_COLOR);

        draw_circle(pos, !point->hovered ? CIRCLE_RADIUS : HOVERED_CIRLE_RADIUS, FINAL_COLOR);

        if (m_is_shape)
            path.line_to(Gfx::FloatPoint(pos));
    }

    if (m_is_shape) {
        path.close();
        painter.fill_path(path, FINAL_COLOR.with_opacity(0.2));
    }

    if (m_adding_points && !m_is_shape)
        draw_circle(m_mouse_pos, CIRCLE_RADIUS, CHOOSING_COLOR);
}

void MeasurementTool::update_perimeter_length()
{
    if (!m_ui)
        return;

    if (m_points.is_empty()) {
        m_ui->set_perimeter_length(0);
        return;
    }

    double length = 0;
    for (size_t i = 1; i < m_points.size(); i++) {
        length += m_points[i - 1]->coordinate.distance_to(m_points[i]->coordinate);
    }

    if (m_is_shape)
        length += m_points.last()->coordinate.distance_to(m_points.first()->coordinate);

    m_ui->set_perimeter_length(length);
}

}
