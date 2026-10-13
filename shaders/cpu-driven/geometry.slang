import modules.bindless;
import modules.common;
import modules.tbn;

// CPU-driven G-buffer pass: one draw per entity, vertices pulled through the mesh's device address
// (the index buffer is bound, so SV_VertexID is the vertex index).

struct PushConstants
{
    float3x4 model; // Top three rows of the model matrix
    float3 normalRow0;
    float3 normalRow1;
    float3 normalRow2;
    common::Camera* camera;
    common::Mesh* mesh;
    common::PbrMaterial* material;

    property float4x4 modelMatrix
    {
        get { return float4x4(model, float4(0.0, 0.0, 0.0, 1.0)); }
    }

    property float3x3 normalMatrix
    {
        get { return float3x3(normalRow0, normalRow1, normalRow2); }
    }
}

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

struct VSOut
{
    float4 position : SV_Position;
    [vk::location(0)] float3 worldPosition;
    [vk::location(1)] float3 worldNormal;
    [vk::location(2)] float2 uv;
}

[shader("vertex")]
func vertexMain(uint vertexID : SV_VertexID, out VSOut output)
{
    let vertex = pc.mesh->vertices[vertexID];
    let camera = pc.camera;

    float4 worldPosition = mul(pc.modelMatrix, float4(vertex.position, 1.0));
    output.position = mul(camera->viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = normalize(mul(pc.normalMatrix, vertex.normal));
    output.uv = vertex.uv;
}

[shader("fragment")]
func fragmentMain(in VSOut input, out common::GBuffer output)
{
    let material = pc.material;

    float3 albedo = material->albedoMap.sample(input.uv).rgb * material->albedo;
    float3 normal = material->normalMap.sample(input.uv).rgb * 2.0 - 1.0;
    float3 emissive = material->emissiveMap.sample(input.uv).rgb * material->emissive;
    float2 metallicRoughness = material->metalRoughnessMap.sample(input.uv).bg;
    float metallic = metallicRoughness.r * material->metallic;
    float roughness = metallicRoughness.g * material->roughness;
    float ao = material->ambientOcclusionMap.sample(input.uv).r * material->ambientOcclusion;

    let tbn = TBN::calcMatrix(input.worldPosition, input.worldNormal, input.uv);
    float3 N = normalize(length(normal) < 0.1 ? input.worldNormal : mul(normal, tbn));

    output.position = float4(input.worldPosition, 1.0);
    output.normal = float4(N, 0.0);
    output.albedo = float4(albedo, 1.0);
    output.arm = float4(ao, roughness, metallic, 0.0);
    output.emissive = float4(emissive, 1.0);
}
