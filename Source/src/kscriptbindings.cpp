#include "kscriptbindings.h"
#include "kobject.h"

#include <new>
#include <cstdio>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace kemena
{
    // The manager whose scripts are currently being driven. getSelf() and the
    // time accessors resolve through this. Each kWorld owns its own manager and
    // sets this in its constructor (registerScriptBindings); when several worlds
    // exist (e.g. editor previews), the world about to run scripts must reclaim
    // it via setActiveScriptContext() — see kWorld::updateScripts().
    static kScriptManager *g_boundManager = nullptr;

    // -----------------------------------------------------------------------
    // kVec3 value type helpers
    // -----------------------------------------------------------------------

    static void vec3DefaultCtor(void *mem)                  { new (mem) kVec3(0.0f); }
    static void vec3InitCtor(float x, float y, float z, void *mem) { new (mem) kVec3(x, y, z); }
    static void vec3CopyCtor(const kVec3 &other, void *mem) { new (mem) kVec3(other); }

    static kVec3 vec3Add(const kVec3 &a, const kVec3 &b) { return a + b; }
    static kVec3 vec3Sub(const kVec3 &a, const kVec3 &b) { return a - b; }
    static kVec3 vec3MulF(const kVec3 &a, float s)       { return a * s; }
    static kVec3 vec3Neg(const kVec3 &a)                 { return -a; }
    static bool  vec3Equals(const kVec3 &a, const kVec3 &b) { return a == b; }
    static float vec3Length(const kVec3 &a)              { return glm::length(a); }
    static kVec3 vec3Normalized(const kVec3 &a)
    {
        float len = glm::length(a);
        return len > 0.0f ? a / len : kVec3(0.0f);
    }
    static float vec3Dot(const kVec3 &a, const kVec3 &b)   { return glm::dot(a, b); }
    static kVec3 vec3Cross(const kVec3 &a, const kVec3 &b) { return glm::cross(a, b); }

    // -----------------------------------------------------------------------
    // kObject method wrappers (object-first calling convention)
    // -----------------------------------------------------------------------

    static kString objGetName(kObject *o)                       { return o->getName(); }
    static void    objSetName(kObject *o, const kString &n)     { o->setName(n); }
    static kString objGetUuid(kObject *o)                       { return o->getUuid(); }

    static kVec3 objGetPosition(kObject *o)                     { return o->getPosition(); }
    static void  objSetPosition(kObject *o, const kVec3 &v)     { o->setPosition(v); }
    static kVec3 objGetGlobalPosition(kObject *o)               { return o->getGlobalPosition(); }

    static kVec3 objGetScale(kObject *o)                        { return o->getScale(); }
    static void  objSetScale(kObject *o, const kVec3 &v)        { o->setScale(v); }

    // Rotation is exposed to scripts as Euler angles in degrees, which are far
    // friendlier than quaternions for gameplay code.
    static kVec3 objGetRotation(kObject *o)                     { return o->getRotationEuler(); }
    static void  objSetRotation(kObject *o, const kVec3 &deg)
    {
        o->setRotation(kQuat(glm::radians(deg)));
    }

    static kVec3 objForward(kObject *o)                         { return o->calculateForward(); }
    static kVec3 objRight(kObject *o)                           { return o->calculateRight(); }
    static kVec3 objUp(kObject *o)                              { return o->calculateUp(); }

    static void  objRotate(kObject *o, const kVec3 &axis, float spd) { o->rotate(axis, spd); }
    static void  objTranslate(kObject *o, const kVec3 &delta)
    {
        o->setPosition(o->getPosition() + delta);
    }

    static bool  objGetActive(kObject *o)                       { return o->getActive(); }
    static void  objSetActive(kObject *o, bool a)               { o->setActive(a); }
    static kObject *objGetParent(kObject *o)                    { return o->getParent(); }

    // -----------------------------------------------------------------------
    // Global functions
    // -----------------------------------------------------------------------

    static kObject *scriptGetSelf()
    {
        return g_boundManager ? g_boundManager->getActiveObject() : nullptr;
    }
    static float scriptDeltaTime()
    {
        return g_boundManager ? g_boundManager->getDeltaTime() : 0.0f;
    }
    static float scriptFixedDeltaTime()
    {
        return g_boundManager ? g_boundManager->getFixedDeltaTime() : 0.0f;
    }
    static void scriptPrint(const kString &msg)
    {
        printf("[Script] %s\n", msg.c_str());
    }

    // -----------------------------------------------------------------------
    // Registration
    // -----------------------------------------------------------------------

    void setActiveScriptContext(kScriptManager *manager)
    {
        // Lightweight: only repoints the host-API context. No engine work.
        g_boundManager = manager;
    }

    void registerScriptBindings(kScriptManager *manager)
    {
        if (!manager)
            return;
        g_boundManager = manager;

        asIScriptEngine *e = manager->getEngine();
        if (!e)
            return;

        int r = 0;
        (void)r;

        // --- kVec3 -----------------------------------------------------------
        // asGetTypeTraits<>() supplies the required asOBJ_APP_CLASS kind bit plus
        // the correct constructor/copy traits for glm::vec3; ALLFLOATS is the
        // register-passing hint. (asOBJ_APP_CLASS_ALLFLOATS alone is rejected
        // with asINVALID_ARG because the app-class kind bit is missing.)
        r = e->RegisterObjectType("kVec3", sizeof(kVec3),
                                  asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS |
                                  asGetTypeTraits<kVec3>());
        assert(r >= 0);

        r = e->RegisterObjectBehaviour("kVec3", asBEHAVE_CONSTRUCT, "void f()",
                                       asFUNCTION(vec3DefaultCtor), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = e->RegisterObjectBehaviour("kVec3", asBEHAVE_CONSTRUCT, "void f(float, float, float)",
                                       asFUNCTION(vec3InitCtor), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = e->RegisterObjectBehaviour("kVec3", asBEHAVE_CONSTRUCT, "void f(const kVec3 &in)",
                                       asFUNCTION(vec3CopyCtor), asCALL_CDECL_OBJLAST);
        assert(r >= 0);

        r = e->RegisterObjectProperty("kVec3", "float x", asOFFSET(kVec3, x)); assert(r >= 0);
        r = e->RegisterObjectProperty("kVec3", "float y", asOFFSET(kVec3, y)); assert(r >= 0);
        r = e->RegisterObjectProperty("kVec3", "float z", asOFFSET(kVec3, z)); assert(r >= 0);

        r = e->RegisterObjectMethod("kVec3", "kVec3 opAdd(const kVec3 &in) const",
                                    asFUNCTION(vec3Add), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "kVec3 opSub(const kVec3 &in) const",
                                    asFUNCTION(vec3Sub), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "kVec3 opMul(float) const",
                                    asFUNCTION(vec3MulF), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "kVec3 opNeg() const",
                                    asFUNCTION(vec3Neg), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "bool opEquals(const kVec3 &in) const",
                                    asFUNCTION(vec3Equals), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "float length() const",
                                    asFUNCTION(vec3Length), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "kVec3 normalized() const",
                                    asFUNCTION(vec3Normalized), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "float dot(const kVec3 &in) const",
                                    asFUNCTION(vec3Dot), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "kVec3 cross(const kVec3 &in) const",
                                    asFUNCTION(vec3Cross), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        // --- kObject ---------------------------------------------------------
        // Engine-owned: no reference counting — AngelScript only holds handles.
        r = e->RegisterObjectType("kObject", 0, asOBJ_REF | asOBJ_NOCOUNT);
        assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "string getName() const",
                                    asFUNCTION(objGetName), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void setName(const string &in)",
                                    asFUNCTION(objSetName), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "string getUuid() const",
                                    asFUNCTION(objGetUuid), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "kVec3 getPosition() const",
                                    asFUNCTION(objGetPosition), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void setPosition(const kVec3 &in)",
                                    asFUNCTION(objSetPosition), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "kVec3 getGlobalPosition() const",
                                    asFUNCTION(objGetGlobalPosition), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "kVec3 getScale() const",
                                    asFUNCTION(objGetScale), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void setScale(const kVec3 &in)",
                                    asFUNCTION(objSetScale), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "kVec3 getRotation() const",
                                    asFUNCTION(objGetRotation), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void setRotation(const kVec3 &in)",
                                    asFUNCTION(objSetRotation), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "kVec3 forward() const",
                                    asFUNCTION(objForward), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "kVec3 right() const",
                                    asFUNCTION(objRight), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "kVec3 up() const",
                                    asFUNCTION(objUp), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "void rotate(const kVec3 &in, float)",
                                    asFUNCTION(objRotate), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void translate(const kVec3 &in)",
                                    asFUNCTION(objTranslate), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "bool getActive() const",
                                    asFUNCTION(objGetActive), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void setActive(bool)",
                                    asFUNCTION(objSetActive), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "kObject@ getParent() const",
                                    asFUNCTION(objGetParent), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        // --- Global functions ------------------------------------------------
        r = e->RegisterGlobalFunction("kObject@ getSelf()",
                                      asFUNCTION(scriptGetSelf), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("float getDeltaTime()",
                                      asFUNCTION(scriptDeltaTime), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("float getFixedDeltaTime()",
                                      asFUNCTION(scriptFixedDeltaTime), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("void print(const string &in)",
                                      asFUNCTION(scriptPrint), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("void log(const string &in)",
                                      asFUNCTION(scriptPrint), asCALL_CDECL); assert(r >= 0);
    }
}
