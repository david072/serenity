/*
 * Copyright (c) 2023, David Ganz <david.g.ganz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../MapWidget.h"
#include <LibGfx/Forward.h>

namespace Maps {

class MeasurementTool : public Tool {
public:
    MeasurementTool() = default;

    virtual ~MeasurementTool() = default;

    virtual StringView tool_name() const override { return "Measure"sv; }

    virtual Tool::EventResult doubleclick_event(GUI::MouseEvent&, MapWidget&) override;
    virtual Tool::EventResult mousemove_event(GUI::MouseEvent&, MapWidget&) override;
    virtual Tool::EventResult mousedown_event(GUI::MouseEvent&, MapWidget&) override;
    virtual Tool::EventResult mouseup_event(GUI::MouseEvent&, MapWidget&) override;
    virtual Tool::EventResult mousewheel_event(GUI::MouseEvent&, MapWidget&) override;
    virtual Tool::EventResult context_menu_event(GUI::ContextMenuEvent&, MapWidget&) override;
    virtual Tool::EventResult keydown_event(GUI::KeyEvent&, MapWidget&) override;
    virtual Tool::EventResult keyup_event(GUI::KeyEvent&, MapWidget&) override;
    virtual void paint_event(GUI::PaintEvent&, MapWidget&, GUI::Painter&) override;

private:
    struct Point {
        double latitude;
        double longitude;
        bool hovered { false };

        Gfx::IntPoint to_pixel_coords(MapWidget& map) const
        {
            return Gfx::IntPoint(map.longitude_to_x(longitude), map.latitude_to_y(latitude));
        }

        void set_position(Gfx::IntPoint pos, MapWidget& map)
        {
            latitude = map.y_to_latitude(pos.y());
            longitude = map.x_to_longitude(pos.x());
        }
    };

    Gfx::IntPoint m_mouse_pos;
    bool m_mouse_down { false };

    bool m_adding_points { true };
    Vector<Point> m_points;
    Point* m_hovered_point;
};

}
