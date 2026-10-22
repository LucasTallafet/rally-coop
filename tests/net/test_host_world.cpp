// El host no cambia la simulación, y los tamaños del mensaje de mundo.

#include "car_worlds.h"
#include "io/recording.h"
#include "net/copilot.h"
#include "net/host.h"
#include "net/protocol.h"
#include "pump.h"
#include "sim/demo_world.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace {

using namespace rally::net;

rally::sim::SimInput input_at(int step) {
    rally::sim::SimInput input;
    input.throttle = rally::sim::kAxisMax;
    // Un volantazo lento a un lado y a otro.
    input.steer = static_cast<std::int16_t>(((step / 240) % 2 == 0 ? 1 : -1) * 8000);
    if (step > 1800) {
        input.brake = 12000;
        input.throttle = 0;
    }
    return input;
}

std::uint64_t run(bool with_host) {
    rally::sim::SimWorld world;
    world.build(rally::tests::flat_world(rally::sim::Surface::Gravel));
    world.seal();
    world.set_telemetry(true);
    std::unique_ptr<Host> host;
    std::unique_ptr<Copilot> copilot;
    if (with_host) {
        host = std::make_unique<Host>();
        std::string error;
        REQUIRE_MESSAGE(host->start(0, error), error);
        copilot = std::make_unique<Copilot>();
        REQUIRE_MESSAGE(copilot->connect("127.0.0.1", host->port(), error), error);
        host->set_world("mundo", "coche", 1);
        REQUIRE(rally::tests::pump(host.get(), {copilot.get()},
                                   [&] { return copilot->take_world().has_value(); }));
    }
    for (int frame = 0; frame < 600; ++frame) {
        for (int i = 0; i < 4; ++i) {
            world.step(input_at(frame * 4 + i));
        }
        if (host) {
            host->after_steps(world, static_cast<std::uint32_t>((frame + 1) * 4), frame == 300);
            host->service();
            copilot->service(rally::tests::wall_seconds());
        }
    }
    if (with_host) {
        CHECK(copilot->buffer().newest_step() > 2000);
    }
    return world.state_hash();
}

} // namespace

TEST_CASE("net_host: after_steps in every frame does not change the simulation") {
    const rally::sim::JoltRuntime runtime;
    const std::uint64_t without = run(false);
    const std::uint64_t with = run(true);
    std::printf("net_host: state_hash sin host 0x%016llx, con host 0x%016llx\n",
                static_cast<unsigned long long>(without), static_cast<unsigned long long>(with));
    CHECK(without == with);
}

TEST_CASE("net_host: size of the world message") {
    const rally::sim::JoltRuntime runtime;
    const rally::sim::CarData& car = rally::tests::reference_car();
    const std::string& car_text = rally::tests::reference_car_text();
    for (int zone = 0; zone < 2; ++zone) {
        const rally::sim::WorldDesc desc =
            zone == 0 ? rally::sim::zone_world(rally::sim::Surface::Gravel, car)
                      : rally::sim::demo_arena(0.0, car);
        WorldInfo info;
        info.world_bytes = rally::io::serialize_world(desc);
        info.car_text = car_text;
        const std::string message = encode_world(info);
        std::printf("net_host: mensaje de mundo %s: %zu B (mundo %zu B + coche %zu B)\n",
                    zone == 0 ? "con la zona" : "con el recinto", message.size(),
                    info.world_bytes.size(), info.car_text.size());
        CHECK(message.size() == 1 + 1 + 8 + 4 + info.world_bytes.size() + 4 + car_text.size());
        WorldInfo back;
        REQUIRE(
            decode_world(std::span<const std::uint8_t>(
                             reinterpret_cast<const std::uint8_t*>(message.data()), message.size()),
                         back));
        CHECK(back.world_bytes == info.world_bytes);
        CHECK(back.car_text == info.car_text);
    }
}

TEST_CASE("net_host: control messages reject truncated or inconsistent bytes") {
    WorldInfo info;
    info.generation = 9;
    info.zone_hash = 5;
    info.world_bytes = "abc";
    info.car_text = "de";
    const std::string message = encode_world(info);
    for (std::size_t n = 0; n < message.size(); ++n) {
        WorldInfo out;
        CHECK_FALSE(decode_world(
            std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(message.data()), n),
            out));
    }
    Reply reply;
    reply.accepted = true;
    const std::string r = encode_reply(reply);
    Reply out;
    CHECK(decode_reply(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(r.data()), r.size()),
        out));
    CHECK(out.accepted);
    std::uint16_t version = 0;
    const std::string h = encode_hello(7);
    CHECK(decode_hello(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(h.data()), h.size()),
        version));
    CHECK(version == 7);
    CHECK_FALSE(decode_hello(std::span<const std::uint8_t>(
                                 reinterpret_cast<const std::uint8_t*>(h.data()), h.size() - 1),
                             version));
}
