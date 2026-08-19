# Kemena3D Scripting API Reference

This is the complete reference for everything a Kemena3D **AngelScript** script
can use: the lifecycle functions the engine calls, the global functions and
types the engine exposes (`kObject`, `kVec3`), and the standard-library pieces
that are available.

For the language itself (syntax, classes, control flow, operators) see the
official [AngelScript manual](https://www.angelcode.com/angelscript/sdk/docs/manual/).
For concepts (assets, components, bytecode, the compile pipeline) see
[Scripting.md](Scripting.md).

> **Source of truth:** the bindings are registered in
> `Source/src/kscriptbindings.cpp` and `Source/src/kscriptmanager.cpp`. If this
> document and the code ever disagree, the code wins — and the doc should be
> updated.

---

## 1. Anatomy of a script

A script is an `.as` file attached to a `kObject` (a scene object). It defines
any subset of the **lifecycle functions** below as plain free functions. The
engine calls them on the object the script is attached to; inside any of them
`getSelf()` returns that object.

```angelscript
// spin.as — rotate the object around the Y axis every frame
void Update()
{
    kObject@ self = getSelf();
    self.rotate(kVec3(0, 1, 0), 1.5 * getDeltaTime()); // 1.5 rad/sec
}
```

Each attached script runs as its **own module**, so global variables declared in
a script are *per-object instance* state — two objects with the same script do
not share globals.

```angelscript
float elapsed = 0.0f; // independent for every object using this script

void Update()
{
    elapsed += getDeltaTime();
}
```

You may also declare your own helper functions and (script) classes; only the
lifecycle names below are special.

---

## 2. Lifecycle functions

Define any subset. Missing ones are simply skipped. Signatures must match
exactly (all are `void name()`).

| Function | When it runs |
|----------|--------------|
| `void Awake()`       | Once, when play begins, before any `Start()`. |
| `void Start()`       | Once, after all `Awake()`s, before the first `Update()`. |
| `void Update()`      | Every frame. Use `getDeltaTime()` for frame-rate independence. |
| `void FixedUpdate()` | Every fixed physics step. Use `getFixedDeltaTime()`. |
| `void LateUpdate()`  | Every frame, after all `Update()`s (e.g. cameras that follow). |
| `void OnEnable()`    | When the object/script becomes active. |
| `void OnDisable()`   | When the object/script becomes inactive. |
| `void OnDestroy()`   | When the object is destroyed / play stops. |

```angelscript
void Awake()  { print("Awake: one-time setup"); }
void Start()  { print("Start: everything else is ready"); }
void Update() { /* per-frame gameplay */ }
void OnDestroy() { print("Goodbye"); }
```

---

## 3. Global functions

| Signature | Description |
|-----------|-------------|
| `kObject@ getSelf()`          | The object this script is attached to (a handle; may be reused via `@`). |
| `float getDeltaTime()`        | Seconds since the previous frame (varies). Multiply movement/rotation by this in `Update()`. |
| `float getFixedDeltaTime()`   | Seconds per fixed step (constant). Use in `FixedUpdate()`. |
| `void print(const string &in)`| Writes a line to the editor **Console** (prefixed `[Script]`). |
| `void log(const string &in)`  | Identical to `print` — an alias. |
| `void printConsole(const string &in)` | Alias that writes a line to the editor **Console** panel. |
| `bool getAction(const string &in)`        | True while the named action is held (keyboard, mouse, or gamepad). |
| `bool getActionPressed(const string &in)` | True only on the frame the named action was pressed. |
| `bool getActionReleased(const string &in)`| True only on the frame the named action was released. |
| `float getAxis(const string &in)`         | Current value of the named axis, in `[-1, 1]`. |
| `kAudio@ loadAudio(const string &in)`     | Loads an audio clip (WAV/MP3/OGG/FLAC). Manager-owned; see [`kAudio`](#6-type-kaudio). |
| `void unloadAudio(kAudio@)`               | Stops and destroys a previously loaded clip. |
| `void playAudio(const string &in, bool loop, float volume, float pitch)` | One-shot helper: loads, configures, and plays a clip. |
| `void stopAllAudio()`                     | Stops every clip currently managed by the audio system. |
| `void setMasterVolume(float)`             | Sets the global output volume (`0` mute, `1` full). |
| `float getMasterVolume()`                 | Reads the global output volume. |
| `void setListenerPosition(const kVec3 &in)`       | Moves the 3-D audio listener. |
| `void setListenerDirection(const kVec3 &in fwd, const kVec3 &in up)` | Orients the 3-D audio listener. |
| `void setListenerVelocity(const kVec3 &in)`       | Sets the listener velocity for Doppler shift. |
| `kAnimator@ getAnimator(kObject@)`        | Returns the skeletal animator of a mesh object, or `null`. |
| `void setPhysicsGravity(const kVec3 &in)` | Sets the global gravity vector (m/s²). |
| `kVec3 getPhysicsGravity()`               | Reads the global gravity vector. |

```angelscript
void Start()
{
    print("Hello from " + getSelf().getName());
    print("frame dt = " + getDeltaTime());   // numbers concatenate into strings
}
```

> There is currently **no** global function to find other objects by name/UUID
> from a script — a script reaches the scene graph through `getSelf()` and
> `getParent()` only.

---

## 4. Type: `kObject`

A handle to a scene object. Obtain one with `getSelf()` or `getParent()`. It is
engine-owned, so you hold it as a handle (`kObject@`) and never construct or
delete it.

### Identity

| Method | Description |
|--------|-------------|
| `string getName() const`            | The object's display name. |
| `void   setName(const string &in)`  | Rename the object. |
| `string getUuid() const`            | The object's stable unique id. |

### Transform

Position and scale are in the units you authored. **Rotation is exposed as Euler
angles in degrees** (friendlier than quaternions for gameplay).

| Method | Description |
|--------|-------------|
| `kVec3 getPosition() const`           | Local position (relative to parent). |
| `void  setPosition(const kVec3 &in)`  | Set local position. |
| `kVec3 getGlobalPosition() const`     | World-space position (read-only). |
| `kVec3 getScale() const`              | Local scale. |
| `void  setScale(const kVec3 &in)`     | Set local scale. |
| `kVec3 getRotation() const`           | Local rotation as Euler **degrees** (x=pitch, y=yaw, z=roll). |
| `void  setRotation(const kVec3 &in)`  | Set local rotation from Euler **degrees**. |
| `void  translate(const kVec3 &in)`    | Move by a delta: `position += delta`. |
| `void  rotate(const kVec3 &in axis, float angle)` | Rotate incrementally by `angle` **radians** around `axis` (axis is normalized). |

> **Units gotcha:** `getRotation`/`setRotation` use **degrees**, but
> `rotate(axis, angle)` uses **radians**. For continuous spin, prefer `rotate`
> with `angle = speed * getDeltaTime()`.

### Direction vectors

Unit vectors derived from the object's current rotation.

| Method | Description |
|--------|-------------|
| `kVec3 forward() const` | The object's local −Z (forward) direction in world space. |
| `kVec3 right() const`   | The object's local +X (right) direction. |
| `kVec3 up() const`      | The object's local +Y (up) direction. |

### Activation & hierarchy

| Method | Description |
|--------|-------------|
| `bool getActive() const`        | Whether the object is active. |
| `void setActive(bool)`          | Activate/deactivate (fires `OnEnable`/`OnDisable`). |
| `kObject@ getParent() const`    | The parent object, or `null` if it has no parent. |
| `kPhysicsObject@ getPhysicsObject() const` | The physics body attached to this object, or `null`. |

> `getPhysicsObject()` returns the body spawned from the object's physics
> descriptor at play start; use the [`kPhysicsObject`](#8-type-kphysobject)
> handle to apply forces, impulses, and velocity changes.

### Examples

Move forward at constant speed:
```angelscript
void Update()
{
    kObject@ self = getSelf();
    self.translate(self.forward() * (3.0 * getDeltaTime())); // 3 units/sec
}
```

Set an absolute orientation (facing 90° around Y):
```angelscript
void Start()
{
    getSelf().setRotation(kVec3(0, 90, 0)); // degrees
}
```

Read world position and report it:
```angelscript
void Start()
{
    kVec3 p = getSelf().getGlobalPosition();
    print("World pos: " + p.x + ", " + p.y + ", " + p.z);
}
```

Walk up to the parent:
```angelscript
void Start()
{
    kObject@ parent = getSelf().getParent();
    if (parent !is null)
        print("My parent is " + parent.getName());
}
```

---

## 5. Type: `kVec3`

A 3-component float vector (value type — copied by value, no `@` handle).

### Construction

| Form | Description |
|------|-------------|
| `kVec3()`                       | Zero vector `(0, 0, 0)`. |
| `kVec3(float x, float y, float z)` | From components. |
| `kVec3(const kVec3 &in)`        | Copy. |

### Properties

| Property | Description |
|----------|-------------|
| `float x` | X component. |
| `float y` | Y component. |
| `float z` | Z component. |

### Operators

| Operator | Meaning |
|----------|---------|
| `a + b`  | Component-wise add. |
| `a - b`  | Component-wise subtract. |
| `v * f`  | Scale by a float. |
| `-v`     | Negate. |
| `a == b` | Equality. |

### Methods

| Method | Description |
|--------|-------------|
| `float length() const`               | Magnitude. |
| `kVec3 normalized() const`           | Unit-length copy (returns zero vector if length is 0). |
| `float dot(const kVec3 &in) const`   | Dot product. |
| `kVec3 cross(const kVec3 &in) const` | Cross product. |

### Examples

```angelscript
kVec3 a(1, 0, 0);
kVec3 b(0, 1, 0);

kVec3 sum   = a + b;            // (1, 1, 0)
kVec3 dir   = (b - a).normalized();
float d     = a.dot(b);        // 0
kVec3 n     = a.cross(b);      // (0, 0, 1)
float len   = (a * 5).length(); // 5
a.x = 2.0f;                    // direct component access
```

> **No general math library:** the AngelScript math add-on is **not** registered,
> so `sin`, `cos`, `sqrt`, etc. are not available globally. Use the `kVec3`
> helpers (`length`, `dot`, `cross`, `normalized`) for vector math.

---

## 6. Type: `kAudio`

A loaded, manager-owned audio clip. Obtain one with `loadAudio()`; you never
construct or delete it. The manager keeps it alive until `unloadAudio()` (or
audio shutdown), so assigning the handle elsewhere is safe.

| Method | Description |
|--------|-------------|
| `void play()`                 | Start or resume playback. |
| `void stop()`                 | Stop playback and rewind to the start. |
| `void pause()`                | Pause without rewinding. |
| `void resume()`               | Resume a paused clip. |
| `void setLooping(bool)`       | Loop indefinitely when true. |
| `void setVolume(float)`       | Linear volume (`0` silent, `1` full). |
| `void setPitch(float)`        | Pitch / speed multiplier (`1` normal). |
| `void setPosition(const kVec3 &in)` | World-space emitter position for 3-D audio. |
| `void setVelocity(const kVec3 &in)` | Emitter velocity for Doppler shift. |
| `void setSpatialization(bool)`      | Enable/disable 3-D panning and attenuation. |
| `void setMinDistance(float)`        | Distance where attenuation begins. |
| `void setMaxDistance(float)`        | Distance where the sound becomes inaudible. |
| `void setAttenuationModel(int)`     | `0` none, `1` inverse, `2` linear, `3` exponential. |
| `void setRolloff(float)`            | Rolloff factor for inverse/exponential models. |
| `bool isPlaying() const`           | True if currently playing. |
| `bool isPaused() const`            | True if paused. |
| `bool isLooping() const`           | True if looping. |

```angelscript
void Start()
{
    kAudio@ music = loadAudio("Assets/audio/music.ogg");
    if (music !is null)
    {
        music.setLooping(true);
        music.setVolume(0.6f);
        music.play();
    }
}
```

---

## 7. Type: `kAnimator` / `kSkeletalAnimation`

Animators drive skeletal mesh playback and are reached through
`getAnimator(meshObject)` (the object must be a mesh node; `null` otherwise).

### `kAnimator`

| Method | Description |
|--------|-------------|
| `void setSpeed(float)`             | Global playback speed multiplier. |
| `float getSpeed() const`           | Current playback speed multiplier. |
| `void setCurrentTime(float)`       | Seek the active clip to a time (ticks). |
| `float getCurrentTime() const`     | Current playback position (ticks). |
| `int getAnimationCount() const`    | Number of registered clips. |
| `void playAnimation(float index)`  | Play a registered clip by index and reset time. |
| `kSkeletalAnimation@ getCurrentAnimation() const` | The active clip, or `null`. |
| `void setBool(const string &in, bool)`   | Set a named Bool controller variable (0/1). |
| `void setFloat(const string &in, float)` | Set a named Float controller variable. |
| `void setInt(const string &in, int)`     | Set a named Int controller variable. |
| `void setInt(const string &in, float)`   | Float overload used by the visual graph (truncates). |
| `void setTrigger(const string &in)`      | Fire a named Trigger controller variable. |

### `kSkeletalAnimation`

| Method | Description |
|--------|-------------|
| `float getDuration() const`       | Total length in ticks. |
| `float getTicksPerSecond() const` | Tick rate used to convert ticks to seconds. |
| `float getSpeed() const`          | Current clip speed multiplier. |
| `void setSpeed(float)`            | Set the clip speed multiplier. |

```angelscript
void Start()
{
    kAnimator@ anim = getAnimator(getSelf());
    if (anim !is null)
    {
        anim.setSpeed(1.5f);
        anim.playAnimation(0);
    }
}
```

---

## 8. Type: `kPhysicsObject`

A physics body obtained from `object.getPhysicsObject()`. It is manager-owned —
never construct or delete it.

| Method | Description |
|--------|-------------|
| `void setPosition(const kVec3 &in)`        | Teleport the body. |
| `kVec3 getPosition() const`                | World-space body position. |
| `void setLinearVelocity(const kVec3 &in)`  | Set linear velocity (m/s). |
| `void setAngularVelocity(const kVec3 &in)` | Set angular velocity (rad/s). |
| `kVec3 getLinearVelocity() const`          | Current linear velocity. |
| `kVec3 getAngularVelocity() const`         | Current angular velocity. |
| `void applyForce(const kVec3 &in)`         | Apply a continuous force at the centre of mass. |
| `void applyImpulse(const kVec3 &in)`       | Apply an instantaneous impulse. |
| `void applyTorque(const kVec3 &in)`        | Apply a torque for one step. |
| `void setMass(float)`                      | Change the body mass. |
| `void setFriction(float)`                  | Friction coefficient (0–1). |
| `void setRestitution(float)`               | Restitution / bounciness (0–1). |
| `void setLinearDamping(float)`             | Linear drag per second. |
| `void setAngularDamping(float)`            | Angular drag per second. |
| `void setGravityFactor(float)`             | Gravity scale for this body (`0` = none). |
| `bool isActive() const`                    | True if the body is simulated. |
| `int getObjectType() const`                | Motion type (see `kPhysicsObjectType`). |
| `int getShapeType() const`                 | Collision shape type (see `kPhysicsShapeType`). |

```angelscript
void FixedUpdate()
{
    kPhysicsObject@ body = getSelf().getPhysicsObject();
    if (body !is null)
        body.applyImpulse(kVec3(0, 5, 0)); // jump-style nudge
}
```

---

## 9. Standard library (`string`)

The only standard add-on registered is **`string`**. `array<T>`, `dictionary`,
the math add-on, and file/datetime add-ons are **not** available.

### `string`

Common members (full set follows AngelScript's standard string add-on):

| Member | Description |
|--------|-------------|
| `a + b`                       | Concatenate. Numbers (`int`, `float`, `double`, `bool`) concatenate too: `"x=" + 3`. |
| `a += b`                      | Append. |
| `a == b`, `a < b` …           | Compare. |
| `uint length() const`         | Number of bytes. |
| `bool isEmpty() const`        | True if empty. |
| `string substr(uint start = 0, int count = -1) const` | Substring. |
| `int findFirst(const string &in, uint start = 0) const` | Index of first occurrence, or `-1`. |
| `int findLast(const string &in, int start = -1) const`  | Index of last occurrence, or `-1`. |
| `void insert(uint pos, const string &in)` | Insert. |
| `void erase(uint pos, int count = -1)`    | Erase. |
| `s[i]`                        | Byte access via `opIndex`. |

### Number formatting / parsing (global functions)

| Function | Description |
|----------|-------------|
| `string formatInt(int64 val, const string &in options = "", uint width = 0)` | Format an integer (options: e.g. `"l"` left, `"0"` pad, `"x"` hex). |
| `string formatUInt(uint64 val, const string &in options = "", uint width = 0)` | Format an unsigned integer. |
| `string formatFloat(double val, const string &in options = "", uint width = 0, uint precision = 0)` | Format a float. |
| `int64 parseInt(const string &in, uint base = 10, uint &out byteCount = 0)` | Parse an integer. |
| `uint64 parseUInt(const string &in, uint base = 10, uint &out byteCount = 0)` | Parse an unsigned integer. |
| `double parseFloat(const string &in, uint &out byteCount = 0)` | Parse a float. |

```angelscript
void Start()
{
    print("score = " + formatInt(1234, "0", 6));     // "score = 001234"
    print("pi ~ "   + formatFloat(3.14159, "", 0, 2)); // "pi ~ 3.14"
}
```

---

## 10. Complete example scripts

### Spinner
```angelscript
// Continuously spin around the Y axis.
float speed = 2.0f; // radians per second

void Update()
{
    getSelf().rotate(kVec3(0, 1, 0), speed * getDeltaTime());
}
```

### Back-and-forth mover
```angelscript
// Oscillate along X around the start position.
kVec3 origin;
float t = 0.0f;

void Start()
{
    origin = getSelf().getPosition();
}

void Update()
{
    t += getDeltaTime();
    // Triangle-wave style offset using only the available vector math.
    float phase = t * 2.0f;
    float tri = phase - float(int(phase));      // 0..1 ramp
    float offset = (tri < 0.5f ? tri : 1.0f - tri) * 4.0f - 1.0f;
    getSelf().setPosition(origin + kVec3(offset, 0, 0));
}
```

### Physics-driven push
```angelscript
// Nudge the object every fixed step (use FixedUpdate for physics-rate work).
void FixedUpdate()
{
    getSelf().translate(kVec3(0, 0, -1) * (2.0 * getFixedDeltaTime()));
}
```

### Activation toggle on start
```angelscript
void Start()
{
    kObject@ self = getSelf();
    if (self.getName() == "Decoration")
        self.setActive(false); // hide decorations at runtime
}
```

### Logging lifecycle
```angelscript
void Awake()     { log("Awake " + getSelf().getName()); }
void Start()     { log("Start"); }
void OnEnable()  { log("Enabled"); }
void OnDisable() { log("Disabled"); }
void OnDestroy() { log("Destroyed"); }
```

---

## 11. Notes & current limitations

- **Rotation units:** `getRotation`/`setRotation` are in **degrees**;
  `rotate(axis, angle)` is in **radians**.
- **No `array`/`dictionary`/math add-ons.** Only `string` (plus the engine
  types) is registered. Use `kVec3` for vector math; keep collections as your
  own script classes if needed.
- **No object lookup.** A script can reach `getSelf()` and `getParent()`, but
  there is no built-in "find object by name/UUID" yet.
- **Handles vs values:** `kObject` is a reference type used via `@` handles and
  is engine-owned — never `new`/`delete` it; compare against `null` with `is` /
  `!is`. `kVec3` is a value type — assignments copy it.
- **Per-instance globals:** script globals are independent per attached object.

This list is exactly what `registerScriptBindings()` exposes today. As the
engine registers more types/functions, extend this reference to match.
