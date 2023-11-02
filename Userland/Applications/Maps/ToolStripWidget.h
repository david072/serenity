/*
 * Copyright (c) 2023, David Ganz <david.g.ganz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGUI/ActionGroup.h>
#include <LibGUI/Label.h>
#include <LibGUI/ScrollableContainerWidget.h>
#include <LibGUI/Toolbar.h>
#include <LibGUI/Widget.h>

namespace Maps {

class Tool;

class ToolStripWidget : public GUI::Widget {
    C_OBJECT(ToolStripWidget);

public:
    Tool* active_tool() { return m_active_tool; }

    Optional<Function<void(Tool*)>> on_tool_selection;

private:
    explicit ToolStripWidget();

    void setup_tools();
    void show_tool_ui(NonnullRefPtr<GUI::Widget> widget);
    void hide_tool_ui();

    RefPtr<GUI::Widget> m_tool_ui_widget;
    RefPtr<GUI::Widget> m_tool_ui_container;
    RefPtr<GUI::Scrollbar> m_tool_ui_scrollbar;
    RefPtr<GUI::Label> m_tool_name_label;
    RefPtr<GUI::Widget> m_tool_ui;

    RefPtr<GUI::Toolbar> m_toolbar;
    GUI::ActionGroup m_action_group;
    Vector<NonnullOwnPtr<Tool>> m_tools;
    Tool* m_active_tool;
};

}
