/*
 * Copyright (c) 2023, David Ganz <david.g.ganz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Tool.h"
#include <LibGfx/Forward.h>

namespace Maps {

class MeasurementTool : public Tool {
public:
    MeasurementTool() = default;

    virtual ~MeasurementTool() = default;

    virtual StringView tool_name() const override { return "Measure"sv; }

    virtual Tool::EventResult doubleclick_event(GUI::MouseEvent&) override;
    virtual Tool::EventResult mousemove_event(GUI::MouseEvent&) override;
    virtual Tool::EventResult mousedown_event(GUI::MouseEvent&) override;
    virtual Tool::EventResult mouseup_event(GUI::MouseEvent&) override;
    virtual Tool::EventResult mousewheel_event(GUI::MouseEvent&) override;
    virtual Tool::EventResult context_menu_event(GUI::ContextMenuEvent&) override;
    virtual void paint_event(GUI::PaintEvent&, GUI::Painter&) override;

private:
    Gfx::IntPoint m_mouse_pos;
};

}
