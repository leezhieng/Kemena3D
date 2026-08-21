#include "kscriptgraph.h"

#include <set>
#include <cstdio>

namespace kemena
{
    // -----------------------------------------------------------------------
    // Node type labels
    // -----------------------------------------------------------------------

    const char *kScriptNodeTypeName(kScriptNodeType type)
    {
        switch (type)
        {
            case kScriptNodeType::EventAwake:       return "On Awake";
            case kScriptNodeType::EventStart:       return "On Start";
            case kScriptNodeType::EventUpdate:      return "On Update";
            case kScriptNodeType::EventFixedUpdate: return "On Fixed Update";
            case kScriptNodeType::EventLateUpdate:  return "On Late Update";
            case kScriptNodeType::EventOnDestroy:   return "On Destroy";
            case kScriptNodeType::Branch:           return "Branch";
            case kScriptNodeType::Print:            return "Print";
            case kScriptNodeType::PrintConsole:     return "Print Console";
            case kScriptNodeType::SetPosition:      return "Set Position";
            case kScriptNodeType::SetRotation:      return "Set Rotation";
            case kScriptNodeType::SetScale:         return "Set Scale";
            case kScriptNodeType::Translate:        return "Translate";
            case kScriptNodeType::Rotate:           return "Rotate";
            case kScriptNodeType::SetActive:        return "Set Active";
            case kScriptNodeType::SetVariable:      return "Set Variable";
            case kScriptNodeType::GetSelf:          return "Get Self";
            case kScriptNodeType::GetPosition:      return "Get Position";
            case kScriptNodeType::GetRotation:      return "Get Rotation";
            case kScriptNodeType::GetScale:         return "Get Scale";
            case kScriptNodeType::GetForward:       return "Get Forward";
            case kScriptNodeType::GetRight:         return "Get Right";
            case kScriptNodeType::GetUp:            return "Get Up";
            case kScriptNodeType::GetDeltaTime:     return "Get Delta Time";
            case kScriptNodeType::GetVariable:      return "Get Variable";
            case kScriptNodeType::LiteralFloat:     return "Float";
            case kScriptNodeType::LiteralBool:      return "Bool";
            case kScriptNodeType::LiteralString:    return "String";
            case kScriptNodeType::LiteralVec3:      return "Vector3";
            case kScriptNodeType::Add:              return "Add";
            case kScriptNodeType::Subtract:         return "Subtract";
            case kScriptNodeType::Multiply:         return "Multiply";
            case kScriptNodeType::Divide:           return "Divide";
            case kScriptNodeType::MakeVec3:         return "Make Vector3";
            case kScriptNodeType::BreakVec3:        return "Break Vector3";
            case kScriptNodeType::ScaleVec3:        return "Scale Vector3";
            case kScriptNodeType::Greater:          return "Greater";
            case kScriptNodeType::Less:             return "Less";
            case kScriptNodeType::Equal:            return "Equal";
            case kScriptNodeType::And:              return "And";
            case kScriptNodeType::Or:               return "Or";
            case kScriptNodeType::Not:              return "Not";
            case kScriptNodeType::GetAction:         return "Get Action";
            case kScriptNodeType::GetActionPressed:  return "Get Action Pressed";
            case kScriptNodeType::GetActionReleased: return "Get Action Released";
            case kScriptNodeType::GetAxis:           return "Get Axis";
            case kScriptNodeType::PlaySound:            return "Play Sound";
            case kScriptNodeType::StopAllSounds:        return "Stop All Sounds";
            case kScriptNodeType::SetMasterVolume:      return "Set Master Volume";
            case kScriptNodeType::GetMasterVolume:      return "Get Master Volume";
            case kScriptNodeType::SetListenerPosition:  return "Set Listener Position";
            case kScriptNodeType::SetListenerDirection: return "Set Listener Direction";
            case kScriptNodeType::GetAnimator:          return "Get Animator";
            case kScriptNodeType::PlayAnimation:        return "Play Animation";
            case kScriptNodeType::SetAnimatorSpeed:     return "Set Animator Speed";
            case kScriptNodeType::SetAnimatorTime:      return "Set Animator Time";
            case kScriptNodeType::GetAnimatorSpeed:     return "Get Animator Speed";
            case kScriptNodeType::SetAnimatorBool:      return "Set Boolean";
            case kScriptNodeType::SetAnimatorFloat:     return "Set Float";
            case kScriptNodeType::SetAnimatorInt:       return "Set Integer";
            case kScriptNodeType::SetAnimatorTrigger:   return "Set Trigger";
            case kScriptNodeType::GetPhysicsObject:     return "Get Physics Object";
            case kScriptNodeType::ApplyForce:           return "Apply Force";
            case kScriptNodeType::ApplyImpulse:         return "Apply Impulse";
            case kScriptNodeType::ApplyTorque:          return "Apply Torque";
            case kScriptNodeType::SetLinearVelocity:    return "Set Linear Velocity";
            case kScriptNodeType::SetAngularVelocity:   return "Set Angular Velocity";
            case kScriptNodeType::GetPhysicsVelocity:   return "Get Physics Velocity";
            case kScriptNodeType::GetPhysicsPosition:   return "Get Physics Position";
            case kScriptNodeType::SetPhysicsGravity:    return "Set Physics Gravity";
            case kScriptNodeType::GetPhysicsGravity:    return "Get Physics Gravity";
            case kScriptNodeType::IsPhysicsActive:      return "Is Physics Active";
            case kScriptNodeType::Anchor:               return "Anchor";
            case kScriptNodeType::Comment:              return "Comment";
            default:                                return "Node";
        }
    }

    // -----------------------------------------------------------------------
    // kScriptGraph — queries
    // -----------------------------------------------------------------------

    kScriptGraphNode *kScriptGraph::findNode(int id)
    {
        for (auto &n : nodes)
            if (n.id == id)
                return &n;
        return nullptr;
    }

    const kScriptGraphNode *kScriptGraph::findNode(int id) const
    {
        for (auto &n : nodes)
            if (n.id == id)
                return &n;
        return nullptr;
    }

    const kScriptGraphLink *kScriptGraph::incomingLink(int nodeId, int pinId) const
    {
        for (auto &l : links)
            if (l.toNode == nodeId && l.toPin == pinId)
                return &l;
        return nullptr;
    }

    const kScriptGraphLink *kScriptGraph::outgoingLink(int nodeId, int pinId) const
    {
        for (auto &l : links)
            if (l.fromNode == nodeId && l.fromPin == pinId)
                return &l;
        return nullptr;
    }

    bool kScriptGraph::isPinConnected(int nodeId, int pinId) const
    {
        for (auto &l : links)
            if ((l.fromNode == nodeId && l.fromPin == pinId) ||
                (l.toNode == nodeId && l.toPin == pinId))
                return true;
        return false;
    }

    void kScriptGraph::removeLinksByNode(int nodeId)
    {
        for (size_t i = links.size(); i-- > 0;)
            if (links[i].fromNode == nodeId || links[i].toNode == nodeId)
                links.erase(links.begin() + i);
    }

    void kScriptGraph::removeLinksByPin(int nodeId, int pinId)
    {
        for (size_t i = links.size(); i-- > 0;)
            if ((links[i].fromNode == nodeId && links[i].fromPin == pinId) ||
                (links[i].toNode == nodeId && links[i].toPin == pinId))
                links.erase(links.begin() + i);
    }

    void kScriptGraph::removeNode(int nodeId)
    {
        removeLinksByNode(nodeId);
        for (size_t i = nodes.size(); i-- > 0;)
            if (nodes[i].id == nodeId)
                nodes.erase(nodes.begin() + i);
    }

    // -----------------------------------------------------------------------
    // kScriptGraph — node factory
    // -----------------------------------------------------------------------

    kScriptGraphNode kScriptGraph::makeNode(kScriptNodeType type, float x, float y)
    {
        kScriptGraphNode n;
        n.id   = newId();
        n.type = type;
        n.name = kScriptNodeTypeName(type);
        n.posX = x;
        n.posY = y;

        auto add = [&](std::vector<kScriptGraphPin> &vec, const kString &nm,
                       kScriptPinType t, bool out) {
            kScriptGraphPin p;
            p.id       = newId();
            p.name     = nm;
            p.type     = t;
            p.isOutput = out;
            vec.push_back(p);
        };
        auto in  = [&](const kString &nm, kScriptPinType t) { add(n.inputs,  nm, t, false); };
        auto out = [&](const kString &nm, kScriptPinType t) { add(n.outputs, nm, t, true); };
        auto defFloatPin = [&](const kString &nm, float v) {
            for (auto &p : n.inputs)
                if (p.name == nm) { p.defFloat = v; return; }
        };
        auto defVecPin = [&](const kString &nm, float x, float y, float z) {
            for (auto &p : n.inputs)
                if (p.name == nm) { p.defVec[0] = x; p.defVec[1] = y; p.defVec[2] = z; return; }
        };

        switch (type)
        {
            case kScriptNodeType::EventAwake:
            case kScriptNodeType::EventStart:
            case kScriptNodeType::EventUpdate:
            case kScriptNodeType::EventFixedUpdate:
            case kScriptNodeType::EventLateUpdate:
            case kScriptNodeType::EventOnDestroy:
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::Branch:
                in("", kScriptPinType::Exec);
                in("Condition", kScriptPinType::Bool);
                out("True", kScriptPinType::Exec);
                out("False", kScriptPinType::Exec);
                break;

            case kScriptNodeType::Print:
            case kScriptNodeType::PrintConsole:
                in("", kScriptPinType::Exec);
                in("Text", kScriptPinType::String);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::SetPosition:
            case kScriptNodeType::SetRotation:
            case kScriptNodeType::SetScale:
                in("", kScriptPinType::Exec);
                in("Target", kScriptPinType::Object);
                in("Value", kScriptPinType::Vec3);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::Translate:
                in("", kScriptPinType::Exec);
                in("Target", kScriptPinType::Object);
                in("Delta", kScriptPinType::Vec3);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::Rotate:
                in("", kScriptPinType::Exec);
                in("Target", kScriptPinType::Object);
                in("Axis", kScriptPinType::Vec3);
                in("Speed", kScriptPinType::Float);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::SetActive:
                in("", kScriptPinType::Exec);
                in("Target", kScriptPinType::Object);
                in("Active", kScriptPinType::Bool);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::SetVariable:
                in("", kScriptPinType::Exec);
                in("Value", kScriptPinType::Float);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::GetSelf:
                out("Self", kScriptPinType::Object);
                break;

            case kScriptNodeType::GetPosition:
            case kScriptNodeType::GetRotation:
            case kScriptNodeType::GetScale:
                in("Target", kScriptPinType::Object);
                out("Value", kScriptPinType::Vec3);
                break;

            case kScriptNodeType::GetForward:
            case kScriptNodeType::GetRight:
            case kScriptNodeType::GetUp:
                in("Target", kScriptPinType::Object);
                out("Dir", kScriptPinType::Vec3);
                break;

            case kScriptNodeType::GetDeltaTime:
                out("Dt", kScriptPinType::Float);
                break;

            case kScriptNodeType::GetVariable:
                out("Value", kScriptPinType::Float);
                break;

            case kScriptNodeType::LiteralFloat:
                out("Value", kScriptPinType::Float);
                break;
            case kScriptNodeType::LiteralBool:
                out("Value", kScriptPinType::Bool);
                break;
            case kScriptNodeType::LiteralString:
                out("Value", kScriptPinType::String);
                break;
            case kScriptNodeType::LiteralVec3:
                out("Value", kScriptPinType::Vec3);
                break;

            case kScriptNodeType::Add:
            case kScriptNodeType::Subtract:
            case kScriptNodeType::Multiply:
            case kScriptNodeType::Divide:
                in("A", kScriptPinType::Float);
                in("B", kScriptPinType::Float);
                out("Result", kScriptPinType::Float);
                break;

            case kScriptNodeType::MakeVec3:
                in("X", kScriptPinType::Float);
                in("Y", kScriptPinType::Float);
                in("Z", kScriptPinType::Float);
                out("Vec3", kScriptPinType::Vec3);
                break;

            case kScriptNodeType::BreakVec3:
                in("Vec3", kScriptPinType::Vec3);
                out("X", kScriptPinType::Float);
                out("Y", kScriptPinType::Float);
                out("Z", kScriptPinType::Float);
                break;

            case kScriptNodeType::ScaleVec3:
                in("Vec3", kScriptPinType::Vec3);
                in("Scale", kScriptPinType::Float);
                out("Result", kScriptPinType::Vec3);
                break;

            case kScriptNodeType::Greater:
            case kScriptNodeType::Less:
            case kScriptNodeType::Equal:
                in("A", kScriptPinType::Float);
                in("B", kScriptPinType::Float);
                out("Result", kScriptPinType::Bool);
                break;

            case kScriptNodeType::And:
            case kScriptNodeType::Or:
                in("A", kScriptPinType::Bool);
                in("B", kScriptPinType::Bool);
                out("Result", kScriptPinType::Bool);
                break;

            case kScriptNodeType::Not:
                in("A", kScriptPinType::Bool);
                out("Result", kScriptPinType::Bool);
                break;

            case kScriptNodeType::GetAction:
            case kScriptNodeType::GetActionPressed:
            case kScriptNodeType::GetActionReleased:
                out("Value", kScriptPinType::Bool);
                break;

            case kScriptNodeType::GetAxis:
                out("Value", kScriptPinType::Float);
                break;

            // --- Audio -------------------------------------------------------
            case kScriptNodeType::PlaySound:
                in("", kScriptPinType::Exec);
                in("File", kScriptPinType::String);
                in("Loop", kScriptPinType::Bool);
                in("Volume", kScriptPinType::Float);
                in("Pitch", kScriptPinType::Float);
                out("", kScriptPinType::Exec);
                defFloatPin("Volume", 1.0f);
                defFloatPin("Pitch", 1.0f);
                break;

            case kScriptNodeType::StopAllSounds:
                in("", kScriptPinType::Exec);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::SetMasterVolume:
                in("", kScriptPinType::Exec);
                in("Volume", kScriptPinType::Float);
                out("", kScriptPinType::Exec);
                defFloatPin("Volume", 1.0f);
                break;

            case kScriptNodeType::GetMasterVolume:
                out("Value", kScriptPinType::Float);
                break;

            case kScriptNodeType::SetListenerPosition:
                in("", kScriptPinType::Exec);
                in("Position", kScriptPinType::Vec3);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::SetListenerDirection:
                in("", kScriptPinType::Exec);
                in("Forward", kScriptPinType::Vec3);
                in("Up", kScriptPinType::Vec3);
                out("", kScriptPinType::Exec);
                defVecPin("Forward", 0.0f, 0.0f, -1.0f);
                defVecPin("Up", 0.0f, 1.0f, 0.0f);
                break;

            // --- Animation ---------------------------------------------------
            case kScriptNodeType::GetAnimator:
                in("Target", kScriptPinType::Object);
                out("Animator", kScriptPinType::Object);
                break;

            case kScriptNodeType::PlayAnimation:
                in("", kScriptPinType::Exec);
                in("Animator", kScriptPinType::Object);
                in("Index", kScriptPinType::Float);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::SetAnimatorSpeed:
                in("", kScriptPinType::Exec);
                in("Animator", kScriptPinType::Object);
                in("Speed", kScriptPinType::Float);
                out("", kScriptPinType::Exec);
                defFloatPin("Speed", 1.0f);
                break;

            case kScriptNodeType::SetAnimatorTime:
                in("", kScriptPinType::Exec);
                in("Animator", kScriptPinType::Object);
                in("Time", kScriptPinType::Float);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::GetAnimatorSpeed:
                in("Animator", kScriptPinType::Object);
                out("Speed", kScriptPinType::Float);
                break;

            case kScriptNodeType::SetAnimatorBool:
                in("", kScriptPinType::Exec);
                in("Animator", kScriptPinType::Object);
                in("Name", kScriptPinType::String);
                in("Value", kScriptPinType::Bool);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::SetAnimatorFloat:
                in("", kScriptPinType::Exec);
                in("Animator", kScriptPinType::Object);
                in("Name", kScriptPinType::String);
                in("Value", kScriptPinType::Float);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::SetAnimatorInt:
                in("", kScriptPinType::Exec);
                in("Animator", kScriptPinType::Object);
                in("Name", kScriptPinType::String);
                in("Value", kScriptPinType::Float);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::SetAnimatorTrigger:
                in("", kScriptPinType::Exec);
                in("Animator", kScriptPinType::Object);
                in("Name", kScriptPinType::String);
                out("", kScriptPinType::Exec);
                break;

            // --- Physics -----------------------------------------------------
            case kScriptNodeType::GetPhysicsObject:
                in("Target", kScriptPinType::Object);
                out("Physics", kScriptPinType::Object);
                break;

            case kScriptNodeType::ApplyForce:
                in("", kScriptPinType::Exec);
                in("Physics", kScriptPinType::Object);
                in("Force", kScriptPinType::Vec3);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::ApplyImpulse:
                in("", kScriptPinType::Exec);
                in("Physics", kScriptPinType::Object);
                in("Impulse", kScriptPinType::Vec3);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::ApplyTorque:
                in("", kScriptPinType::Exec);
                in("Physics", kScriptPinType::Object);
                in("Torque", kScriptPinType::Vec3);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::SetLinearVelocity:
            case kScriptNodeType::SetAngularVelocity:
                in("", kScriptPinType::Exec);
                in("Physics", kScriptPinType::Object);
                in("Velocity", kScriptPinType::Vec3);
                out("", kScriptPinType::Exec);
                break;

            case kScriptNodeType::GetPhysicsVelocity:
                in("Physics", kScriptPinType::Object);
                out("Velocity", kScriptPinType::Vec3);
                break;

            case kScriptNodeType::GetPhysicsPosition:
                in("Physics", kScriptPinType::Object);
                out("Position", kScriptPinType::Vec3);
                break;

            case kScriptNodeType::SetPhysicsGravity:
                in("", kScriptPinType::Exec);
                in("Gravity", kScriptPinType::Vec3);
                out("", kScriptPinType::Exec);
                defVecPin("Gravity", 0.0f, -9.81f, 0.0f);
                break;

            case kScriptNodeType::GetPhysicsGravity:
                out("Gravity", kScriptPinType::Vec3);
                break;

            case kScriptNodeType::IsPhysicsActive:
                in("Physics", kScriptPinType::Object);
                out("Active", kScriptPinType::Bool);
                break;

            case kScriptNodeType::Anchor:
                in("In", kScriptPinType::Float);
                out("Out", kScriptPinType::Float);
                break;

            case kScriptNodeType::Comment:
                n.sizeX   = 320.0f;
                n.sizeY   = 180.0f;
                n.comment = "Comment";
                break;

            default:
                break;
        }
        return n;
    }

    // -----------------------------------------------------------------------
    // kScriptGraph — JSON serialisation
    // -----------------------------------------------------------------------

    static kJson pinToJson(const kScriptGraphPin &p)
    {
        return kJson{
            {"id", p.id},
            {"name", p.name},
            {"type", (int)p.type},
            {"is_output", p.isOutput},
            {"def_float", p.defFloat},
            {"def_vec", {p.defVec[0], p.defVec[1], p.defVec[2]}},
            {"def_bool", p.defBool},
            {"def_str", p.defStr},
        };
    }

    static kScriptGraphPin pinFromJson(const kJson &j)
    {
        kScriptGraphPin p;
        p.id       = j.value("id", 0);
        p.name     = j.value("name", std::string());
        p.type     = (kScriptPinType)j.value("type", 1);
        p.isOutput = j.value("is_output", false);
        p.defFloat = j.value("def_float", 0.0f);
        p.defBool  = j.value("def_bool", false);
        p.defStr   = j.value("def_str", std::string());
        if (j.contains("def_vec") && j["def_vec"].is_array() && j["def_vec"].size() == 3)
            for (int i = 0; i < 3; ++i)
                p.defVec[i] = j["def_vec"][i].get<float>();
        return p;
    }

    kJson kScriptGraph::toJson() const
    {
        kJson jnodes = kJson::array();
        for (const auto &n : nodes)
        {
            kJson jin  = kJson::array();
            kJson jout = kJson::array();
            for (const auto &p : n.inputs)  jin.push_back(pinToJson(p));
            for (const auto &p : n.outputs) jout.push_back(pinToJson(p));
            jnodes.push_back({
                {"id", n.id},
                {"type", (int)n.type},
                {"name", n.name},
                {"x", n.posX},
                {"y", n.posY},
                {"value_float", {n.valueFloat[0], n.valueFloat[1], n.valueFloat[2]}},
                {"value_bool", n.valueBool},
                {"value_str", n.valueStr},
                {"size_x", n.sizeX},
                {"size_y", n.sizeY},
                {"comment", n.comment},
                {"inputs", jin},
                {"outputs", jout},
            });
        }

        kJson jlinks = kJson::array();
        for (const auto &l : links)
            jlinks.push_back({
                {"id", l.id},
                {"from_node", l.fromNode}, {"from_pin", l.fromPin},
                {"to_node", l.toNode},     {"to_pin", l.toPin},
            });

        kJson jvars = kJson::array();
        for (const auto &v : variables)
            jvars.push_back({{"name", v.name}, {"def", v.defValue}});

        return kJson{
            {"kind", "script_graph"},
            {"uuid", uuid},
            {"name", name},
            {"next_id", nextId},
            {"nodes", jnodes},
            {"links", jlinks},
            {"variables", jvars},
        };
    }

    void kScriptGraph::fromJson(const kJson &j)
    {
        nodes.clear();
        links.clear();
        variables.clear();

        uuid   = j.value("uuid", std::string());
        name   = j.value("name", std::string());
        nextId = j.value("next_id", 1);
        dirty  = false;

        if (j.contains("nodes") && j["nodes"].is_array())
        {
            for (const auto &jn : j["nodes"])
            {
                kScriptGraphNode n;
                n.id   = jn.value("id", 0);
                n.type = (kScriptNodeType)jn.value("type", 0);
                n.name = jn.value("name", std::string());
                n.posX = jn.value("x", 0.0f);
                n.posY = jn.value("y", 0.0f);
                n.valueBool = jn.value("value_bool", false);
                n.valueStr  = jn.value("value_str", std::string());
                n.sizeX     = jn.value("size_x", 300.0f);
                n.sizeY     = jn.value("size_y", 200.0f);
                n.comment   = jn.value("comment", std::string());
                if (jn.contains("value_float") && jn["value_float"].is_array() &&
                    jn["value_float"].size() == 3)
                    for (int i = 0; i < 3; ++i)
                        n.valueFloat[i] = jn["value_float"][i].get<float>();
                if (jn.contains("inputs"))
                    for (const auto &jp : jn["inputs"])
                        n.inputs.push_back(pinFromJson(jp));
                if (jn.contains("outputs"))
                    for (const auto &jp : jn["outputs"])
                        n.outputs.push_back(pinFromJson(jp));
                nodes.push_back(n);
            }
        }

        if (j.contains("links") && j["links"].is_array())
        {
            for (const auto &jl : j["links"])
            {
                kScriptGraphLink l;
                l.id       = jl.value("id", 0);
                l.fromNode = jl.value("from_node", 0);
                l.fromPin  = jl.value("from_pin", 0);
                l.toNode   = jl.value("to_node", 0);
                l.toPin    = jl.value("to_pin", 0);
                links.push_back(l);
            }
        }

        if (j.contains("variables") && j["variables"].is_array())
        {
            for (const auto &jv : j["variables"])
            {
                kScriptGraphVar v;
                v.name     = jv.value("name", std::string());
                v.defValue = jv.value("def", 0.0f);
                variables.push_back(v);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Code generation
    // -----------------------------------------------------------------------

    namespace
    {
        // Formats a float as a valid AngelScript float literal (always typed 'f').
        kString formatFloat(float v)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", (double)v);
            kString s = buf;
            if (s.find('.') == kString::npos && s.find('e') == kString::npos &&
                s.find("inf") == kString::npos && s.find("nan") == kString::npos)
                s += ".0";
            s += "f";
            return s;
        }

        kString escapeString(const kString &in)
        {
            kString out;
            for (char c : in)
            {
                if (c == '\\' || c == '"') { out += '\\'; out += c; }
                else if (c == '\n')        { out += "\\n"; }
                else if (c == '\t')        { out += "\\t"; }
                else if (c == '\r')        { /* drop */ }
                else                       { out += c; }
            }
            return out;
        }

        const char *eventFuncName(kScriptNodeType t)
        {
            switch (t)
            {
                case kScriptNodeType::EventAwake:       return "Awake";
                case kScriptNodeType::EventStart:       return "Start";
                case kScriptNodeType::EventUpdate:      return "Update";
                case kScriptNodeType::EventFixedUpdate: return "FixedUpdate";
                case kScriptNodeType::EventLateUpdate:  return "LateUpdate";
                case kScriptNodeType::EventOnDestroy:   return "OnDestroy";
                default:                                return nullptr;
            }
        }

        // Walks the graph and emits AngelScript text.
        struct Codegen
        {
            const kScriptGraph &g;
            kString             error;
            std::set<int>       dataStack; ///< Guards against data-wire cycles.

            explicit Codegen(const kScriptGraph &graph) : g(graph) {}

            const kScriptGraphPin *inPin(const kScriptGraphNode &n, const char *name) const
            {
                for (auto &p : n.inputs)
                    if (p.name == name)
                        return &p;
                return nullptr;
            }

            const kScriptGraphPin *firstExecOut(const kScriptGraphNode &n,
                                                const char *name = nullptr) const
            {
                for (auto &p : n.outputs)
                    if (p.type == kScriptPinType::Exec && (!name || p.name == name))
                        return &p;
                return nullptr;
            }

            int execTarget(const kScriptGraphNode &n, const kScriptGraphPin &execOut) const
            {
                const kScriptGraphLink *l = g.outgoingLink(n.id, execOut.id);
                return l ? l->toNode : 0;
            }

            kString pinDefault(const kScriptGraphPin &p) const
            {
                switch (p.type)
                {
                    case kScriptPinType::Float:  return formatFloat(p.defFloat);
                    case kScriptPinType::Bool:   return p.defBool ? "true" : "false";
                    case kScriptPinType::String: return "\"" + escapeString(p.defStr) + "\"";
                    case kScriptPinType::Vec3:
                        return "kVec3(" + formatFloat(p.defVec[0]) + ", " +
                               formatFloat(p.defVec[1]) + ", " +
                               formatFloat(p.defVec[2]) + ")";
                    case kScriptPinType::Object: return "getSelf()";
                    default:                     return "0";
                }
            }

            kString emitInput(const kScriptGraphNode &n, const kScriptGraphPin &pin)
            {
                const kScriptGraphLink *link = g.incomingLink(n.id, pin.id);
                if (!link)
                {
                    if (pin.type == kScriptPinType::Object)
                    {
                        if (pin.name == "Animator") return "getAnimator(getSelf())";
                        if (pin.name == "Physics")  return "getSelf().getPhysicsObject()";
                    }
                    return pinDefault(pin);
                }
                const kScriptGraphNode *src = g.findNode(link->fromNode);
                if (!src)
                    return pinDefault(pin);
                return emitNode(*src, link->fromPin);
            }

            kString emitNamedInput(const kScriptGraphNode &n, const char *name)
            {
                const kScriptGraphPin *p = inPin(n, name);
                return p ? emitInput(n, *p) : kString("0");
            }

            // Expression for a data output pin of a node.
            kString emitNode(const kScriptGraphNode &n, int outPinId)
            {
                if (dataStack.count(n.id))
                {
                    error = "Cycle detected in data wires.";
                    return "0";
                }
                dataStack.insert(n.id);
                kString r = emitNodeImpl(n, outPinId);
                dataStack.erase(n.id);
                return r;
            }

            kString emitNodeImpl(const kScriptGraphNode &n, int outPinId)
            {
                switch (n.type)
                {
                    case kScriptNodeType::Anchor:
                        // A reroute node forwards whatever feeds its input,
                        // regardless of wire type. Editor-only nodes never
                        // reach this point unless a link points at them.
                        return n.inputs.empty() ? kString("0") : emitInput(n, n.inputs[0]);

                    case kScriptNodeType::LiteralFloat:  return formatFloat(n.valueFloat[0]);
                    case kScriptNodeType::LiteralBool:   return n.valueBool ? "true" : "false";
                    case kScriptNodeType::LiteralString: return "\"" + escapeString(n.valueStr) + "\"";
                    case kScriptNodeType::LiteralVec3:
                        return "kVec3(" + formatFloat(n.valueFloat[0]) + ", " +
                               formatFloat(n.valueFloat[1]) + ", " +
                               formatFloat(n.valueFloat[2]) + ")";

                    case kScriptNodeType::GetSelf:      return "getSelf()";
                    case kScriptNodeType::GetDeltaTime: return "getDeltaTime()";
                    case kScriptNodeType::GetVariable:
                        return n.valueStr.empty() ? kString("0.0f") : n.valueStr;

                    case kScriptNodeType::GetPosition:  return emitNamedInput(n, "Target") + ".getPosition()";
                    case kScriptNodeType::GetRotation:  return emitNamedInput(n, "Target") + ".getRotation()";
                    case kScriptNodeType::GetScale:     return emitNamedInput(n, "Target") + ".getScale()";
                    case kScriptNodeType::GetForward:   return emitNamedInput(n, "Target") + ".forward()";
                    case kScriptNodeType::GetRight:     return emitNamedInput(n, "Target") + ".right()";
                    case kScriptNodeType::GetUp:        return emitNamedInput(n, "Target") + ".up()";

                    case kScriptNodeType::Add:
                        return "(" + emitNamedInput(n, "A") + " + " + emitNamedInput(n, "B") + ")";
                    case kScriptNodeType::Subtract:
                        return "(" + emitNamedInput(n, "A") + " - " + emitNamedInput(n, "B") + ")";
                    case kScriptNodeType::Multiply:
                        return "(" + emitNamedInput(n, "A") + " * " + emitNamedInput(n, "B") + ")";
                    case kScriptNodeType::Divide:
                        return "(" + emitNamedInput(n, "A") + " / " + emitNamedInput(n, "B") + ")";

                    case kScriptNodeType::MakeVec3:
                        return "kVec3(" + emitNamedInput(n, "X") + ", " +
                               emitNamedInput(n, "Y") + ", " + emitNamedInput(n, "Z") + ")";
                    case kScriptNodeType::BreakVec3:
                    {
                        kString v = emitNamedInput(n, "Vec3");
                        for (size_t i = 0; i < n.outputs.size(); ++i)
                            if (n.outputs[i].id == outPinId)
                                return v + (i == 0 ? ".x" : i == 1 ? ".y" : ".z");
                        return v + ".x";
                    }
                    case kScriptNodeType::ScaleVec3:
                        return "(" + emitNamedInput(n, "Vec3") + " * " +
                               emitNamedInput(n, "Scale") + ")";

                    case kScriptNodeType::Greater:
                        return "(" + emitNamedInput(n, "A") + " > " + emitNamedInput(n, "B") + ")";
                    case kScriptNodeType::Less:
                        return "(" + emitNamedInput(n, "A") + " < " + emitNamedInput(n, "B") + ")";
                    case kScriptNodeType::Equal:
                        return "(" + emitNamedInput(n, "A") + " == " + emitNamedInput(n, "B") + ")";
                    case kScriptNodeType::And:
                        return "(" + emitNamedInput(n, "A") + " && " + emitNamedInput(n, "B") + ")";
                    case kScriptNodeType::Or:
                        return "(" + emitNamedInput(n, "A") + " || " + emitNamedInput(n, "B") + ")";
                    case kScriptNodeType::Not:
                        return "(!" + emitNamedInput(n, "A") + ")";

                    case kScriptNodeType::GetAction:
                        return "getAction(\"" + escapeString(n.valueStr) + "\")";
                    case kScriptNodeType::GetActionPressed:
                        return "getActionPressed(\"" + escapeString(n.valueStr) + "\")";
                    case kScriptNodeType::GetActionReleased:
                        return "getActionReleased(\"" + escapeString(n.valueStr) + "\")";
                    case kScriptNodeType::GetAxis:
                        return "getAxis(\"" + escapeString(n.valueStr) + "\")";

                    case kScriptNodeType::GetMasterVolume:
                        return "getMasterVolume()";

                    case kScriptNodeType::GetAnimator:
                        return "getAnimator(" + emitNamedInput(n, "Target") + ")";
                    case kScriptNodeType::GetAnimatorSpeed:
                        return emitNamedInput(n, "Animator") + ".getSpeed()";

                    case kScriptNodeType::GetPhysicsObject:
                        return emitNamedInput(n, "Target") + ".getPhysicsObject()";
                    case kScriptNodeType::GetPhysicsVelocity:
                        return emitNamedInput(n, "Physics") + ".getLinearVelocity()";
                    case kScriptNodeType::GetPhysicsPosition:
                        return emitNamedInput(n, "Physics") + ".getPosition()";
                    case kScriptNodeType::GetPhysicsGravity:
                        return "getPhysicsGravity()";
                    case kScriptNodeType::IsPhysicsActive:
                        return emitNamedInput(n, "Physics") + ".isActive()";

                    default:
                        return "0";
                }
            }

            // Statement text for one action node.
            kString statement(const kScriptGraphNode &n)
            {
                switch (n.type)
                {
                    case kScriptNodeType::Print:
                        return "print(" + emitNamedInput(n, "Text") + ");";
                    case kScriptNodeType::PrintConsole:
                        return "printConsole(" + emitNamedInput(n, "Text") + ");";
                    case kScriptNodeType::SetPosition:
                        return emitNamedInput(n, "Target") + ".setPosition(" +
                               emitNamedInput(n, "Value") + ");";
                    case kScriptNodeType::SetRotation:
                        return emitNamedInput(n, "Target") + ".setRotation(" +
                               emitNamedInput(n, "Value") + ");";
                    case kScriptNodeType::SetScale:
                        return emitNamedInput(n, "Target") + ".setScale(" +
                               emitNamedInput(n, "Value") + ");";
                    case kScriptNodeType::Translate:
                        return emitNamedInput(n, "Target") + ".translate(" +
                               emitNamedInput(n, "Delta") + ");";
                    case kScriptNodeType::Rotate:
                        return emitNamedInput(n, "Target") + ".rotate(" +
                               emitNamedInput(n, "Axis") + ", " +
                               emitNamedInput(n, "Speed") + ");";
                    case kScriptNodeType::SetActive:
                        return emitNamedInput(n, "Target") + ".setActive(" +
                               emitNamedInput(n, "Active") + ");";
                    case kScriptNodeType::SetVariable:
                        return n.valueStr.empty()
                                   ? kString("// Set Variable: no variable selected")
                                   : n.valueStr + " = " + emitNamedInput(n, "Value") + ";";

                    case kScriptNodeType::PlaySound:
                        return "playAudio(" + emitNamedInput(n, "File") + ", " +
                               emitNamedInput(n, "Loop") + ", " +
                               emitNamedInput(n, "Volume") + ", " +
                               emitNamedInput(n, "Pitch") + ");";
                    case kScriptNodeType::StopAllSounds:
                        return "stopAllAudio();";
                    case kScriptNodeType::SetMasterVolume:
                        return "setMasterVolume(" + emitNamedInput(n, "Volume") + ");";
                    case kScriptNodeType::SetListenerPosition:
                        return "setListenerPosition(" + emitNamedInput(n, "Position") + ");";
                    case kScriptNodeType::SetListenerDirection:
                        return "setListenerDirection(" + emitNamedInput(n, "Forward") + ", " +
                               emitNamedInput(n, "Up") + ");";

                    case kScriptNodeType::PlayAnimation:
                        return emitNamedInput(n, "Animator") + ".playAnimation(" +
                               emitNamedInput(n, "Index") + ");";
                    case kScriptNodeType::SetAnimatorSpeed:
                        return emitNamedInput(n, "Animator") + ".setSpeed(" +
                               emitNamedInput(n, "Speed") + ");";
                    case kScriptNodeType::SetAnimatorTime:
                        return emitNamedInput(n, "Animator") + ".setCurrentTime(" +
                               emitNamedInput(n, "Time") + ");";
                    case kScriptNodeType::SetAnimatorBool:
                        return emitNamedInput(n, "Animator") + ".setBool(" +
                               emitNamedInput(n, "Name") + ", " +
                               emitNamedInput(n, "Value") + ");";
                    case kScriptNodeType::SetAnimatorFloat:
                        return emitNamedInput(n, "Animator") + ".setFloat(" +
                               emitNamedInput(n, "Name") + ", " +
                               emitNamedInput(n, "Value") + ");";
                    case kScriptNodeType::SetAnimatorInt:
                        return emitNamedInput(n, "Animator") + ".setInt(" +
                               emitNamedInput(n, "Name") + ", " +
                               emitNamedInput(n, "Value") + ");";
                    case kScriptNodeType::SetAnimatorTrigger:
                        return emitNamedInput(n, "Animator") + ".setTrigger(" +
                               emitNamedInput(n, "Name") + ");";

                    case kScriptNodeType::ApplyForce:
                        return emitNamedInput(n, "Physics") + ".applyForce(" +
                               emitNamedInput(n, "Force") + ");";
                    case kScriptNodeType::ApplyImpulse:
                        return emitNamedInput(n, "Physics") + ".applyImpulse(" +
                               emitNamedInput(n, "Impulse") + ");";
                    case kScriptNodeType::ApplyTorque:
                        return emitNamedInput(n, "Physics") + ".applyTorque(" +
                               emitNamedInput(n, "Torque") + ");";
                    case kScriptNodeType::SetLinearVelocity:
                        return emitNamedInput(n, "Physics") + ".setLinearVelocity(" +
                               emitNamedInput(n, "Velocity") + ");";
                    case kScriptNodeType::SetAngularVelocity:
                        return emitNamedInput(n, "Physics") + ".setAngularVelocity(" +
                               emitNamedInput(n, "Velocity") + ");";
                    case kScriptNodeType::SetPhysicsGravity:
                        return "setPhysicsGravity(" + emitNamedInput(n, "Gravity") + ");";

                    default:
                        return "";
                }
            }

            // Emits the exec chain starting at nodeId. visited is taken by value so
            // each branch path is independent while still catching cycles.
            kString emitExec(int nodeId, int indent, std::set<int> visited)
            {
                kString out;
                kString pad((size_t)indent, ' ');
                int cur = nodeId;

                while (cur != 0)
                {
                    if (visited.count(cur))
                        break;
                    visited.insert(cur);

                    const kScriptGraphNode *n = g.findNode(cur);
                    if (!n)
                        break;

                    if (n->type == kScriptNodeType::Anchor)
                    {
                        // Pass-through reroute on an execution wire: follow the
                        // first outgoing link without emitting a statement.
                        const kScriptGraphLink *next = nullptr;
                        for (const auto &p : n->outputs)
                        {
                            next = g.outgoingLink(n->id, p.id);
                            if (next)
                                break;
                        }
                        cur = next ? next->toNode : 0;
                        continue;
                    }

                    if (n->type == kScriptNodeType::Branch)
                    {
                        kString cond = emitNamedInput(*n, "Condition");
                        const kScriptGraphPin *tp = firstExecOut(*n, "True");
                        const kScriptGraphPin *fp = firstExecOut(*n, "False");
                        int tTarget = tp ? execTarget(*n, *tp) : 0;
                        int fTarget = fp ? execTarget(*n, *fp) : 0;

                        out += pad + "if (" + cond + ")\n" + pad + "{\n";
                        out += emitExec(tTarget, indent + 4, visited);
                        out += pad + "}\n";
                        if (fTarget != 0)
                        {
                            out += pad + "else\n" + pad + "{\n";
                            out += emitExec(fTarget, indent + 4, visited);
                            out += pad + "}\n";
                        }
                        break; // a branch ends the linear chain
                    }

                    kString s = statement(*n);
                    if (!s.empty())
                        out += pad + s + "\n";

                    const kScriptGraphPin *eo = firstExecOut(*n);
                    cur = eo ? execTarget(*n, *eo) : 0;
                }
                return out;
            }
        };
    } // namespace

    kScriptGraphResult kScriptGraphCompiler::compile(const kScriptGraph &graph)
    {
        kScriptGraphResult res;
        Codegen cg(graph);

        kString code;
        code += "// Generated by the Kemena3D visual script editor.\n";
        code += "// Source graph: " +
                (graph.name.empty() ? kString("untitled") : graph.name) + "\n";
        code += "// Do not edit by hand — regenerated whenever the graph is saved.\n\n";

        // Graph variables become file-scope globals.
        for (const auto &v : graph.variables)
        {
            if (v.name.empty())
                continue;
            code += "float " + v.name + " = " + formatFloat(v.defValue) + ";\n";
        }
        if (!graph.variables.empty())
            code += "\n";

        // Each event node becomes one lifecycle function.
        std::set<int> emittedEvents;
        for (const auto &n : graph.nodes)
        {
            const char *fn = eventFuncName(n.type);
            if (!fn)
                continue;
            if (emittedEvents.count((int)n.type))
                continue; // ignore duplicate event nodes of the same kind
            emittedEvents.insert((int)n.type);

            const kScriptGraphPin *eo = cg.firstExecOut(n);
            int first = eo ? cg.execTarget(n, *eo) : 0;

            kString body = cg.emitExec(first, 4, std::set<int>());
            code += kString("void ") + fn + "()\n{\n" + body + "}\n\n";
        }

        if (!cg.error.empty())
        {
            res.success = false;
            res.error   = cg.error;
            res.code    = code;
            return res;
        }

        res.success = true;
        res.code    = code;
        return res;
    }
}
