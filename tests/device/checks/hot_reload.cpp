module aegis.test;

import std;
import aegis.asset_library;
import aegis.assets;
import aegis.core;
import aegis.engine;
import aegis.render_passes;
import aegis.world;

// --hot-reload: a texture changed on disk reaches the running engine (AssetLibrary::rescan and reload, what the editor
// polls). A temporary project holds a 4x4 texture, a material that shows it as its emissive map and a scene of the
// engine's cube under a camera, lit by nothing, so the cube shows only the texture's colour. One engine renders it red,
// then the shell overwrites the texture blue, rescans and reloads: the next frames must show blue, through the same
// texture object (the material keeps its descriptor). Deleting the texture falls back to the engine's checker (magenta
// and black), and writing it back brings the blue back. A glTF of two meshes, A and B, each drawn by an entity, then loses
// B: its part falls back to the engine's cube with one warning while A gets its new contents, and B comes back with its
// ID once the file has it again.
namespace aegis::test
{
namespace
{
constexpr std::string_view textureFile{ "textures/swatch.bmp" };
constexpr std::string_view materialFile{ "materials/swatch.material" };
constexpr std::string_view sceneFile{ "scenes/hot_reload.scene" };
constexpr std::string_view modelFile{ "models/parts.gltf" };
constexpr assets::AssetId textureId{ 0x5d0c9e2a71b4f386 };
constexpr assets::AssetId modelId{ 0x93b1f5e07a2c6d48 };
constexpr assets::AssetId meshA{ 0x1c7f2a09e3b5d864 }; // "StaticMesh/A/0" in the model's sidecar
constexpr assets::AssetId meshB{ 0x6e48d3b1f07a2c95 }; // "StaticMesh/B/0"

// The engine content's cube mesh (content/cube.obj.meta, "StaticMesh/cube/0").
constexpr std::string_view cubeMesh{ "ebb588c4f30352a9" };

using Colour = std::array<std::uint8_t, 3>;
constexpr Colour red{ 255, 0, 0 };
constexpr Colour blue{ 0, 0, 255 };

// A 4x4 24-bit BMP of one colour (the texture importer reads BMP like PNG; it is simpler to write).
auto bmp(const Colour& colour) -> std::string
{
    constexpr std::uint32_t side{ 4 };
    constexpr std::uint32_t rowSize{ side * 3 }; // 12: already a multiple of 4
    constexpr std::uint32_t pixelOffset{ 14 + 40 };
    constexpr std::uint32_t fileSize{ pixelOffset + rowSize * side };
    std::string bytes;
    const auto put = [&bytes](std::uint32_t value, int size) {
        for (int i = 0; i < size; ++i)
            bytes.push_back(static_cast<char>((value >> (8 * i)) & 0xffu));
    };
    bytes += "BM";
    put(fileSize, 4);
    put(0, 4);
    put(pixelOffset, 4);
    put(40, 4); // BITMAPINFOHEADER
    put(side, 4);
    put(side, 4);
    put(1, 2);  // Planes
    put(24, 2); // Bits per pixel
    put(0, 4);  // BI_RGB
    put(rowSize * side, 4);
    put(2835, 4);
    put(2835, 4);
    put(0, 4);
    put(0, 4);
    for (std::uint32_t pixel = 0; pixel < side * side; ++pixel)
    {
        bytes.push_back(static_cast<char>(colour[2])); // BGR
        bytes.push_back(static_cast<char>(colour[1]));
        bytes.push_back(static_cast<char>(colour[0]));
    }
    return bytes;
}

auto sidecarText() -> std::string
{
    assets::Sidecar sidecar{ .id = textureId, .importer = "texture" };
    sidecar.settings.srgb = true;
    return assets::writeSidecar(sidecar);
}

auto base64(std::span<const std::byte> bytes) -> std::string
{
    constexpr std::string_view digits{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" };
    std::string text;
    for (std::size_t i = 0; i < bytes.size(); i += 3)
    {
        std::uint32_t chunk = std::to_integer<std::uint32_t>(bytes[i]) << 16u;
        if (i + 1 < bytes.size())
            chunk |= std::to_integer<std::uint32_t>(bytes[i + 1]) << 8u;
        if (i + 2 < bytes.size())
            chunk |= std::to_integer<std::uint32_t>(bytes[i + 2]);
        text += digits[(chunk >> 18u) & 63u];
        text += digits[(chunk >> 12u) & 63u];
        text += i + 1 < bytes.size() ? digits[(chunk >> 6u) & 63u] : '=';
        text += i + 2 < bytes.size() ? digits[chunk & 63u] : '=';
    }
    return text;
}

// A glTF of one triangle per mesh, each mesh on a node of its own: A, and B unless 'withB' is false. The buffer is in a
// data URI, so the file is the whole model.
auto gltfText(bool withB) -> std::string
{
    constexpr std::array<float, 18> positions{ -1, 0, 0, 0, 0, 0, -1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const auto buffer = base64(std::as_bytes(std::span{ positions }));
    const auto accessor = [](int view) {
        return std::format(R"({{ "bufferView": {}, "componentType": 5126, "count": 3, "type": "VEC3", "min": [-1, 0, 0], "max": [1, 0, 1] }})", view);
    };
    return std::format(R"({{
  "asset": {{ "version": "2.0" }},
  "scene": 0,
  "scenes": [ {{ "nodes": [{}] }} ],
  "nodes": [ {{ "name": "A", "mesh": 0 }}{} ],
  "meshes": [ {{ "name": "A", "primitives": [ {{ "attributes": {{ "POSITION": 0 }} }} ] }}{} ],
  "buffers": [ {{ "byteLength": 72, "uri": "data:application/octet-stream;base64,{}" }} ],
  "bufferViews": [ {{ "buffer": 0, "byteOffset": 0, "byteLength": 36 }}, {{ "buffer": 0, "byteOffset": 36, "byteLength": 36 }} ],
  "accessors": [ {}, {} ]
}}
)",
        withB ? "0, 1" : "0", withB ? R"(, { "name": "B", "mesh": 1 })" : "",
        withB ? R"(, { "name": "B", "primitives": [ { "attributes": { "POSITION": 1 } } ] })" : "", buffer, accessor(0), accessor(1));
}

auto modelSidecarText() -> std::string
{
    assets::Sidecar sidecar{ .id = modelId, .importer = "model" };
    sidecar.subAssets.emplace("StaticMesh/A/0", meshA);
    sidecar.subAssets.emplace("StaticMesh/B/0", meshB);
    return assets::writeSidecar(sidecar);
}

auto materialText() -> std::string
{
    return std::format(R"({{
  "type": "Material",
  "id": "8e31b07c4a5d92f6",
  "version": 1,
  "template": "{}",
  "parameters": {{
    "albedo": [0, 0, 0],
    "emissive": [1, 1, 1],
    "emissiveMap": "{}",
    "metallic": 0,
    "roughness": 1
  }}
}}
)",
        assets::engine::pbrTemplate, textureId);
}

auto sceneText() -> std::string
{
    return std::format(R"({{
  "type": "Scene",
  "id": "c4a29e70b15d38f6",
  "version": 1,
  "types": {{ "Camera": 1, "MainCamera": 1, "MeshRenderer": 1, "Sky": 1, "Transform": 1 }},
  "entities": [
    {{
      "id": "2b7e93d0c4a1f658",
      "name": "Camera",
      "children": [],
      "components": {{
        "Camera": {{ "fov": 1.0471976, "near": 0.1, "far": 100 }},
        "MainCamera": {{}},
        "Transform": {{ "location": [0, -3, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }}
      }}
    }},
    {{
      "id": "7d05a1e8f36b92c4",
      "name": "Sky",
      "children": [],
      "components": {{
        "Sky": {{ "environment": null }},
        "Transform": {{ "location": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }}
      }}
    }},
    {{
      "id": "e9c3f41a68b27d05",
      "name": "Cube",
      "children": [],
      "components": {{
        "MeshRenderer": {{ "mesh": "{}", "material": "8e31b07c4a5d92f6" }},
        "Transform": {{ "location": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }}
      }}
    }},
    {{
      "id": "41d8a7c0e35f29b6",
      "name": "A",
      "children": [],
      "components": {{
        "MeshRenderer": {{ "mesh": "{}", "material": "8e31b07c4a5d92f6" }},
        "Transform": {{ "location": [0, 20, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }}
      }}
    }},
    {{
      "id": "a6f3029e7d1c4b58",
      "name": "B",
      "children": [],
      "components": {{
        "MeshRenderer": {{ "mesh": "{}", "material": "8e31b07c4a5d92f6" }},
        "Transform": {{ "location": [0, 20, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }}
      }}
    }}
  ]
}}
)",
        cubeMesh, meshA, meshB);
}

auto writeFile(const std::filesystem::path& file, std::string_view bytes) -> std::expected<void, std::string>
{
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    {
        std::ofstream stream{ file, std::ios::binary | std::ios::trunc };
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (error || !stream)
            return std::unexpected{ std::format("cannot write '{}'", file.string()) };
    }
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

// The pixels of 'pixels' (RGBA8) that show 'colour' (each channel full or off) as an emissive colour, unlit: a full
// channel bright, an off one exactly 0, which no light or bloom raises.
auto colourPixels(std::span<const std::byte> pixels, const Colour& colour) -> std::size_t
{
    std::size_t count{ 0 };
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
    {
        bool match = true;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const auto value = std::to_integer<int>(pixels[i + channel]);
            match = match && (colour[channel] == 0 ? value == 0 : value >= 100);
        }
        count += match ? 1 : 0;
    }
    return count;
}

constexpr Colour magenta{ 255, 0, 255 }; // The checker's bright squares

// The steps of the run, by frame: each reads the frame before it, then changes the texture's file and reloads it; the
// last two change the model's.
constexpr std::uint32_t blueFrame{ 4 };
constexpr std::uint32_t removeFrame{ 8 };
constexpr std::uint32_t restoreFrame{ 12 };
constexpr std::uint32_t partRemoveFrame{ 16 };
constexpr std::uint32_t partRestoreFrame{ 20 };
constexpr std::uint32_t lastFrame{ partRestoreFrame };
constexpr std::size_t steps{ 6 };

// Reads the main view each step, and writes, deletes and rewrites the texture's file, reloading after each.
class HotReloadShell final : public Shell
{
public:
    HotReloadShell(std::filesystem::path content, std::vector<std::string>& errors) :
        m_content{ std::move(content) },
        m_errors{ errors }
    {
    }

    auto onStartup(Engine& engine) -> std::expected<void, EngineError> override
    {
        m_engine = &engine;
        // Counts the warnings that name B, next to stderr
        m_sink = log::addSink([this](log::Level level, std::string_view message) {
            if (level == log::Level::Warn && message.contains(meshB.toString()))
                ++m_warningsOfB;
        });
        return {};
    }

    void onShutdown() override { log::removeSink(m_sink); }

    void onFrame(FrameContext& frame) override
    {
        // Each step reads the frame right after a change for its exact colours: without temporal AA's history.
        if (m_frame == 0)
        {
            if (auto disabled = disableTemporalAa(frame.frameGraph); !disabled)
                m_errors.push_back(std::format("Disabling temporal AA failed: {}", disabled.error()));
        }
        frame.update(frame.world, world::UpdateMode::Play);
        if (auto stepped = step(frame); !stepped)
            m_errors.push_back(stepped.error());
        ++m_frame;
    }

    [[nodiscard]] auto renderViews() const -> std::vector<RenderView> override { return { RenderView{ .extent = sceneExtent } }; }

    [[nodiscard]] auto counts() const -> const std::vector<std::string>& { return m_counts; }

private:
    auto step(FrameContext& frame) -> std::expected<void, std::string>
    {
        if (m_frame == partRemoveFrame || m_frame == partRestoreFrame)
            return changeModel(frame);
        if (m_frame != blueFrame && m_frame != removeFrame && m_frame != restoreFrame && m_frame != partRemoveFrame - 1)
            return {};

        auto pixels = m_engine->readView(render_graph::names::mainView);
        if (!pixels)
            return std::unexpected{ std::format("reading the view back failed: {}", pixels.error()) };
        const auto redPixels = colourPixels(*pixels, red);
        const auto bluePixels = colourPixels(*pixels, blue);
        const auto checkerPixels = colourPixels(*pixels, magenta);
        // The cube covers about 40000 pixels of the 800x600 frame; the checker's bright half about half of that.
        constexpr std::size_t minimum{ 5000 };
        const auto expect = [&](std::string_view when, std::string_view shows, std::size_t count, std::size_t others) -> std::expected<void, std::string> {
            if (count < minimum || others != 0)
                return std::unexpected{ std::format("{}, the cube shows {} {} pixels and {} of the other colours, expected at least {} and none",
                    when, count, shows, others, minimum) };
            m_counts.push_back(std::format("{} ({} pixels)", shows, count));
            return {};
        };

        const auto texturePath = m_content / textureFile;
        if (m_frame == blueFrame)
        {
            if (auto shown = expect("Before the reload", "red", redPixels, bluePixels + checkerPixels); !shown)
                return shown;
            m_texture = frame.assets.loadTexture(textureId).get();
            if (auto written = writeFile(texturePath, bmp(blue)); !written)
                return written;
        }
        else if (m_frame == removeFrame)
        {
            if (auto shown = expect("After the texture was rewritten", "blue", bluePixels, redPixels + checkerPixels); !shown)
                return shown;
            if (frame.assets.loadTexture(textureId).get() != m_texture)
                return std::unexpected{ "the reload replaced the texture's object instead of updating it" };
            std::error_code error;
            std::filesystem::remove(texturePath, error);
            if (error)
                return std::unexpected{ std::format("cannot delete '{}': {}", texturePath.string(), error.message()) };
        }
        else if (m_frame == restoreFrame)
        {
            if (auto shown = expect("After the texture was deleted", "checker", checkerPixels, redPixels + bluePixels); !shown)
                return shown;
            if (auto written = writeFile(texturePath, bmp(blue)); !written)
                return written;
        }
        else
        {
            return expect("After the texture was written back", "blue", bluePixels, redPixels + checkerPixels);
        }
        return reload(frame, textureId).transform([](bool) {});
    }

    // The model loses B, then gets it back.
    auto changeModel(FrameContext& frame) -> std::expected<void, std::string>
    {
        const auto a = frame.assets.loadMesh(meshA);
        const auto b = frame.assets.loadMesh(meshB);
        const auto* aBefore = a.get();
        // B is the failed part the step before left, before it comes back
        const auto bBefore = m_frame == partRemoveFrame ? assets::AssetState::Ready : assets::AssetState::Failed;
        if (a.state() != assets::AssetState::Ready || b.state() != bBefore)
            return std::unexpected{ "the model's meshes are not as the step before left them" };
        if (auto written = writeFile(m_content / modelFile, gltfText(m_frame != partRemoveFrame)); !written)
            return written;
        auto resync = reload(frame, modelId);
        if (!resync)
            return std::unexpected{ resync.error() };
        if (!*resync)
            return std::unexpected{ "the reload of a model did not ask for the static instances to be read again" };
        frame.world.registry().invalidateStatic(); // As the editor does
        if (a.get() == aBefore || a.state() != assets::AssetState::Ready)
            return std::unexpected{ "the mesh A did not get the new import's contents" };

        if (m_frame == partRemoveFrame)
        {
            if (b.state() != assets::AssetState::Failed || !b.get() || !b.slot()->message.contains("StaticMesh/B/0"))
                return std::unexpected{ std::format("the removed mesh B is not a failed part with the cube: {}", b.slot()->message) };
            if (m_warningsOfB != 1)
                return std::unexpected{ std::format("the removed mesh B was warned about {} times, expected once", m_warningsOfB) };
            m_counts.push_back("B fell back");
            return {};
        }
        if (b.state() != assets::AssetState::Ready || frame.assets.find(modelFile, "StaticMesh/B/0") != meshB)
            return std::unexpected{ "the mesh B did not come back with its ID" };
        m_counts.push_back("B came back");
        return {};
    }

    // What the editor does at each poll; 'source' must be the one source found and reloaded.
    // Returns whether the reload asked for the static instances to be read again.
    auto reload(FrameContext& frame, assets::AssetId source) -> std::expected<bool, std::string>
    {
        auto changes = frame.assets.rescan();
        if (!changes)
            return std::unexpected{ std::format("the rescan failed: {}", describe(changes.error())) };
        const auto found = changes->changed.size() + changes->added.size() + changes->removed.size();
        if (found != 1)
            return std::unexpected{ std::format("the rescan found {} changed sources, expected one (the library's own sidecar writes are none)", found) };
        const auto reloaded = frame.assets.reload(*changes);
        if (!std::ranges::contains(reloaded.reloaded, source))
            return std::unexpected{ std::format("the reload did not reload {}", source) };
        if (source == textureId && reloaded.resync)
            return std::unexpected{ "the reload of a texture asked for the static instances to be read again" };
        return reloaded.resync;
    }

    std::filesystem::path m_content;
    std::vector<std::string>& m_errors;
    Engine* m_engine{ nullptr };
    std::uint32_t m_frame{ 0 };
    const assets::Texture* m_texture{ nullptr }; // The texture's object before the first reload
    std::vector<std::string> m_counts;           // What each step saw, for the summary
    log::SinkId m_sink{};
    int m_warningsOfB{ 0 };
};
}

auto runHotReload(const Options& options) -> int
{
    const TemporaryFolder project{ std::filesystem::temp_directory_path() / std::format("aegis-test-reload-{:016x}", std::random_device{}() * 0x100000001ull) };
    const auto content = project.path / "content";
    const auto fail = [](std::string_view message) {
        std::println("Hot reload check failed: {}", message);
        return exitFailure;
    };
    const auto texture = std::string{ textureFile };
    for (const auto& [file, bytes] : { std::pair{ content / textureFile, bmp(red) }, std::pair{ content / (texture + ".meta"), sidecarText() },
             std::pair{ content / materialFile, materialText() }, std::pair{ content / sceneFile, sceneText() },
             std::pair{ content / modelFile, gltfText(true) }, std::pair{ content / (std::string{ modelFile } + ".meta"), modelSidecarText() } })
    {
        if (auto written = writeFile(file, bytes); !written)
            return fail(written.error());
    }

    std::vector<std::string> errors;
    std::vector<std::string> counts;
    {
        auto engine = Engine::create({
            .title = "aegis-test",
            .width = sceneExtent.x,
            .height = sceneExtent.y,
            .headless = true,
            .frames = std::max(options.frames, lastFrame + 1),
            .screenshot = options.screenshot,
            .screenshotView = true,
            .gpuDriven = options.gpuDriven,
            .project = project.path,
        });
        if (!engine)
            return fail(std::format("failed to create the engine: {}", describe(engine.error())));
        if (options.gpuDriven && !(*engine)->useGpuDrivenRendering())
        {
            std::println("{}", skipMeshShaders);
            return skipExitCode;
        }

        auto shell = std::make_unique<HotReloadShell>(content, errors);
        const auto* hotReload = shell.get();
        (*engine)->setShell(std::move(shell));
        const auto scene = (*engine)->assets().find(sceneFile);
        if (!scene)
            return fail(std::format("'{}' is not in the temporary project", sceneFile));
        if (auto loaded = (*engine)->loadScene(*scene); !loaded)
            return fail(describe(loaded.error()));
        if ((*engine)->run() != exitSuccess)
            return fail("the run failed (see the errors above)");
        counts = hotReload->counts();
    }

    if (!errors.empty())
        return fail(errors.front());
    if (counts.size() != steps)
        return fail(std::format("the run took {} of its {} steps; it needs at least {} frames", counts.size(), steps, lastFrame + 1));
    std::println("Hot reload verified: the cube showed {}, then {} after a reload, {} once the texture was deleted and {} once it was back; "
                 "a glTF's removed part fell back with one warning and came back with its ID",
        counts[0], counts[1], counts[2], counts[3]);
    return exitSuccess;
}
}
