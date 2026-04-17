export module Aegis.Core.Layer;

export namespace Aegis::Core
{
	class Layer
	{
	public:
		virtual ~Layer() = default;

		virtual void onAttach() {}
		virtual void onDetach() {}
		virtual void onUpdate(float deltaSeconds) {}
		virtual void onUIRender() {}
	};
}
