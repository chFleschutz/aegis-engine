#include "aegis/engine.h"
#include "aegis/scene/description.h"

class TemplateScene : public Aegis::Scene::Description
{
public:
	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene) override
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