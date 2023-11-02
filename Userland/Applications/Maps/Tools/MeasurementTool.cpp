/*
 * Copyright (c) 2023, David Ganz <david.g.ganz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "MeasurementTool.h"

namespace Maps {

Tool::EventResult MeasurementTool::doubleclick_event(GUI::MouseEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult MeasurementTool::mousedown_event(GUI::MouseEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult MeasurementTool::mouseup_event(GUI::MouseEvent&, MapWidget&)
{
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

Tool::EventResult MeasurementTool::mousemove_event(GUI::MouseEvent& event, MapWidget&)
{
    m_mouse_pos = event.position();
    return Tool::EventResult::Update;
}

void MeasurementTool::paint_event(GUI::PaintEvent&, MapWidget&, GUI::Painter& painter)
{
    painter.draw_rect(Gfx::IntRect(m_mouse_pos.x() - 10, m_mouse_pos.y() - 10, 20, 20), Gfx::Color::Black);
}

}
