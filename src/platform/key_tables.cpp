#include "platform/key_tables.h"

#include "platform/copilot_keys.h"
#include "platform/demo_actions_keys.h"
#include "platform/demo_camera_keys.h"
#include "platform/demo_driver_feel_keys.h"
#include "platform/demo_measure_keys.h"
#include "platform/demo_screen_keys.h"
#include "platform/demo_world_keys.h"
#include "platform/demo_zone_modes_keys.h"
#include "platform/drive_keys.h"
#include "platform/window_keys.h"

namespace rally::platform {

namespace {

constexpr const KeyTable* kTables[] = {
    &kDriveKeys,  &kWorldKeys,   &kZoneModeKeys, &kCameraKeys,  &kDriverFeelKeys,
    &kScreenKeys, &kMeasureKeys, &kWindowKeys,   &kActionsKeys, &kCopilotKeys,
};

} // namespace

std::span<const KeyTable* const> key_tables() { return kTables; }

} // namespace rally::platform
