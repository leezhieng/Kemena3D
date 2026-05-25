# Kemena3D SDK

Kemena3D is an open-source, cross-platform 3D rendering engine developed in C++. It’s designed for more than just game development, it’s also well-suited for simulations, interactive applications, architectural visualizations, and various other 3D use cases.

If you're looking for the game engine with complete editor - Kemena3D Studio, please visit the following page instead: https://github.com/leezhieng/kemena3d-studio

## Website

You can find the latest release, tutorials and additional information at: https://kemena3d.com

## Screenshots

![Phone Material on Mesh](Screenshots/phong_mesh.png)
![PBR Material with Shadow](Screenshots/pbr_shadow.jpg)
![Blur Screen Effect](Screenshots/blur.png)
![Edge Detection Screen Effect](Screenshots/edge.png)

## Scripting

Gameplay logic is scripted with [AngelScript](https://www.angelcode.com/angelscript/).
Scripts attach to scene objects, run standard lifecycle functions
(`Awake`, `Start`, `Update`, `FixedUpdate`, `LateUpdate`, `OnDestroy`), and are
compiled to bytecode for fast loading. A host API exposes objects, transforms,
timing and logging to scripts.

See [Documentation/Scripting.md](Documentation/Scripting.md) for the full guide.
