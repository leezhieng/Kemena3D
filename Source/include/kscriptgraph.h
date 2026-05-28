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
        EventAwake = 0,
        EventStart,
        EventUpdate,
        EventFixedUpdate,
        EventLateUpdate,
        EventOnDestroy,

        // --- Flow ------------------------------------------------------------
        Branch, ///< if/else on a Bool condition.

        // --- Actions (exec in + exec out) -----------------------------------
        Print,
        SetPosition,
        SetRotation,
        SetScale,
        Translate,
        Rotate,
        SetActive,
        SetVariable,

        // --- Getters (pure data) --------------------------------------------
        GetSelf,
        GetPosition,
        GetRotation,
        GetScale,
        GetForward,
        GetRight,
        GetUp,
        GetDeltaTime,
        GetVariable,

        // --- Literals --------------------------------------------------------
        LiteralFloat,
        LiteralBool,
        LiteralString,
        LiteralVec3,

        // --- Math ------------------------------------------------------------
        Add,
        Subtract,
        Multiply,
        Divide,

        // --- Vector ----------------------------------------------------------
        MakeVec3,
        BreakVec3,
        ScaleVec3, ///< Vec3 * Float.

        // --- Compare / logic -------------------------------------------------
        Greater,
        Less,
        Equal,
        And,
        Or,
        Not,

        Count ///< Sentinel.
    };

    /** @brief Human-readable label for a node type (used by the editor menu). */
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
        float   defFloat  = 0.0f;
        float   defVec[3] = { 0.0f, 0.0f, 0.0f };
        bool    defBool   = false;
        kString defStr;

        // Cached screen position, refreshed by the editor each frame.
        float uiX = 0.0f, uiY = 0.0f;
    };

    /**
     * @brief A single graph node with its input/output pins and payload.
     */
    struct KEMENA3D_API kScriptGraphNode
    {
        int             id   = 0;
        kScriptNodeType type = kScriptNodeType::EventUpdate;
        kString         name;
        float           posX = 0.0f, posY = 0.0f;

        std::vector<kScriptGraphPin> inputs;
        std::vector<kScriptGraphPin> outputs;

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
        int id       = 0;
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

        std::vector<kScriptGraphNode> nodes;
        std::vector<kScriptGraphLink> links;
        std::vector<kScriptGraphVar>  variables;

        int  nextId = 1;       ///< Monotonic id source.
        bool dirty  = false;   ///< Unsaved-changes flag (editor only).

        /** @brief Returns the next graph-unique id. */
        int newId() { return nextId++; }

        /** @brief Finds a node by id, or nullptr. */
        kScriptGraphNode *findNode(int id);
        const kScriptGraphNode *findNode(int id) const;

        /** @brief Returns the link feeding an input pin, or nullptr. */
        const kScriptGraphLink *incomingLink(int nodeId, int pinId) const;

        /** @brief Returns the first link leaving an output pin, or nullptr. */
        const kScriptGraphLink *outgoingLink(int nodeId, int pinId) const;

        /** @brief True if any link touches the given pin. */
        bool isPinConnected(int nodeId, int pinId) const;

        /** @brief Removes every link attached to a node. */
        void removeLinksByNode(int nodeId);

        /** @brief Removes every link attached to a specific pin. */
        void removeLinksByPin(int nodeId, int pinId);

        /** @brief Removes a node and all of its links. */
        void removeNode(int nodeId);

        /**
         * @brief Builds a fully-pinned node of the given type at (x, y).
         *
         * Pin ids are drawn from newId(); the node id is assigned too.
         */
        kScriptGraphNode makeNode(kScriptNodeType type, float x, float y);

        /** @brief Serialises the whole graph to JSON. */
        kJson toJson() const;

        /** @brief Restores a graph from JSON produced by toJson(). */
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
        KEMENA3D_API kScriptGraphResult compile(const kScriptGraph &graph);
    }
}

#endif // KSCRIPTGRAPH_H
