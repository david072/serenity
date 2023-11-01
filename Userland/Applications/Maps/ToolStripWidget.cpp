/*
 * Copyright (c) 2023, David Ganz <david.g.ganz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ToolStripWidget.h"
#include "Tools/MeasurementTool.h"
#include <LibGUI/BoxLayout.h>
#include <LibGfx/Orientation.h>

namespace Maps {

ToolStripWidget::ToolStripWidget()
    : m_active_tool(nullptr)
{
    set_fill_with_background_color(true);

    set_fixed_width(26);
    set_max_width(26);
    set_layout<GUI::VerticalBoxLayout>(2, 0);

    m_action_group.set_exclusive(true);
    m_action_group.set_unchecking_allowed(true);

    m_toolbar = add<GUI::Toolbar>(Gfx::Orientation::Vertical);
    m_toolbar->set_collapsible(true);

    setup_tools();
}

void ToolStripWidget::setup_tools()
{
    auto add_tool = [this](StringView icon_name, NonnullOwnPtr<Tool> tool) -> ErrorOr<void> {
        auto action = GUI::Action::create_checkable(
            tool->tool_name(),
            TRY(Gfx::Bitmap::load_from_file(TRY(String::formatted("/res/icons/16x16/{}.png", icon_name)))),
            [this, tool = tool.ptr()](auto& action) {
                if (action.is_checked()) {
                    if (on_tool_selection.has_value())
                        on_tool_selection.value()(tool);
                    m_active_tool = tool;
                } else {
                    if (on_tool_selection.has_value())
                        on_tool_selection.value()(nullptr);
                    m_active_tool = nullptr;
                }
            });

        m_action_group.add_action(action);
        m_toolbar->add_action(action);
        m_tools.append(move(tool));
        return {};
    };

    MUST(add_tool("app-hello-world"sv, make<MeasurementTool>()));
}

}
