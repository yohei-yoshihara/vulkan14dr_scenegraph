#version 450

// シーン共通
layout(set = 0, binding = 0) uniform SceneUBO {
  mat4 view;
  mat4 proj;
  vec3 lightPos;
} sceneUBO;

// モデル毎
layout(set = 1, binding = 0) uniform UniformBufferObject {
  mat4 model;
  mat4 shadowMatrix;
  uint texIndex;
} ubo;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texCoord;

layout(location = 0) out vec2 out_texCoord;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec3 out_lightDir;
layout(location = 3) out vec4 out_shadowCoord;
layout(location = 4) out flat uint out_texIndex;

const mat4 biasMat = mat4( 
  0.5, 0.0, 0.0, 0.0,
  0.0, 0.5, 0.0, 0.0,
  0.0, 0.0, 1.0, 0.0,
  0.5, 0.5, 0.0, 1.0 );

void main()
{
  mat4 mvp = sceneUBO.proj * sceneUBO.view * ubo.model;
  gl_Position = mvp * vec4(in_position, 1.0);

  out_normal = mat3(ubo.model) * in_normal;

  out_texCoord = in_texCoord;

  vec3 worldPos = vec3(ubo.model * vec4(in_position, 1.0));
  out_lightDir = sceneUBO.lightPos - worldPos;

  out_shadowCoord = ubo.shadowMatrix * vec4(in_position, 1.0);  

  out_texIndex = ubo.texIndex;
}