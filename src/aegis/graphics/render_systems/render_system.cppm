module;

#include "graphics/render_context.h"

export module Aegis.Graphics.RenderSystem;

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