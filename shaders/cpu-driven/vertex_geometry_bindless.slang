import modules.common;
import modules.constants;
import modules.tbn;
import modules.bindless;

struct Global
{
    float4x4 projection;
    float4x4 view;
    float4x4 viewInverse;
}

struct Material
{
    float3 albedo;
    float3 emissive;
    float metallic;
    float roughness;
    float ao;
    bindless::Handle<SampledImage2D> albedoMap;
    bindless::Handle<SampledImage2D> normalMap;
    bindless::Handle<SampledImage2D> metalRoughnessMap;
    bindless::Handle<SampledImage2D> aoMap;
    bindless::Handle<SampledImage2D> emissiveMap;
};

struct PushConstants
{
    float3x4 modelRaw;
    float3 normalRow0;
    bindless::Handle<UniformBuffer<Global>> global;
    float3 normalRow1;
    bindless::Handle<Undefined> mesh;
    float3 normalRow2;
    bindless::Handle<UniformBuffer<Material>> material;

    property float4x4 modelMatrix
    {
        get { return float4x4(modelRaw, float4(0.0, 0.0, 0.0, 1.0)); }
    }

    property float3x3 normalMatrix
    {
        get { return float3x3(normalRow0, normalRow1, normalRow2); }
    }
};

struct VSOut
{
    float4 position : SV_Position;
    [vk::location(0)] float3 worldPosition;
    [vk::location(1)] float3 worldNormal;
    [vk::location(2)] float2 uv;
};

[vk::push_constant] PushConstants pc;

[shader("vertex")]
func vertexMain(in common::VertexIn input, out VSOut output)
{
    let global = pc.global.get();

    float4 worldPosition = mul(pc.modelMatrix, float4(input.position, 1.0));

    output.position = mul(global.projection, mul(global.view, worldPosition));
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = normalize(mul(pc.normalMatrix, input.normal));
    output.uv = input.uv;
}

[shader("fragment")]
func fragmentMain(in VSOut input, out common::GBuffer output)
{
    let mat = pc.material.get();

    float3 albedo = mat.albedoMap.get().Sample(input.uv).rgb * mat.albedo;
    float3 normal = mat.normalMap.get().Sample(input.uv).rgb * 2.0 - 1.0;
    float3 emissive = mat.emissiveMap.get().Sample(input.uv).rgb * mat.emissive;
    float2 metalicRoughness = mat.metalRoughnessMap.get().Sample(input.uv).bg;
    float metallic = metalicRoughness.r * mat.metallic;
    float roughness = metalicRoughness.g * mat.roughness;
    float ao = mat.aoMap.get().Sample(input.uv).r * mat.ao;

    let TBN = TBN::calcMatrix(input.worldPosition, input.worldNormal, input.uv);
    float3 N = normalize(length(normal) < 0.1 ? input.worldNormal : mul(normal, TBN));

    output.position = float4(input.worldPosition, 1.0);
    output.normal = float4(N, 0.0);
    output.albedo = float4(albedo, 1.0);
    output.arm = float4(ao, roughness, metallic, 0.0);
    output.emissive = float4(emissive, 1.0);
}
