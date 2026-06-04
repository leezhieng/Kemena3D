# Materials &amp; Shaders

Kemena3D materials are **shader-driven**: the editor's Material inspector is not
hardcoded — it is generated from the shader the material uses. A shader declares
which of its uniforms are material parameters with `// @var` comments, and the
inspector renders one control per declaration.

```
.mat (shader + param values)  →  inspector reads the shader's @var list
                              →  one GUI control per parameter
                              →  values pushed into the matching uniforms at render time
```

---

## The `@var` annotation

Add a comment anywhere in a shader's source:

```glsl
// @var <type> <uniformName> [Display Label]
```

- **`<type>`** — the GLSL type. Determines both the value storage and the GUI control.
- **`<uniformName>`** — the exact uniform the value is bound to. May be a struct
  member path (e.g. `material.diffuse`).
- **`[Display Label]`** — optional. Shown in the inspector. If omitted, a label is
  derived from the name (a leading `material.` and a trailing `Map` are stripped,
  first letter capitalised — so `albedoMap` → "Albedo").

### Supported types &amp; controls

| `@var` type   | Inspector control            | Stored in `.mat` as            |
|---------------|------------------------------|--------------------------------|
| `float`       | drag slider                  | number                         |
| `int`         | drag int                     | number                         |
| `bool`        | checkbox                     | boolean                        |
| `vec2`        | 2-component drag             | `[x, y]`                       |
| `vec3`        | colour picker                | `[r, g, b]`                    |
| `vec4`        | colour picker (with alpha)   | `[r, g, b, a]`                 |
| `sampler2D`   | **texture dropdown** (UUID)  | image-asset UUID string        |
| `samplerCube` | texture dropdown (UUID)      | image-asset UUID string        |

Sampler dropdowns list **only image assets whose Image Type is `Texture`** (GUI
and Sprite images are excluded), and also accept a texture dragged from the
Project panel. When a texture is bound, the renderer also sets a companion
`has_<uniformName>` bool uniform to `true`, so shaders can branch on presence:

```glsl
uniform sampler2D albedoMap;
uniform bool      has_albedoMap;
...
vec4 base = has_albedoMap ? texture(albedoMap, uv) : vec4(1.0);
```

---

## Shader source format

A shader's vertex and fragment stages live in one source, separated by markers
(geometry is optional):

```glsl
// --- VERTEX ---
#version 330 core
...

// --- FRAGMENT ---
#version 330 core
...
```

`@var` comments can appear in either stage; all are collected.

---

## Two kinds of shader

### Built-in shaders

`Unlit`, `Phong`, and `PBR` ship with the engine and are already annotated. A
material selects one by name; the inspector shows that shader's parameters
(e.g. PBR exposes Base Color, Metallic, Roughness, and the Albedo / Normal /
Metal-Rough / AO / Emissive texture slots).

### Raw shader assets

Hand-written GLSL files you author and version with your project.

1. **Create** one: Project panel ▸ right-click ▸ **Create ▸ Raw Shader** (or the
   main menu's Create ▸ Raw Shader). This writes a `New Raw Shader.glsl` seeded
   with a working unlit/textured template and example `@var` lines.
2. **Edit** it in any text editor — change the code, add/remove `@var` lines.
3. **Use** it: in a material, open the **Shader** dropdown — every `.glsl`/`.hlsl`
   shader in the project appears alongside the built-ins. Selecting one stores
   its asset UUID in the `.mat`.
4. Saving an edited shader recompiles it the next time a material using it is
   built, so the inspector and viewport update.

> The renderer is OpenGL/GLSL. `.hlsl` files are recognised as shader assets but
> will not compile until a D3D backend exists — author raw shaders in GLSL.

---

## A minimal example

```glsl
// --- VERTEX ---
#version 330 core
layout(location = 0) in vec3 vertexPosition;
layout(location = 2) in vec2 texCoord;
uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
out vec2 texCoordFrag;
void main()
{
    texCoordFrag = texCoord;
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(vertexPosition, 1.0);
}

// --- FRAGMENT ---
#version 330 core
in vec2 texCoordFrag;
out vec4 fragColor;

// @var vec3      tint      Tint
// @var sampler2D albedoMap Albedo
uniform vec3      tint;
uniform sampler2D albedoMap;
uniform bool      has_albedoMap;

void main()
{
    vec4 base = has_albedoMap ? texture(albedoMap, texCoordFrag) : vec4(1.0);
    fragColor = vec4(tint, 1.0) * base;
}
```

In the inspector this material shows a **Tint** colour picker and an **Albedo**
texture dropdown — nothing about those controls is hardcoded in the editor.

---

## `.mat` file format

```json
{
  "uuid": "…",
  "shader": "Phong",          // built-in name, or "Custom" for a raw shader
  "shader_uuid": "",          // raw shader asset UUID (empty for built-ins)
  "single_sided": true,       // material render state (not a shader uniform)
  "params": {
    "material.diffuse":   [1.0, 0.5, 0.2],
    "material.roughness": 0.4,
    "albedoMap":          "f9c2…"   // image-asset UUID
  }
}
```

Values are keyed by uniform name under `params`. Materials saved before the
`@var` system (with flat `diffuse`/`ambient`/… keys) are migrated to this format
automatically when opened.

---

## How values reach the GPU

`kMaterial` carries a generic `name → {type, value/texture}` map
(`setParamVec3`, `setParamSampler2D`, …). Both the main renderer and the
offscreen renderer iterate it and push each parameter to the uniform of the same
name; sampler parameters bind their texture to a free unit and set the sampler +
`has_<name>` flag. See `kmaterial.h` and the `@var` blocks in `krenderer.cpp` /
`koffscreenrenderer.cpp`.
