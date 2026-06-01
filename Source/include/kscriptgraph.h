/**
 * @file kscriptgraph.h
 * @brief Visual-scripting node graph: data model, JSON serialisation, and
 *        AngelScript code generation.
 *
 * A kScriptGraph is the editable form of a node-graph script. The editor saves
 * it as a @c .logic JSON file; kScriptGraphCompiler::compile() turns it into
 * AngelScript @c .as text that flows through the normal script pipeline
 * (kScriptManager compiles that text to bytecode).
 *
 * The graph mixes two wire kinds, Unreal-Blueprint style:
 *  - @b Execution wires (kScriptPinType::Exec) sequence the statements.
 *  - @b Data wires (Float/Bool/String/Vec3/Object) feed values into pins.
 */

#ifndef KSCRIPTGRAPH_H
#define KSCRIPTGRAPH_H

#include "kexport.h"

#include <vector>
#include <string>

#include <nlohmann/json.hpp>

#include "kdatatype.h"

namespace kemena
{
    using kJson = nlohmann::json;

    /**
     * @brief Kind/type of a node pin. Exec pins carry control flow; the rest
     *        carry typed data values.
     */
    enum class kScriptPinType
    {
        Exec = 0, ///< Execution (control-flow) wire.
        Float,    ///< Floating-point value.
        Bool,     ///< Boolean value.
        String,   ///< Text value.
        Vec3,     ///< 3-component vector value.
        Object,   ///< kObject handle.
    };

    /**
     * @brief Every node kind the visual scripting graph supports.
     */
    enum class kScriptNodeType
    {
        // --- Events (entry points; one exec output) -------------------------
        EventAwake = 0,   ///< Emits the @c Awake lifecycle function.
        EventStart,       ///< Emits the @c Start lifecycle function.
        EventUpdate,      ///< Emits the @c Update lifecycle function.
        EventFixedUpdate, ///< Emits the @c FixedUpdate lifecycle function.
        EventLateUpdate,  ///< Emits the @c LateUpdate lifecycle function.
        EventOnDestroy,   ///< Emits the @c OnDestroy lifecycle function.

        // --- Flow ------------------------------------------------------------
        Branch, ///< if/else on a Bool condition.

        // --- Actions (exec in + exec out) -----------------------------------
        Print,       ///< Logs a value to the console.
        SetPosition, ///< Sets the object's world position.
        SetRotation, ///< Sets the object's rotation.
        SetScale,    ///< Sets the object's scale.
        Translate,   ///< Offsets the object's position by a vector.
        Rotate,      ///< Rotates the object by a vector.
        SetActive,   ///< Enables/disables the object.
        SetVariable, ///< Assigns a graph variable.

        // --- Getters (pure data) --------------------------------------------
        GetSelf,      ///< The object the script is attached to.
        GetPosition,  ///< Reads the object's world position.
        GetRotation,  ///< Reads the object's rotation.
        GetScale,     ///< Reads the object's scale.
        GetForward,   ///< The object's forward axis.
        GetRight,     ///< The object's right axis.
        GetUp,        ///< The object's up axis.
        GetDeltaTime, ///< Frame delta time.
        GetVariable,  ///< Reads a graph variable.

        // --- Literals --------------------------------------------------------
        LiteralFloat,  ///< Constant float value.
        LiteralBool,   ///< Constant bool value.
        LiteralString, ///< Constant string value.
        LiteralVec3,   ///< Constant Vec3 value.

        // --- Math ------------------------------------------------------------
        Add,      ///< a + b.
        Subtract, ///< a - b.
        Multiply, ///< a * b.
        Divide,   ///< a / b.

        // --- Vector ----------------------------------------------------------
        MakeVec3,  ///< Combines three floats into a Vec3.
        BreakVec3, ///< Splits a Vec3 into three floats.
        ScaleVec3, ///< Vec3 * Float.

        // --- Compare / logic -------------------------------------------------
        Greater, ///< a > b.
        Less,    ///< a < b.
        Equal,   ///< a == b.
        And,     ///< Logical AND.
        Or,      ///< Logical OR.
        Not,     ///< Logical negation.

        Count ///< Sentinel (number of node types).
    };

    /**
     * @brief Human-readable label for a node type (used by the editor menu).
     * @param type Node type to name.
     * @return A static, null-terminated display string (e.g. "On Update").
     */
    KEMENA3D_API const char *kScriptNodeTypeName(kScriptNodeType type);

    /**
     * @brief A connection point on a node.
     */
    struct KEMENA3D_API kScriptGraphPin
    {
        int            id   = 0;                       ///< Graph-unique pin id.
        kString        name;                           ///< Display label.
        kScriptPinType type = kScriptPinType::Float;    ///< Wire kind.
        bool           isOutput = false;                ///< Output vs input.

        // Inline default used when an input data pin is left unconnected.
        float   defFloat  = 0.0f;                       ///< Default for unconnected Float inputs.
        float   defVec[3] = { 0.0f, 0.0f, 0.0f };       ///< Default for unconnected Vec3 inputs.
        bool    defBool   = false;                      ///< Default for unconnected Bool inputs.
        kString defStr;                                 ///< Default for unconnected String inputs.

        // Cached screen position, refreshed by the editor each frame.
        float uiX = 0.0f, uiY = 0.0f;                   ///< Cached pin screen position (editor only).
    };

    /**
     * @brief A single graph node with its input/output pins and payload.
     */
    struct KEMENA3D_API kScriptGraphNode
    {
        int             id   = 0;                           ///< Graph-unique node id.
        kScriptNodeType type = kScriptNodeType::EventUpdate; ///< Node kind.
        kString         name;                                ///< Display label.
        float           posX = 0.0f, posY = 0.0f;            ///< Canvas position (editor only).

        std::vector<kScriptGraphPin> inputs;  ///< Input pins (exec + data).
        std::vector<kScriptGraphPin> outputs; ///< Output pins (exec + data).

        // Payload for literal and variable nodes.
        float   valueFloat[3] = { 0.0f, 0.0f, 0.0f }; ///< Literal numeric value.
        bool    valueBool     = false;                ///< Literal boolean value.
        kString valueStr;                             ///< Literal string OR variable name.
    };

    /**
     * @brief A directed wire from an output pin to an input pin.
     */
    struct KEMENA3D_API kScriptGraphLink
    {
        int id       = 0;              ///< Graph-unique link id.
        int fromNode = 0, fromPin = 0; ///< Source node + output pin.
        int toNode   = 0, toPin   = 0; ///< Destination node + input pin.
    };

    /**
     * @brief A graph-scope variable, emitted as an AngelScript global.
     *
     * v1 supports float variables only; this keeps generated code type-sound
     * without per-node pin retyping.
     */
    struct KEMENA3D_API kScriptGraphVar
    {
        kString name;            ///< Identifier (must be a valid AngelScript name).
        float   defValue = 0.0f; ///< Initial value.
    };

    /**
     * @brief The complete editable node graph for one script.
     */
    struct KEMENA3D_API kScriptGraph
    {
        kString uuid;  ///< Stable script-asset UUID.
        kString name;  ///< Display name.

        std::vector<kScriptGraphNode> nodes;     ///< All nodes in the graph.
        std::vector<kScriptGraphLink> links;     ///< All wires between pins.
        std::vector<kScriptGraphVar>  variables; ///< Graph-scope variables.

        int  nextId = 1;       ///< Monotonic id source.
        bool dirty  = false;   ///< Unsaved-changes flag (editor only).

        /**
         * @brief Returns the next graph-unique id and advances the counter.
         * @return A fresh id never previously handed out by this graph.
         */
        int newId() { return nextId++; }

        /**
         * @brief Finds a node by id, or nullptr.
         * @param id Graph-unique node id to look up.
         * @return Pointer to the matching node, or nullptr if none.
         */
        kScriptGraphNode *findNode(int id);

        /**
         * @brief Const overload: finds a node by id, or nullptr.
         * @param id Graph-unique node id to look up.
         * @return Pointer to the matching node, or nullptr if none.
         */
        const kScriptGraphNode *findNode(int id) const;

        /**
         * @brief Returns the link feeding an input pin, or nullptr.
         * @param nodeId Destination node id.
         * @param pinId  Destination input pin id.
         * @return The incoming link, or nullptr if the pin is unconnected.
         */
        const kScriptGraphLink *incomingLink(int nodeId, int pinId) const;

        /**
         * @brief Returns the first link leaving an output pin, or nullptr.
         * @param nodeId Source node id.
         * @param pinId  Source output pin id.
         * @return The first outgoing link, or nullptr if none.
         */
        const kScriptGraphLink *outgoingLink(int nodeId, int pinId) const;

        /**
         * @brief True if any link touches the given pin.
         * @param nodeId Owning node id.
         * @param pinId  Pin id to test.
         * @return True when at least one link references the pin.
         */
        bool isPinConnected(int nodeId, int pinId) const;

        /**
         * @brief Removes every link attached to a node.
         * @param nodeId Node whose links are removed.
         */
        void removeLinksByNode(int nodeId);

        /**
         * @brief Removes every link attached to a specific pin.
         * @param nodeId Owning node id.
         * @param pinId  Pin whose links are removed.
         */
        void removeLinksByPin(int nodeId, int pinId);

        /**
         * @brief Removes a node and all of its links.
         * @param nodeId Node to delete.
         */
        void removeNode(int nodeId);

        /**
         * @brief Builds a fully-pinned node of the given type at (x, y).
         *
         * Pin ids are drawn from newId(); the node id is assigned too.
         *
         * @param type Node kind to construct.
         * @param x    Initial canvas X position.
         * @param y    Initial canvas Y position.
         * @return The newly built node (not yet added to @ref nodes).
         */
        kScriptGraphNode makeNode(kScriptNodeType type, float x, float y);

        /**
         * @brief Serialises the whole graph to JSON.
         * @return A JSON object holding nodes, links, variables and metadata.
         */
        kJson toJson() const;

        /**
         * @brief Restores a graph from JSON produced by toJson().
         * @param j JSON object previously emitted by toJson().
         */
        void fromJson(const kJson &j);
    };

    /**
     * @brief Result of compiling a graph to AngelScript text.
     */
    struct KEMENA3D_API kScriptGraphResult
    {
        bool    success = false; ///< True if code was generated.
        kString error;           ///< Human-readable failure reason.
        kString code;            ///< Generated AngelScript source.
    };

    /**
     * @brief Compiles a node graph into AngelScript source text.
     *
     * Each event node becomes a lifecycle function; graph variables become
     * file-scope globals; exec wires sequence statements and data wires are
     * inlined as expressions.
     */
    namespace kScriptGraphCompiler
    {
        /**
         * @brief Compiles a node graph into AngelScript source text.
         * @param graph The node graph to translate.
         * @return Result carrying the generated code, or an error on failure.
         */
        KEMENA3D_API kScriptGraphResult compile(const kScriptGraph &graph);
    }
}

#endif // KSCRIPTGRAPH_H
