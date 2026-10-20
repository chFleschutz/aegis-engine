import aegis.engine;

class TemplateScene : public aegis::SceneDescription
{
public:
    /// @brief All objects in a scene are created here
    void initialize(aegis::SceneContext& context) override
    {
        [[maybe_unused]] auto& [scene, scripts, assets] = context;
        using namespace aegis;

        // Create your scene here
    }
};

auto main() -> int
{
    aegis::Engine engine;
    engine.loadScene<TemplateScene>();
    engine.run();
}