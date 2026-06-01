#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include "kdatatype.h"
#include "nlohmann/json.hpp"

namespace kemena
{

// ---------------------------------------------------------------------------
// Pin / connection types
// ---------------------------------------------------------------------------

/**
 * @brief Data type carried by a shader-node pin (link/connection type).
 *
 * Determines the GLSL type of the value flowing through a pin and which
 * connections are allowed (see kPinCompatible).
 */
enum class kPinType
{
    Float,        ///< Scalar float.
    Vec2,         ///< 2-component vector.
    Vec3,         ///< 3-component vector.
    Vec4,         ///< 4-component vector.
    Sampler2D,    ///< 2D texture sampler.
    SamplerCube,  ///< Cube-map texture sampler.
};

/**
 * @brief Returns the GLSL type keyword for a pin type.
 * @param t Pin type to name.
 * @return GLSL type string (e.g. "float", "vec3", "sampler2D"); defaults to "float".
 */
inline const char* kPinTypeName(kPinType t)
{
    switch (t)
    {
        case kPinType::Float:       return "float";
        case kPinType::Vec2:        return "vec2";
        case kPinType::Vec3:        return "vec3";
        case kPinType::Vec4:        return "vec4";
        case kPinType::Sampler2D:   return "sampler2D";
        case kPinType::SamplerCube: return "samplerCube";
    }
    return "float";
}

/**
 * @brief Tests whether an output pin type may feed into an input pin type.
 *
 * Allows identical types plus a set of safe implicit promotions:
 * float to any vector, vec3 to vec4, and vec4 to vec3 (taking .rgb).
 *
 * @param from Source (output) pin type.
 * @param to   Destination (input) pin type.
 * @return true if a link from @p from to @p to is permitted.
 */
inline bool kPinCompatible(kPinType from, kPinType to)
{
    if (from == to) return true;
    // float → any numeric
    if (from == kPinType::Float &&
        (to == kPinType::Vec2 || to == kPinType::Vec3 || to == kPinType::Vec4))
        return true;
    // vec3 → vec4
    if (from == kPinType::Vec3 && to == kPinType::Vec4) return true;
    // vec4 → vec3 (take .rgb)
    if (from == kPinType::Vec4 && to == kPinType::Vec3) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Shader node types  (kShaderNodeType to avoid clash with kNodeType in kdatatype.h)
// ---------------------------------------------------------------------------

/**
 * @brief Identifies the kind of operation a shader graph node performs.
 *
 * Named kShaderNodeType (rather than kNodeType) to avoid clashing with the
 * scene-graph node enum in kdatatype.h. Values are grouped into constant/
 * built-in inputs, texture sampling, arithmetic, vector construction,
 * material property inputs, and shader outputs.
 */
enum class kShaderNodeType
{
    // --- Constant / built-in inputs ---
    ConstFloat,     ///< User-supplied constant float.
    ConstVec2,      ///< User-supplied constant vec2.
    ConstVec3,      ///< User-supplied constant vec3.
    ConstVec4,      ///< User-supplied constant vec4.
    UVCoord,        ///< Interpolated texture coordinates.
    Time,           ///< Elapsed time uniform.
    VertexColor,    ///< Per-vertex color attribute.
    WorldPosition,  ///< Fragment world-space position.
    ViewDirection,  ///< Direction from fragment toward the camera.
    VertexNormal,   ///< Interpolated surface normal.

    // --- Texture sampling ---
    Texture2D,      ///< Sample a 2D texture.
    TextureCube,    ///< Sample a cube-map texture.

    // --- Arithmetic ---
    Add,            ///< a + b.
    Subtract,       ///< a - b.
    Multiply,       ///< a * b.
    Divide,         ///< a / b.
    Dot,            ///< Dot product.
    Cross,          ///< Cross product.
    Normalize,      ///< Normalize a vector.
    Length,         ///< Vector length.
    Clamp,          ///< Clamp value between min and max.
    Mix,            ///< Linear interpolation (lerp).
    Pow,            ///< Raise base to an exponent.
    Abs,            ///< Absolute value.
    Floor,          ///< Round down to integer.
    Ceil,           ///< Round up to integer.
    Fract,          ///< Fractional part.
    Sqrt,           ///< Square root.
    Min,            ///< Component-wise minimum.
    Max,            ///< Component-wise maximum.
    Step,           ///< Step function (0 or 1 about an edge).
    Smoothstep,     ///< Smooth Hermite interpolation between edges.
    OneMinus,       ///< 1 - value.

    // --- Vector construction / decomposition ---
    Split,          ///< Decompose a vector into its components.
    Combine,        ///< Assemble scalars into a vector.
    Swizzle,        ///< Reorder/select components via a mask.

    // --- Material property inputs ---
    MaterialTiling,     ///< Material UV tiling factor.
    MaterialAmbient,    ///< Material ambient color.
    MaterialDiffuse,    ///< Material diffuse color.
    MaterialSpecular,   ///< Material specular color.
    MaterialShininess,  ///< Material shininess exponent.
    MaterialMetallic,   ///< Material metallic factor (PBR).
    MaterialRoughness,  ///< Material roughness factor (PBR).

    // --- Output (base shader type) ---
    OutputFlat,     ///< Unlit/flat shading output.
    OutputPhong,    ///< Phong lit output (requires lights).
    OutputPBR,      ///< Physically based output (requires lights).
};

// ---------------------------------------------------------------------------
// Pin descriptor
// ---------------------------------------------------------------------------

/**
 * @brief A single input or output connection point on a shader node.
 *
 * Holds the pin's type, a default value used when no link feeds it, and a
 * cached screen-space position populated during node-editor drawing.
 */
struct kShaderPin
{
    int      id       = 0;                 ///< Unique pin id within the graph.
    kString  name;                         ///< Display name of the pin.
    kPinType type     = kPinType::Float;   ///< Data type carried by the pin.
    bool     isOutput = false;             ///< True if this is an output pin.

    // Default value when no link feeds this pin
    float    defFloat    = 0.0f;                  ///< Default scalar value when unconnected.
    float    defVec[4]   = { 0.f, 0.f, 0.f, 1.f };///< Default vector value when unconnected.

    // Runtime UI position (screen space, set during draw)
    float    uiX = 0, uiY = 0;             ///< Cached screen-space position (set during draw).
};

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------

/**
 * @brief A node in the shader graph: a typed operation with input/output pins.
 *
 * Carries its editor position and a small type-specific payload (constant
 * value, texture uniform name or swizzle mask, and a boolean flag).
 */
struct kShaderNode
{
    int                     id   = 0;                            ///< Unique node id within the graph.
    kShaderNodeType         type = kShaderNodeType::ConstFloat;  ///< Operation this node performs.
    kString                 name;                                ///< Display name of the node.
    float                   posX = 200.f, posY = 200.f;          ///< Node position in the editor canvas.

    std::vector<kShaderPin> inputs;   ///< Input pins.
    std::vector<kShaderPin> outputs;  ///< Output pins.

    // Node-specific payload
    float   valueFloat[4] = { 0.f, 0.f, 0.f, 1.f }; ///< Constant value for Const* nodes.
    kString valueStr;                               ///< Texture uniform name or swizzle mask.
    bool    valueBool = false;                      ///< Generic boolean payload.

    /**
     * @brief Returns a human-readable name for a shader node type.
     * @param t Node type to name.
     * @return Static string label for the node type.
     */
    static const char* typeName(kShaderNodeType t);
};

// ---------------------------------------------------------------------------
// Link (directed edge: output pin → input pin)
// ---------------------------------------------------------------------------

/**
 * @brief A directed edge connecting an output pin to an input pin.
 */
struct kShaderLink
{
    int id       = 0;            ///< Unique link id within the graph.
    int fromNode = 0, fromPin = 0; ///< Source node id and output pin id.
    int toNode   = 0, toPin   = 0; ///< Destination node id and input pin id.
};

// ---------------------------------------------------------------------------
// Graph
// ---------------------------------------------------------------------------

/**
 * @brief A complete node-based shader graph: nodes, links, and identity.
 *
 * Owns its nodes and links, hands out unique ids, tracks a dirty flag for
 * the editor, and provides queries, edits, a node factory, and JSON I/O.
 */
struct kShaderGraph
{
    kString                    uuid;          ///< Stable unique identifier for the graph asset.
    kString                    name;          ///< Display name of the graph.
    std::vector<kShaderNode>   nodes;         ///< All nodes in the graph.
    std::vector<kShaderLink>   links;         ///< All links between node pins.
    int                        nextId = 1;    ///< Next id to hand out via newId().
    bool                       dirty  = false;///< True when the graph has unsaved changes.

    /**
     * @brief Allocates and returns a fresh unique id.
     * @return The next sequential id.
     */
    int newId() { return nextId++; }

    /**
     * @brief Finds a node by id.
     * @param id Node id to look up.
     * @return Pointer to the node, or nullptr if not found.
     */
    kShaderNode*       findNode(int id);

    /**
     * @brief Finds a node by id (const overload).
     * @param id Node id to look up.
     * @return Const pointer to the node, or nullptr if not found.
     */
    const kShaderNode* findNode(int id) const;

    /**
     * @brief Finds the link feeding a given input pin.
     * @param nodeId Destination node id.
     * @param pinId  Destination input pin id.
     * @return The link whose toNode/toPin match, or nullptr if the pin is unconnected.
     */
    const kShaderLink* incomingLink(int nodeId, int pinId) const;

    /**
     * @brief Tests whether a pin has any link attached.
     * @param nodeId Owning node id.
     * @param pinId  Pin id to test.
     * @return true if at least one link touches the pin.
     */
    bool isPinConnected(int nodeId, int pinId) const;

    /**
     * @brief Removes every link touching the given node.
     * @param nodeId Node whose links should be removed.
     */
    void removeLinksByNode(int nodeId);

    /**
     * @brief Removes every link touching the given pin.
     * @param nodeId Owning node id.
     * @param pinId  Pin id whose links should be removed.
     */
    void removeLinksByPin(int nodeId, int pinId);

    /**
     * @brief Creates a node of a given type with its default pins wired up.
     * @param type Node type to construct.
     * @param x    Initial canvas X position.
     * @param y    Initial canvas Y position.
     * @return A fully initialized node (not yet added to the graph).
     */
    kShaderNode makeNode(kShaderNodeType type, float x, float y);

    /**
     * @brief Serializes the graph to JSON.
     * @return JSON representation of the graph.
     */
    nlohmann::json toJson() const;

    /**
     * @brief Rebuilds the graph from a JSON representation.
     * @param j JSON object previously produced by toJson().
     */
    void           fromJson(const nlohmann::json& j);
};

// ---------------------------------------------------------------------------
// Compiler: walks the graph and emits combined GLSL source
// ---------------------------------------------------------------------------

/**
 * @brief Result of compiling a shader graph into GLSL.
 *
 * Reports success/error, the generated source, and a set of auto-detected
 * material inputs used to drive the material inspector and uniform binding.
 */
struct kShaderCompileResult
{
    bool        success = false;  ///< True if compilation succeeded.
    kString     error;            ///< Error message when @ref success is false.
    kString     glsl;             ///< Combined vertex + fragment GLSL source.

    // Auto-detected material inputs (for material inspector display)
    bool        usesAlbedoMap            = false; ///< Graph samples an albedo/base-color map.
    bool        usesNormalMap            = false; ///< Graph samples a normal map.
    bool        usesMetallicRoughnessMap = false; ///< Graph samples a metallic-roughness map.
    bool        usesAoMap                = false; ///< Graph samples an ambient-occlusion map.
    bool        usesEmissiveMap          = false; ///< Graph samples an emissive map.
    std::vector<std::pair<kString, kString>> customSamplers; ///< Extra samplers: { uniformName, "2D"|"Cube" }.
    bool        needsMaterial            = false; ///< Graph references material.* uniforms.
    bool        needsLights              = false; ///< Output requires lighting (Phong/PBR).
};

/**
 * @brief Compiles a kShaderGraph into combined GLSL source.
 *
 * Stateless front end: walks the graph from its output node, emitting GLSL
 * expressions and statements, and reports the result via kShaderCompileResult.
 */
class kShaderCompiler
{
public:
    /**
     * @brief Compiles a shader graph into GLSL.
     * @param graph Graph to compile.
     * @return Compilation result with generated source and detected inputs.
     */
    static kShaderCompileResult compile(const kShaderGraph& graph);

private:
    /**
     * @brief Mutable code-generation state shared across one compile pass.
     *
     * Accumulates emitted variable names, sampler/uniform declarations, the
     * main() body statements, and a counter for generating unique variables.
     */
    struct Ctx
    {
        std::map<int, kString>  nodeVar;    ///< Node id to emitted output variable name.
        std::vector<kString>    samplers;   ///< Sampler uniform declarations.
        std::vector<kString>    uniforms;   ///< Other uniform declarations.
        std::set<kString>       emittedSamplerNames; ///< Sampler names already declared (dedup).
        std::vector<kString>    body;       ///< Statement lines emitted into main().
        int                     counter = 0;///< Counter feeding newVar().

        /**
         * @brief Generates a fresh unique GLSL variable name.
         * @return A name of the form "v_<n>".
         */
        kString newVar() { return "v_" + std::to_string(counter++); }
    };

    /**
     * @brief Emits the GLSL expression feeding a specific input pin.
     * @param g      Graph being compiled.
     * @param nodeId Node owning the input pin.
     * @param pinId  Input pin id.
     * @param ctx    Code-generation context (mutated).
     * @return GLSL expression string (a variable name or a literal).
     */
    static kString emitPin(const kShaderGraph& g, int nodeId, int pinId, Ctx& ctx);

    /**
     * @brief Emits a node and its dependencies.
     * @param g      Graph being compiled.
     * @param nodeId Node to emit.
     * @param ctx    Code-generation context (mutated).
     * @return The output variable name of the node's first output pin.
     */
    static kString emitNode(const kShaderGraph& g, int nodeId, Ctx& ctx);

    /**
     * @brief Wraps an expression to convert between compatible pin types.
     * @param expr   Source GLSL expression.
     * @param actual Actual type of @p expr.
     * @param target Desired target type.
     * @return GLSL expression promoted to @p target, or @p expr unchanged.
     */
    static kString promote(const kString& expr, kPinType actual, kPinType target);

    /**
     * @brief Returns the GLSL literal for a pin's default value.
     * @param pin Pin whose default to format.
     * @return GLSL literal expression for the pin default.
     */
    static kString pinDefault(const kShaderPin& pin);

    /**
     * @brief Returns the GLSL type keyword for a pin type.
     * @param t Pin type.
     * @return GLSL type string.
     */
    static kString glslType(kPinType t) { return kPinTypeName(t); }

    /**
     * @brief Produces the fixed vertex-shader template source.
     * @return Vertex shader GLSL.
     */
    static kString vertexTemplate();

    /**
     * @brief Produces the fragment-shader preamble (uniforms, varyings, helpers).
     * @param g       Graph being compiled.
     * @param outNode The graph's output node.
     * @param ctx     Code-generation context with collected declarations.
     * @return Fragment shader preamble GLSL.
     */
    static kString fragmentPreamble(const kShaderGraph& g, const kShaderNode& outNode,
                                    const Ctx& ctx);

    /**
     * @brief Produces the lighting code for a given output node type.
     * @param outType Output node type (Flat, Phong, or PBR).
     * @return GLSL lighting code for that shading model.
     */
    static kString lightingCode(kShaderNodeType outType);
};

} // namespace kemena
