// Registro de notas: una fila por orden, con la distancia real hasta el cambio de dirección
// siguiente, frente a lo calculado a mano en lazadas.

#include "game/direction_changes.h"
#include "game/notes.h"
#include "io/notes_log.h"
#include "sim/stage_axis.h"
#include "stage_driver.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace rally::game;
using namespace rally::io;

namespace {

std::vector<DirectionChange> lazadas_changes() {
    rally::sim::StageAxis axis;
    const rally::sim::StageParseResult r =
        rally::sim::StageAxis::parse(rally::tests::lazadas_stage_text(), axis);
    INFO(r.error);
    REQUIRE(r.ok);
    return direction_changes(axis);
}

std::string fixed3(double v) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.3f", v);
    return text;
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        out.push_back(text.substr(start, end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

struct Step {
    std::uint64_t step;
    double car_s;
    NoteOrder order;
    std::string expected; // la fila entera
};

} // namespace

TEST_CASE("notes_log: the rows of a test run in lazadas") {
    const std::vector<DirectionChange> c = lazadas_changes();
    REQUIRE(c.size() == 6);
    const std::string a0 = fixed3(c[0].s_start);
    const std::string a1 = fixed3(c[1].s_start);
    const std::string a2 = fixed3(c[2].s_start);
    const std::string a5 = fixed3(c[5].s_start);
    // La primera orden es un modificador con la cola vacía: no hace nada y se registra.
    const std::vector<Step> steps = {
        {1, 0.0, mark_order(NoteMark::Tightens), "1,modificador,sin nota,,,,,,,0.000,0"},
        {10, 0.0, corner_order(c[0].s_start, NoteGrade::G4, NoteSide::Right),
         "10,curva,ok,1," + a0 + ",4 derecha,,,140.000,0.000,0"},
        {20, 12.5, distance_order(wheel_distance(5)),
         "20,distancia,ok,1," + a0 + ",4 derecha,150,rueda,140.000,12.500,0"},
        {30, 12.5, corner_order(c[1].s_start, NoteGrade::G2, NoteSide::Left),
         "30,curva,ok,2," + a1 + ",2 izquierda,,,0.000,12.500,0"},
        {40, 12.5, mark_order(NoteMark::Tightens),
         "40,modificador,ok,2," + a1 + ",2 izquierda se cierra,,,0.000,12.500,0"},
        {50, 12.5, distance_order(counter_distance(60)),
         "50,distancia,ok,2," + a1 + ",2 izquierda se cierra,60,contador,0.000,12.500,0"},
        {60, 12.5, corner_order(c[2].s_start, NoteGrade::G3, NoteSide::Right),
         "60,curva,ok,3," + a2 + ",3 derecha,,,80.000,12.500,0"},
        {70, 12.5, distance_order(wheel_distance(0)),
         "70,distancia,ok,3," + a2 + ",3 derecha,y,rueda,80.000,12.500,0"},
        {80, 12.5, hazard_order(300.0, NoteMark::Crest),
         "80,peligro,ok,4,300.000,cresta,,,140.000,12.500,0"},
        {90, 12.5, erase_last_order(), "90,borrar,ok,4,300.000,cresta,,,140.000,12.500,1"},
        {100, 500.0, clear_pending_order(), "100,vaciar,ok,,,,,,,500.000,1"},
        {110, 500.0, corner_order(c[5].s_start, NoteGrade::Hairpin, NoteSide::Left),
         "110,curva,ok,5," + a5 + ",H izquierda,,,,500.000,0"},
    };

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "rally_notes_log_test";
    std::filesystem::remove_all(dir);
    const std::string path = (dir / "sub" / "notas.csv").string(); // la carpeta no existe
    NotesLog log;
    std::string error;
    const std::string header = notes_log_header("lazadas");
    REQUIRE_MESSAGE(log.open(path, header, error), error);
    NoteQueue q;
    q.reset(1077.7);
    for (const Step& s : steps) {
        const NoteApplied applied = q.apply(s.order, s.car_s);
        // Fila suelta y fila del archivo dicen lo mismo.
        std::string row;
        append_notes_log_row(s.step, s.car_s, s.order, applied, c, row);
        CHECK(row == s.expected + "\n");
        log.append(s.step, s.car_s, s.order, applied, c);
    }
    CHECK(log.rows() == 12);
    REQUIRE_MESSAGE(log.close(error), error);

    std::string text;
    {
        std::ifstream in(std::filesystem::path(path), std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    // UTF-8 con BOM.
    REQUIRE(text.size() > 3);
    CHECK(static_cast<unsigned char>(text[0]) == 0xEF);
    CHECK(static_cast<unsigned char>(text[1]) == 0xBB);
    CHECK(static_cast<unsigned char>(text[2]) == 0xBF);
    const std::vector<std::string> lines = lines_of(text);
    REQUIRE(lines.size() == 4 + steps.size());
    CHECK(lines[0] == "\xEF\xBB\xBF# rally notes");
    CHECK(lines[1] == "# format=1");
    CHECK(lines[2] == "# stage=lazadas");
    CHECK(lines[3] ==
          "step,order,result,note_id,anchor_s,note,distance,distance_source,real_distance_m,car_s,"
          "erased");
    for (std::size_t i = 0; i < steps.size(); ++i) {
        CHECK(lines[4 + i] == steps[i].expected);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("notes_log: a rejected order because the queue is full is logged") {
    const std::vector<DirectionChange> c = lazadas_changes();
    NoteQueue q;
    q.reset(1077.7);
    for (int i = 0; i < kMaxNotes; ++i) {
        q.apply(corner_order(2.0 * i, NoteGrade::G6, NoteSide::Left), 0.0);
    }
    const NoteOrder order = corner_order(c[0].s_start, NoteGrade::G3, NoteSide::Left);
    const NoteApplied applied = q.apply(order, 0.0);
    REQUIRE(applied.result == NoteResult::Full);
    std::string row;
    append_notes_log_row(5, 0.0, order, applied, c, row);
    // Sin identificador (no entró), pero con lo que se rechazó.
    CHECK(row ==
          "5,curva,cola llena,," + fixed3(c[0].s_start) + ",3 izquierda,,,140.000,0.000,0\n");
    // Una orden fuera de rango.
    NoteDistance far = counter_distance(10);
    far.metres = 991;
    const NoteOrder bad = distance_order(far);
    row.clear();
    append_notes_log_row(6, 1.0, bad, q.apply(bad, 0.0), c, row);
    CHECK(row == "6,distancia,no valida,,,,991,contador,,1.000,0\n");
}

TEST_CASE("notes_log: the file cannot be created in a place that does not exist") {
    NotesLog log;
    std::string error;
    // Una carpeta con nombre de archivo no se puede abrir como archivo.
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    CHECK_FALSE(log.open(dir.string(), notes_log_header("x"), error));
    CHECK_FALSE(error.empty());
}
