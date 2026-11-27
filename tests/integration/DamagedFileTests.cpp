// What opens of a damaged file can be worked with: it evaluates and meshes
// without throwing. Reading and evaluating alone are tested beside core
// (FileTests.cpp), which may not mesh.

#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "Damaged.h"
#include "Expect.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/core/File.h"
#include "clothcad/session/Samples.h"
#include "clothcad/sim/Mesh.h"

using namespace clothcad;

TEST_CASE("what opens of the damaged trousers evaluates and meshes")
{
    const auto trousers = session::samples()[6].build();
    const std::string text = toText(trousers);
    REQUIRE(text.find("Instance") != std::string::npos);  // the trousers, with all they are made of

    std::size_t opened = 0, meshed = 0;
    testing::forEachDamaged(text, [&](const std::string& damaged) {
        INFO(damaged);
        const std::variant<SavedScene, FileError> read = fromText(damaged);
        const SavedScene* scene = std::get_if<SavedScene>(&read);
        if (!scene) return;
        ++opened;
        const Result shapes = evaluate(scene->document);
        // Coarse, so that the thousands of them take seconds.
        const sim::ClothMesh cloth = sim::meshOf(scene->document, shapes, {.particleDistance = coarsestParticleDistance});
        for (const sim::PanelMesh& panel : cloth.panels) {
            CHECK((panel.problem.empty() == !panel.triangles.empty()));
            if (!panel.triangles.empty()) ++meshed;
        }
    });
    EXPECT((opened > 100 && meshed > 100), "much of it opens and is cloth still");
}
