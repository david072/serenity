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

class MapWidget;

class Tool {
public:
    enum class EventResult {
        None,
        Update
    };

    virtual ~Tool() = default;

    virtual EventResult doubleclick_event(GUI::MouseEvent&, MapWidget&);
    virtual EventResult mousemove_event(GUI::MouseEvent&, MapWidget&);
    virtual EventResult mousedown_event(GUI::MouseEvent&, MapWidget&);
    virtual EventResult mouseup_event(GUI::MouseEvent&, MapWidget&);
    virtual EventResult mousewheel_event(GUI::MouseEvent&, MapWidget&);
    virtual EventResult context_menu_event(GUI::ContextMenuEvent&, MapWidget&);
    virtual void paint_event(GUI::PaintEvent&, MapWidget&, GUI::Painter&);

    virtual StringView tool_name() const = 0;

protected:
    Tool();
};

}
