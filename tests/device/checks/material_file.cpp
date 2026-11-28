module;

#include <stb/stb_image.h>

module aegis.test;

import std;
import aegis.asset_library;
import aegis.assets;
import aegis.engine;
import aegis.rhi;
import aegis.serialize;

// --material-file: a scene that names a material file renders with it, a changed file renders differently, and so does
// an edited copy of the file given to the loaded material (AssetLibrary::applyMaterialFile, the editor's Material panel).
// The project is written to a temporary folder, so the run needs no sample project: a camera looks at the engine's
// cube, whose material is the file's. The material is black and lit by nothing (no lights, the default black sky, which
// the lighting pass needs to run), so the cube shows only its emissive colour; rewriting the file swaps that colour and a second engine renders it again. A
// third engine loads that file and applies a copy with a third colour before it renders, which the frame must show.
// The changed file is also masked and double-sided ("alphaMode", "cullMode"): the third engine checks that its loaded
// material has that variant, and that the applied copy, which names neither, gives it the default variant again.
namespace aegis::test
{
namespace
{
constexpr std::string_view materialFile{ "materials/emissive.material" };
constexpr std::string_view sceneFile{ "scenes/material_file.scene" };

// The emissive colours of the three runs: one channel off in each, so a pixel shows which one was rendered.
constexpr std::array<int, 3> cyan{ 0, 1, 1 };
constexpr std::array<int, 3> yellow{ 1, 1, 0 };
constexpr std::array<int, 3> magenta{ 1, 0, 1 };

// The engine content's cube mesh (content/cube.obj.meta, "StaticMesh/cube/0").
constexpr std::string_view cubeMesh{ "ebb588c4f30352a9" };

// With 'masked', the file is masked and double-sided; the cube's alpha is 1 (no albedo map), so it still covers the
// same pixels.
auto materialText(const std::array<int, 3>& emissive, bool masked = false) -> std::string
{
    return std::format(R"({{
  "type": "Material",
  "id": "4d8e1f0a7b3c9265",
  "version": 1,
  "template": "{}",{}
  "parameters": {{
    "albedo": [0, 0, 0],
    "emissive": [{}, {}, {}],
    "metallic": 0,
    "roughness": 1
  }}
}}
)",
        assets::engine::pbrTemplate, masked ? "\n  \"alphaMode\": \"mask\",\n  \"cullMode\": \"none\"," : "", emissive[0], emissive[1],
        emissive[2]);
}

auto sceneText() -> std::string
{
    return std::format(R"({{
  "type": "Scene",
  "id": "a37c05e19f2d6b84",
  "version": 1,
  "types": {{ "Camera": 1, "MainCamera": 1, "MeshRenderer": 1, "Sky": 1, "Transform": 1 }},
  "entities": [
    {{
      "id": "1e9b47c2d05a836f",
      "name": "Camera",
      "children": [],
      "components": {{
        "Camera": {{ "fov": 1.0471976, "near": 0.1, "far": 100 }},
        "MainCamera": {{}},
        "Transform": {{ "location": [0, -3, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }}
      }}
    }},
    {{
      "id": "5c81e7f02a4d93b6",
      "name": "Sky",
      "children": [],
      "components": {{
        "Sky": {{ "environment": null }},
        "Transform": {{ "location": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }}
      }}
    }},
    {{
      "id": "6f20d8b3a1c7e594",
      "name": "Cube",
      "children": [],
      "components": {{
        "MeshRenderer": {{ "mesh": "{}", "material": "4d8e1f0a7b3c9265" }},
        "Transform": {{ "location": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }}
      }}
    }}
  ]
}}
)",
        cubeMesh);
}

auto writeText(const std::filesystem::path& file, std::string_view text) -> std::expected<void, std::string>
{
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream stream{ file, std::ios::binary | std::ios::trunc };
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (error || !stream)
        return std::unexpected{ std::format("cannot write '{}'", file.string()) };
    return {};
}

// Deletes the temporary project, whatever the run did.
struct TemporaryFolder
{
    std::filesystem::path path;

    explicit TemporaryFolder(std::filesystem::path folder) :
        path{ std::move(folder) }
    {
    }
    TemporaryFolder(const TemporaryFolder&) = delete;
    auto operator=(const TemporaryFolder&) -> TemporaryFolder& = delete;
    ~TemporaryFolder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

// The pixels of 'pixels' (RGBA8) that show 'colour' as an emissive colour, unlit: a 1 bright (the default post-processing
// maps it to about 245) and a 0 as exactly 0, which no light or bloom raises. The bloom around the cube stays below 200.
auto colourPixels(std::span<const std::byte> pixels, const std::array<int, 3>& colour) -> std::size_t
{
    std::size_t count{ 0 };
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
    {
        bool match = true;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const auto value = std::to_integer<int>(pixels[i + channel]);
            match = match && (colour[channel] == 0 ? value == 0 : value >= 200);
        }
        count += match ? 1 : 0;
    }
    return count;
}

// How many pixels of the last frame show each of the three colours.
struct Counts
{
    std::size_t cyan{ 0 };
    std::size_t yellow{ 0 };
    std::size_t magenta{ 0 };
};

// What a run does to the loaded scene's assets before it renders; nothing for the first two.
using Prepare = std::function<std::expected<void, std::string>(AssetLibrary& assets)>;

// Renders the scene file of 'project' headless with a fresh engine (and so a fresh asset library) and counts the
// colours of its last frame.
auto render(const Options& options, const std::filesystem::path& project, const std::filesystem::path& frameFile, const Prepare& prepare)
    -> std::expected<Counts, std::string>
{
    {
        auto engine = Engine::create({
            .title = "aegis-test",
            .width = sceneExtent.x,
            .height = sceneExtent.y,
            .headless = true,
            .frames = options.frames,
            .screenshot = frameFile.string(),
            .gpuDriven = options.gpuDriven,
            .project = project,
        });
        if (!engine)
            return std::unexpected{ std::format("Failed to create the engine: {}", describe(engine.error())) };
        if (options.gpuDriven && !(*engine)->useGpuDrivenRendering())
            return std::unexpected{ std::string{ skipMeshShaders } };

        const auto scene = (*engine)->assets().find(sceneFile);
        if (!scene)
            return std::unexpected{ std::format("'{}' is not in the temporary project", sceneFile) };
        if (auto loaded = (*engine)->loadScene(*scene); !loaded)
            return std::unexpected{ describe(loaded.error()) };
        if (prepare)
        {
            if (auto prepared = prepare((*engine)->assets()); !prepared)
                return std::unexpected{ prepared.error() };
        }
        if ((*engine)->run() != exitSuccess)
            return std::unexpected{ "the run failed (see the errors above)" };
    }

    int width{ 0 };
    int height{ 0 };
    int channels{ 0 };
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> image{
        stbi_load(frameFile.string().c_str(), &width, &height, &channels, 4), &stbi_image_free
    };
    if (!image)
        return std::unexpected{ std::format("failed to read the frame '{}'", frameFile.string()) };
    const auto pixels = std::as_bytes(std::span{ image.get(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4 });
    return Counts{ .cyan = colourPixels(pixels, cyan), .yellow = colourPixels(pixels, yellow), .magenta = colourPixels(pixels, magenta) };
}

// The third run: the loaded material (the yellow file, masked and double-sided) takes a magenta copy of the file, which
// names no modes.
auto applyMagenta(AssetLibrary& assets) -> std::expected<void, std::string>
{
    const auto material = assets.find(materialFile);
    if (!material)
        return std::unexpected{ std::format("'{}' is not in the temporary project", materialFile) };
    const auto loaded = assets.load<assets::Material>(*material);
    if (!loaded)
        return std::unexpected{ std::format("'{}' did not load", materialFile) };
    if (loaded->alphaMode() != assets::AlphaMode::Mask || loaded->cullMode() != rhi::CullMode::None)
        return std::unexpected{ std::format("'{}' loaded without its file's alpha mode Mask and cull mode None", materialFile) };

    auto file = serialize::parseMaterial(materialText(magenta));
    if (!file)
        return std::unexpected{ serialize::describe(file.error()) };
    if (auto applied = assets.applyMaterialFile(*material, *file); !applied)
        return std::unexpected{ describe(applied.error()) };
    if (loaded->alphaMode() != assets::AlphaMode::Opaque || loaded->cullMode() != rhi::CullMode::Back)
        return std::unexpected{ "the applied copy did not give the material the default alpha and cull mode again" };
    return {};
}
}

auto runMaterialFile(const Options& options) -> int
{
    const TemporaryFolder project{ std::filesystem::temp_directory_path() / std::format("aegis-test-material-{:016x}", std::random_device{}() * 0x100000001ull) };
    const auto content = project.path / "content";
    // Headless, the last frame goes to --screenshot, else next to the project; the second run's overwrites the first's.
    const std::filesystem::path frameFile = options.screenshot.empty() ? project.path / "frame.png" : std::filesystem::path{ options.screenshot };

    const auto fail = [](std::string_view message) {
        std::println("Material file check failed: {}", message);
        return exitFailure;
    };
    for (const auto& [file, text] : { std::pair{ content / materialFile, materialText(cyan) }, std::pair{ content / sceneFile, sceneText() } })
    {
        if (auto written = writeText(file, text); !written)
            return fail(written.error());
    }

    // A pixel count, not a reference image: software rendering differs in the last bits, and the cube covers about
    // 40000 pixels of the 800x600 frame.
    constexpr std::size_t minimum{ 10000 };
    std::array<Counts, 3> counts{};
    for (std::size_t run = 0; run < counts.size(); ++run)
    {
        if (run == 1)
        {
            if (auto written = writeText(content / materialFile, materialText(yellow, true)); !written)
                return fail(written.error());
        }
        auto rendered = render(options, project.path, frameFile, run == 2 ? Prepare{ applyMagenta } : Prepare{});
        if (!rendered)
        {
            if (rendered.error() == skipMeshShaders)
            {
                std::println("{}", skipMeshShaders);
                return skipExitCode;
            }
            return fail(rendered.error());
        }
        counts[run] = *rendered;
    }

    if (counts[0].cyan < minimum || counts[0].yellow != 0)
        return fail(std::format("the first file's cube shows {} cyan and {} yellow pixels, expected at least {} and none", counts[0].cyan, counts[0].yellow, minimum));
    if (counts[1].yellow < minimum || counts[1].cyan != 0)
        return fail(std::format("the changed file's cube shows {} yellow and {} cyan pixels, expected at least {} and none", counts[1].yellow, counts[1].cyan, minimum));
    if (counts[2].magenta < minimum || counts[2].yellow != 0)
        return fail(std::format("the applied copy's cube shows {} magenta and {} yellow pixels, expected at least {} and none", counts[2].magenta, counts[2].yellow, minimum));
    std::println("Material file verified: the cube showed the file's emissive colour ({} pixels), the changed file's ({} pixels) and the applied copy's ({} pixels)",
        counts[0].cyan, counts[1].yellow, counts[2].magenta);
    return exitSuccess;
}
}
