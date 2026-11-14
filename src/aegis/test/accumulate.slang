import modules.bindless;

// checks/graph.cpp, verifyGraphHistory: every texel of the output becomes the input's plus one, or one
// when the input holds nothing yet. Input and output may be the same image.

struct AccumulateConstants
{
    bindless::StorageImageDescriptor input;
    bindless::StorageImageDescriptor output;
    uint fresh; // The input has no contents: start over
}

[[vk::push_constant]] ConstantBuffer<AccumulateConstants> push;

[shader("compute")]
[numthreads(8, 8, 1)]
func accumulateMain(uint3 id : SV_DispatchThreadID)
{
    let outputImage = bindless::storage2D(push.output);
    uint2 size;
    outputImage.GetDimensions(size.x, size.y);
    if (any(id.xy >= size))
        return;

    float4 before = float4(0.0);
    if (push.fresh == 0)
        before = bindless::storage2D(push.input)[id.xy];
    outputImage[id.xy] = before + 1.0;
}
