export module Aegis.Graphics.RenderSystem;

import Aegis.Graphics.RenderContext;

export namespace Aegis::Graphics
{
	class RenderSystem
	{
	public:
		RenderSystem() = default;
		virtual ~RenderSystem() = default;

		virtual void render(const RenderContext& ctx) = 0;
	};
}