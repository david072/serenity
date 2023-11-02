/*
 * Copyright (c) 2023, David Ganz <david.g.ganz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../MapWidget.h"
#include <LibGfx/Forward.h>

namespace Maps {

static constexpr int CIRCLE_RADIUS = 4;

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
    class Point : public Weakable<Point> {
    public:
        Coordinate coordinate;
        bool hovered { false };

        Point(double lat, double lng)
            : coordinate(Coordinate(lat, lng))
        {
        }

        Gfx::IntPoint to_pixel_coords(MapWidget& map) const
        {
            return Gfx::IntPoint(map.longitude_to_x(coordinate.longitude), map.latitude_to_y(coordinate.latitude));
        }

        void set_position(Gfx::IntPoint pos, MapWidget& map)
        {
            auto lat = map.y_to_latitude(pos.y());
            auto lng = map.x_to_longitude(pos.x());
            coordinate = Coordinate(lat, lng);
        }

        bool is_hovered_by(Gfx::IntPoint other, MapWidget& map) const
        {
            return other.distance_from(to_pixel_coords(map)) <= CIRCLE_RADIUS;
        }
    };

    bool is_dragging() { return m_mouse_down && m_hovered_point.has_value(); }

    Gfx::IntPoint m_mouse_pos;
    bool m_mouse_down { false };

    bool m_is_shape { false };
    bool m_adding_points { true };
    Vector<NonnullOwnPtr<Point>> m_points;
    WeakPtr<Point> m_hovered_point;
};

}
