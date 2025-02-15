/*
 * Copyright (c) 2018-2021, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGUI/Button.h>
#include <LibGUI/CheckBox.h>
#include <LibGUI/ComboBox.h>
#include <LibGUI/SettingsWindow.h>
#include <LibGUI/TextEditor.h>
#include <LibVT/TerminalWidget.h>

class TerminalSettingsMainWidget final : public GUI::SettingsWindow::Tab {
    C_OBJECT_ABSTRACT(TerminalSettingsMainWidget)
public:
    static ErrorOr<NonnullRefPtr<TerminalSettingsMainWidget>> try_create();

    virtual void apply_settings() override;
    virtual void cancel_settings() override;

private:
    TerminalSettingsMainWidget() = default;
    ErrorOr<void> setup();
    void write_back_settings() const;

    static VT::TerminalWidget::BellMode parse_bell(StringView bell_string);
    static ByteString stringify_bell(VT::TerminalWidget::BellMode bell_mode);

    VT::TerminalWidget::BellMode m_bell_mode { VT::TerminalWidget::BellMode::Disabled };
    bool m_confirm_close { true };

    VT::TerminalWidget::BellMode m_original_bell_mode;
    bool m_orignal_confirm_close { true };
};

class TerminalSettingsViewWidget final : public GUI::SettingsWindow::Tab {
    C_OBJECT_ABSTRACT(TerminalSettingsViewWidget)
public:
    static ErrorOr<NonnullRefPtr<TerminalSettingsViewWidget>> try_create();

    virtual void apply_settings() override;
    virtual void cancel_settings() override;

private:
    TerminalSettingsViewWidget() = default;
    ErrorOr<void> setup();
    void write_back_settings() const;

    RefPtr<Gfx::Font const> m_font;
    float m_opacity;
    ByteString m_color_scheme;
    VT::CursorShape m_cursor_shape { VT::CursorShape::Block };
    bool m_cursor_is_blinking_set { true };
    size_t m_max_history_size;
    bool m_show_scrollbar { true };

    RefPtr<Gfx::Font const> m_original_font;
    float m_original_opacity;
    ByteString m_original_color_scheme;
    VT::CursorShape m_original_cursor_shape;
    bool m_original_cursor_is_blinking_set;
    size_t m_original_max_history_size;
    bool m_original_show_scrollbar { true };
};
