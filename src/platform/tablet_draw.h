#pragma once

#include "game/direction_changes.h"
#include "platform/overlay.h"
#include "platform/perf_stats.h"
#include "platform/tablet_cursor.h"
#include "platform/tablet_geometry.h"
#include "platform/tablet_view.h"
#include "sim/stage_track.h"

#include <cstdint>
#include <vector>

namespace rally::platform {

// El dibujo de la tablet del copiloto (lote 6): una capa fina sobre platform/tablet_geometry, que
// calcula todo sin raylib. Mapa cenital con la flecha fija en el tercio inferior, perfil de
// altura debajo y cinta de ruta a la derecha, con el cursor en el mapa y en la cinta. Nada de
// notas (docs/01 §5.2; CLAUDE.md): solo la carretera, sus marcas, la flecha y el cursor.
class TabletDraw {
public:
    TabletDraw();

    // Al construir el mundo: el eje muestreado del tramo que se dibuja.
    void set_stage(const sim::StageTrack& track) { road_.build(track); }

    // Dibuja la tablet en la capa Tablet de `overlay`. Mide la geometría y el dibujo por
    // separado (last_geometry_ns, last_draw_ns).
    void draw(Overlay& overlay, const sim::StageTrack& track, const TabletView& view,
              const TabletCursor& cursor, const std::vector<game::DirectionChange>& changes,
              MapOrientation orientation);

    // La vista del mapa del último fotograma (para el clic del puntero) y su sitio.
    const MapView& map_view() const { return map_view_; }
    const TabletLayout& layout() const { return layout_; }

    std::int64_t last_geometry_ns() const { return geometry_ns_; }
    std::int64_t last_draw_ns() const { return draw_ns_; }

private:
    void draw_map(Overlay& overlay, const sim::StageTrack& track, const TabletView& view,
                  const TabletCursor& cursor, MapOrientation orientation);
    void draw_profile(Overlay& overlay, const TabletView& view);
    void draw_ribbon(Overlay& overlay);

    TabletRoad road_;
    TabletLayout layout_;
    MapView map_view_;
    MapGeometry map_;
    ProfileGeometry profile_;
    RibbonGeometry ribbon_;
    std::vector<MapSegment> segments_;
    MapMark cursor_mark_;
    bool cursor_on_map_ = false;
    std::int64_t geometry_ns_ = 0;
    std::int64_t draw_ns_ = 0;
};

} // namespace rally::platform
