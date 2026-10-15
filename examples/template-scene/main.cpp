import aegis.engine;

class TemplateScene : public aegis::SceneDescription
{
public:
    /// @brief All objects in a scene are created here
    void initialize(aegis::scene::Scene& scene, aegis::scripting::ScriptManager& scripts) override
    {
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