/*
 * Copyright (c) 2023, David Ganz <david.g.ganz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Tool.h"
#include <LibGUI/Action.h>

namespace Maps {

Tool::Tool()
{
}

Tool::EventResult Tool::doubleclick_event(GUI::MouseEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult Tool::mousemove_event(GUI::MouseEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult Tool::mousedown_event(GUI::MouseEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult Tool::mouseup_event(GUI::MouseEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult Tool::mousewheel_event(GUI::MouseEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult Tool::context_menu_event(GUI::ContextMenuEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult Tool::keyup_event(GUI::KeyEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

Tool::EventResult Tool::keydown_event(GUI::KeyEvent&, MapWidget&)
{
    return Tool::EventResult::None;
}

void Tool::paint_event(GUI::PaintEvent&, MapWidget&, GUI::Painter&)
{
}

}
