/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2019-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */

#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "host_device.h"
#include "sampling.glsl"
#include "raycommon.glsl"


// clang-format off
layout(location = 0) rayPayloadInEXT HitPayload payload;
layout(set = 1, binding = eFrameInfo) uniform FrameInfo_ { FrameInfo frameInfo; };
layout(set = 2, binding = eHdr) uniform sampler2D hdrTexture;
// clang-format on

void main()
{

  vec2 uv  = getSphericalUv(gl_WorldRayDirectionEXT);  // See sampling.glsl
  vec3 env = texture(hdrTexture, uv).rgb;

  payload.contrib = env * frameInfo.clearColor.xyz;
  payload.hitT    = INFINITE;  // Ending trace
}
