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

    set_min_width(26);
    set_max_width(326);
    set_preferred_width(26);
    set_layout<GUI::HorizontalBoxLayout>(2, 0);

    m_action_group.set_exclusive(true);
    m_action_group.set_unchecking_allowed(true);

    m_tool_ui_widget = add<GUI::Widget>();
    m_tool_ui_widget->set_layout<GUI::HorizontalBoxLayout>();

    m_tool_ui_container = m_tool_ui_widget->add<GUI::Widget>();
    m_tool_ui_container->set_layout<GUI::VerticalBoxLayout>();
    m_tool_name_label = m_tool_ui_container->add<GUI::Label>();
    m_tool_name_label->set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_tool_name_label->set_font(m_tool_name_label->font().bold_variant());

    m_tool_ui_scrollbar = m_tool_ui_widget->add<GUI::Scrollbar>();
    m_tool_ui_widget->set_width(300);
    m_tool_ui_widget->set_visible(false);

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

                    m_tool_name_label->set_text(String::from_utf8(m_active_tool->tool_name()).release_value_but_fixme_should_propagate_errors());
                    show_tool_ui(m_active_tool->tool_ui());
                } else {
                    if (on_tool_selection.has_value())
                        on_tool_selection.value()(nullptr);
                    m_active_tool = nullptr;

                    hide_tool_ui();
                }
            });

        m_action_group.add_action(action);
        m_toolbar->add_action(action);
        m_tools.append(move(tool));
        return {};
    };

    MUST(add_tool("app-hello-world"sv, make<MeasurementTool>()));
}

void ToolStripWidget::show_tool_ui(NonnullRefPtr<GUI::Widget> widget)
{
    set_width(226);
    set_preferred_width(226);
    m_tool_ui = move(widget);
    m_tool_ui_widget->set_visible(true);
    m_tool_ui->set_width(m_tool_ui_container->width());
    m_tool_ui_container->insert_child_before(*m_tool_ui, *m_tool_ui_scrollbar);
}

void ToolStripWidget::hide_tool_ui()
{
    m_tool_ui_widget->set_visible(false);
    m_tool_ui_container->remove_child(*m_tool_ui);
    set_preferred_width(26);
}

}
