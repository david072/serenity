/*
 * Copyright (c) 2023, David Ganz <david.g.ganz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "MeasurementTool.h"

namespace Maps {

static constexpr int CIRCLE_RADIUS = 4;
static constexpr Color FINAL_COLOR = Gfx::Color::Yellow;
static constexpr Color CHOOSING_COLOR = Gfx::Color::LightGray;

Tool::EventResult MeasurementTool::doubleclick_event(GUI::MouseEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult MeasurementTool::mousedown_event(GUI::MouseEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult MeasurementTool::mouseup_event(GUI::MouseEvent& event, MapWidget& map)
{
    if (!m_adding_points)
        return Tool::EventResult::None;

    m_points.append(Point(map.y_to_latitude(event.y()), map.x_to_longitude(event.x())));
    return Tool::EventResult::Update;
}

Tool::EventResult MeasurementTool::mousewheel_event(GUI::MouseEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult MeasurementTool::context_menu_event(GUI::ContextMenuEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult MeasurementTool::mousemove_event(GUI::MouseEvent& event, MapWidget&)
{
    m_mouse_pos = event.position();
    return Tool::EventResult::Update;
}

Tool::EventResult MeasurementTool::keyup_event(GUI::KeyEvent& event, MapWidget&)
{
    switch (event.key()) {
    case Key_Escape:
        m_points.clear();
        m_adding_points = true;
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
    auto draw_circle = [&painter](Gfx::IntPoint center, int radius, Gfx::Color color) {
        auto rect = Gfx::IntRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
        painter.fill_rect_with_rounded_corners(rect, color, rect.width() / 2);
    };

    for (size_t i = 0; i < m_points.size(); i++) {
        auto point = m_points[i].to_pixel_coords(map);
        if (i > 0)
            painter.draw_line(m_points[i - 1].to_pixel_coords(map), point, FINAL_COLOR);

        if (i == m_points.size() - 1 && m_adding_points)
            painter.draw_line(point, m_mouse_pos, CHOOSING_COLOR);

        draw_circle(point, CIRCLE_RADIUS, FINAL_COLOR);
    }

    if (m_adding_points)
        draw_circle(m_mouse_pos, CIRCLE_RADIUS, CHOOSING_COLOR);
}

}
