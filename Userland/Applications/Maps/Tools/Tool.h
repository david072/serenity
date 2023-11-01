/*
 * Copyright (c) 2023, David Ganz <david.g.ganz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGUI/ActionGroup.h>
#include <LibGUI/Event.h>
#include <LibGUI/Painter.h>
#include <LibGUI/Toolbar.h>
#include <LibGUI/Widget.h>

namespace Maps {

class Tool {
public:
    enum class EventResult {
        None,
        Update
    };

    virtual ~Tool() = default;

    virtual EventResult doubleclick_event(GUI::MouseEvent&);
    virtual EventResult mousemove_event(GUI::MouseEvent&);
    virtual EventResult mousedown_event(GUI::MouseEvent&);
    virtual EventResult mouseup_event(GUI::MouseEvent&);
    virtual EventResult mousewheel_event(GUI::MouseEvent&);
    virtual EventResult context_menu_event(GUI::ContextMenuEvent&);
    virtual void paint_event(GUI::PaintEvent&, GUI::Painter&);

    virtual StringView tool_name() const = 0;

protected:
    Tool();
};

}
