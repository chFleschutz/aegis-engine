import Aegis.Engine;

class TemplateScene : public Aegis::SceneDescription
{
public:
	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene, Aegis::Scripting::ScriptManager& scripts) override
	{
		using namespace Aegis;

		// Create your scene here 

	}
};

auto main() -> int
{
	Aegis::Engine engine;
	engine.loadScene<TemplateScene>();
	engine.run();
}