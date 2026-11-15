// The layout stub aegis_add_shaders (AegisShaders.cmake) compiles once per surface shader, with SURFACE_MODULE
// naming it: its push constant points at the surface's parameter block, so slangc -reflection-json describes the
// block (field names, offsets, sizes, types) for the material template loader to check templates against.
import SURFACE_MODULE;

struct LayoutConstants
{
    Surface.Params* params;
}

[[vk::push_constant]] ConstantBuffer<LayoutConstants> push;

[shader("compute")]
[numthreads(1, 1, 1)]
func main()
{
}
