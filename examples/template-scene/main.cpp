import aegis.editor;
import aegis.engine;

class TemplateScene : public aegis::SceneDescription
{
public:
    /// @brief All objects in a scene are created here
    void initialize(aegis::SceneContext& context) override
    {
        [[maybe_unused]] auto& [scene, types, assets] = context;
        aegis::addDefaults(context);
        using namespace aegis;

        // Create your scene here
    }
};

auto main(int argc, char** argv) -> int
{
    auto engine = aegis::createEngine(argc, argv);
    if (!engine)
        return engine.error();
    (*engine)->pushLayer<aegis::editor::EditorLayer>();
    (*engine)->loadScene<TemplateScene>();
    return (*engine)->run();
}