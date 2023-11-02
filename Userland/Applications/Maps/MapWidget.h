/*
 * Copyright (c) 2023, Bastiaan van der Plaat <bastiaan.v.d.plaat@gmail.com>
 * Copyright (c) 2023, Jelle Raaijmakers <jelle@gmta.nl>
 * Copyright (c) 2023, David Ganz <david.g.ganz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "ToolStripWidget.h"
#include "Tools/Tool.h"
#include <AK/Queue.h>
#include <LibConfig/Listener.h>
#include <LibGUI/Action.h>
#include <LibGUI/Frame.h>
#include <LibGUI/Menu.h>
#include <LibGUI/Painter.h>
#include <LibProtocol/Request.h>
#include <LibProtocol/RequestClient.h>

namespace Maps {

static int constexpr EARTH_RADIUS = 6371000.0;

struct Coordinate {
    double latitude;
    double longitude;

    double distance_to(Coordinate const& other) const;
};

class MapWidget : public GUI::Frame
    , public Config::Listener {
    C_OBJECT(MapWidget);

public:
    struct LatLngBounds {
        Coordinate north_west;
        Coordinate south_east;

        int get_zoom() const;
    };

    struct Options {
        Optional<String> tile_provider {};
        Coordinate center;
        int zoom;
        bool context_menu_enabled { true };
        bool scale_enabled { true };
        int scale_max_width { 100 };
        bool attribution_enabled { true };
        Optional<String> attribution_text {};
        Optional<URL> attribution_url {};
    };

    Coordinate center() const { return m_center; }
    void set_center(Coordinate const& center)
    {
        m_center = {
            min(max(center.latitude, -LATITUDE_MAX), LATITUDE_MAX),
            min(max(center.longitude, -180.0), 180.0)
        };
        update();
    }

    int zoom() const { return m_zoom; }
    void set_zoom(int zoom);

    struct Marker {
        Coordinate latlng;
        Optional<String> tooltip {};
        RefPtr<Gfx::Bitmap> image { nullptr };
        Optional<String> name {};
    };
    void add_marker(Marker const& marker)
    {
        m_markers.append(marker);
        update();
    }
    void remove_markers_with_name(StringView name)
    {
        m_markers.remove_all_matching([name](auto const& marker) { return marker.name == name; });
        update();
    }

    struct Panel {
        enum class Position {
            TopLeft,
            TopRight,
            BottomLeft,
            BottomRight,
        };
        String text;
        Position position;
        Optional<URL> url {};
        Optional<String> name {};
        Gfx::IntRect rect { 0, 0, 0, 0 };
    };
    void add_panel(Panel const& panel)
    {
        m_panels.append(panel);
        update();
    }
    void remove_panels_with_name(StringView name)
    {
        m_panels.remove_all_matching([name](auto const& panel) { return panel.name == name; });
        update();
    }

    Coordinate context_menu_latlng() const { return m_context_menu_latlng; }
    void add_context_menu_action(NonnullRefPtr<GUI::Action> const& action)
    {
        m_context_menu_actions.append(action);
    }

    struct TileKey {
        int x;
        int y;
        int zoom;

        unsigned hash() const
        {
            return pair_int_hash(x, pair_int_hash(y, zoom));
        }

        bool operator==(TileKey const& other) const
        {
            return x == other.x && y == other.y && zoom == other.zoom;
        }
    };

    enum class TileDownloadBehavior {
        DoNotDownload,
        Download,
    };

    void set_tool_strip(RefPtr<ToolStripWidget> widget)
    {
        m_tool_strip_widget = move(widget);
        m_tool_strip_widget->on_tool_selection = [&](Tool*) { update(); };
        update();
    }

    int longitude_to_x(double);
    int latitude_to_y(double);
    double x_to_longitude(int);
    double y_to_latitude(int);

protected:
    MapWidget(Options const&);

    RefPtr<Protocol::RequestClient> request_client() const { return m_request_client; }

private:
    virtual void config_string_did_change(StringView domain, StringView group, StringView key, StringView value) override;
    virtual void doubleclick_event(GUI::MouseEvent&) override;
    virtual void mousemove_event(GUI::MouseEvent&) override;
    virtual void mousedown_event(GUI::MouseEvent&) override;
    virtual void mouseup_event(GUI::MouseEvent&) override;
    virtual void mousewheel_event(GUI::MouseEvent&) override;
    virtual void context_menu_event(GUI::ContextMenuEvent& event) override;
    virtual void keydown_event(GUI::KeyEvent&) override;
    virtual void keyup_event(GUI::KeyEvent&) override;
    virtual void paint_event(GUI::PaintEvent&) override;

    Tool* active_tool()
    {
        if (!m_tool_strip_widget)
            return nullptr;
        return m_tool_strip_widget->active_tool();
    }

    bool handle_tool_result(Tool::EventResult);

    void set_zoom_for_mouse_event(int zoom, GUI::MouseEvent&);

    Optional<RefPtr<Gfx::Bitmap>> get_tile_image(int x, int y, int zoom, TileDownloadBehavior);
    void process_tile_queue();
    void clear_tile_queue();

    void paint_map(GUI::Painter&);
    void paint_scale_line(GUI::Painter&, String label, Gfx::IntRect rect);
    void paint_scale(GUI::Painter&);
    void paint_panels(GUI::Painter&);

    static int constexpr TILE_SIZE = 256;
    static double constexpr LATITUDE_MAX = 85.0511287798066;
    static size_t constexpr TILES_CACHE_MAX = 256;
    static constexpr size_t TILES_DOWNLOAD_PARALLEL_MAX = 8;
    static int constexpr ZOOM_MIN = 2;
    static int constexpr ZOOM_MAX = 19;
    static int constexpr PANEL_PADDING_X = 6;
    static int constexpr PANEL_PADDING_Y = 4;

    // These colors match the default OpenStreetMap map tiles style, so they don't depend on any system theme colors
    static Gfx::Color constexpr map_background_color = { 200, 200, 200 };
    static Gfx::Color constexpr panel_background_color = { 255, 255, 255, 204 };
    static Gfx::Color constexpr panel_foreground_color = { 51, 51, 51 };

    RefPtr<Protocol::RequestClient> m_request_client;
    Vector<RefPtr<Protocol::Request>, TILES_DOWNLOAD_PARALLEL_MAX> m_active_requests;
    Queue<TileKey, 32> m_tile_queue;
    RefPtr<Gfx::Bitmap> m_marker_image;
    Optional<String> m_tile_provider;
    String m_default_tile_provider;
    Coordinate m_center;
    int m_zoom {};
    bool m_context_menu_enabled {};
    RefPtr<GUI::Menu> m_context_menu;
    Coordinate m_context_menu_latlng;
    Vector<NonnullRefPtr<GUI::Action>> m_context_menu_actions;
    bool m_scale_enabled {};
    int m_scale_max_width {};
    bool m_attribution_enabled {};
    URL m_attribution_url;
    bool m_dragging { false };
    int m_last_mouse_x { 0 };
    int m_last_mouse_y { 0 };
    bool m_first_image_loaded { false };
    bool m_connection_failed { false };
    OrderedHashMap<TileKey, RefPtr<Gfx::Bitmap>> m_tiles;
    Vector<Marker> m_markers;
    Vector<Panel> m_panels;

    RefPtr<ToolStripWidget> m_tool_strip_widget;
};

}

template<>
struct AK::Traits<Maps::MapWidget::TileKey> : public GenericTraits<Maps::MapWidget::TileKey> {
    static unsigned hash(Maps::MapWidget::TileKey const& t) { return t.hash(); }
};
