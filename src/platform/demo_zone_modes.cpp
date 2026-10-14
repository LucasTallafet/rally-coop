#include "platform/demo_zone_modes.h"

#include "platform/demo_common.h"
#include "platform/demo_zone_modes_keys.h"
#include "platform/keys_raylib.h"

#include <algorithm>
#include <string>

namespace rally::platform {

using namespace demo;

void DemoZoneModes::handle_keys(DemoContext& ctx, FramePacer& /*pacer*/, PerfCounter& /*perf*/) {
    const std::uint8_t context = ctx.frame_key_context;
    WorldMode& mode = ctx.mode;
    if (action_pressed(action(ZoneModeAction::ToggleArena), context)) {
        mode.zone = !mode.zone;
        ctx.view = 0;
        ctx.rebuild = Rebuild::Describe;
    }
    if (action_pressed(action(ZoneModeAction::BlindMode), context)) {
        // Al entrar empieza la serie desde la primera prueba; al salir, la pista vuelve a
        // la superficie elegida con G.
        mode.blind = !mode.blind;
        if (mode.blind) {
            blind_series_ = sim::blind_series(blind_seed_);
            mode.blind_trial = 0;
            mode.blind_trial_surface = blind_series_[0];
        }
        ctx.rebuild = Rebuild::Describe;
    }
    if (action_pressed(action(ZoneModeAction::NextTrial), context)) {
        mode.blind_trial = std::min(mode.blind_trial + 1, sim::kBlindTrials - 1);
        mode.blind_trial_surface = blind_series_[static_cast<std::size_t>(mode.blind_trial)];
        ctx.rebuild = Rebuild::Describe;
    }
    if (action_pressed(action(ZoneModeAction::NextSurface), context)) {
        mode.blind_surface = static_cast<sim::Surface>((static_cast<int>(mode.blind_surface) + 1) %
                                                       sim::kSurfaceCount);
        ctx.rebuild = Rebuild::Describe;
    }
    if (action_pressed(action(ZoneModeAction::RoadSurface), context)) {
        mode.road_surface = mode.road_surface == sim::Surface::Gravel ? sim::Surface::Asphalt
                                                                      : sim::Surface::Gravel;
        mode.road_start = true;
        ctx.rebuild = Rebuild::Describe;
        ctx.overlay.set_message(Overlay::Message::Car,
                                std::string("U: carretera de ") +
                                    sim::surface_name(mode.road_surface),
                                kText, kShortMessageNs);
    }
    if (action_pressed(action(ZoneModeAction::ZoneStart), context)) {
        mode.road_start = !mode.road_start;
        ctx.rebuild = Rebuild::Describe;
        ctx.overlay.set_message(Overlay::Message::Car,
                                mode.road_start ? "Y: salida en la carretera de prueba"
                                                : "Y: salida en la recta",
                                kText, kShortMessageNs);
    }
    // La zona está hecha para el origen: O solo mueve el recinto.
    if (action_pressed(action(ZoneModeAction::FarOrigin), context)) {
        mode.far_from_origin = !mode.far_from_origin;
        ctx.rebuild = Rebuild::Describe;
    }
}

} // namespace rally::platform
