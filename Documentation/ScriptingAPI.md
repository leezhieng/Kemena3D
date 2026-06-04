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

## 6. Standard library (`string`)

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

## 7. Complete example scripts

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

## 8. Notes & current limitations

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
