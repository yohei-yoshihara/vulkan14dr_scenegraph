#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 in_texCoord;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_lightDir;
layout(location = 3) in vec4 in_shadowCoord;
layout(location = 4) in flat uint in_texIndex;

// シーン共通
layout(set = 0, binding = 1) uniform sampler2D shadowMap;
layout(set = 0, binding = 2) uniform UBO {
    vec3 lightColor;   // 光の色
    float intensity;   // 光の強さ
		float ambient;     // 環境光
} ubo;

// テクスチャ
layout(set = 2, binding = 0) uniform sampler texSampler;
layout(set = 2, binding = 1) uniform texture2D texImages[];

layout(location = 0) out vec4 out_color;

float textureProj(vec4 shadowCoord, vec2 off)
{
	float shadow = 1.0;
	if ( shadowCoord.z > -1.0 && shadowCoord.z < 1.0 ) 
	{
		float dist = texture( shadowMap, shadowCoord.st + off ).r;
		if ( shadowCoord.w > 0.0 && dist < shadowCoord.z ) 
		{
			shadow = ubo.ambient;
		}
	}
	return shadow;
}

float filterPCF(vec4 sc)
{
	ivec2 texDim = textureSize(shadowMap, 0);
	float scale = 1.5;
	float dx = scale * 1.0 / float(texDim.x);
	float dy = scale * 1.0 / float(texDim.y);

	float shadowFactor = 0.0;
	int count = 0;
	int range = 1;
	
	for (int x = -range; x <= range; x++)
	{
		for (int y = -range; y <= range; y++)
		{
			shadowFactor += textureProj(sc, vec2(dx*x, dy*y));
			count++;
		}
	
	}
	return shadowFactor / count;
}

void main()
{
	float shadow = filterPCF(in_shadowCoord / in_shadowCoord.w);
	vec4 textureColor = texture(sampler2D(texImages[nonuniformEXT(in_texIndex)], texSampler), in_texCoord);

  vec3 normal = normalize(in_normal);
  vec3 lightDir = normalize(in_lightDir);
  float diffuse = max(dot(normal, lightDir), 0.0);
	vec3 color = textureColor.rgb * ubo.lightColor * diffuse * shadow * ubo.intensity;
	out_color = vec4(color.rgb * shadow, textureColor.a);
}