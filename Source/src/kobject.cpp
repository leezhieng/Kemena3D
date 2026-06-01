#include "kmesh.h"
#include <algorithm>

namespace kemena
{
    static unsigned int s_nextObjectId = 1;

    kObject::kObject(kObject *parentNode)
    {
        id = s_nextObjectId++;
        if (parentNode != nullptr)
            setParent(parentNode);
        setType(kNodeType::NODE_TYPE_OBJECT);
    }

    kObject::~kObject()
    {
        kDriver *driver = kDriver::getCurrent();
        if (driver == nullptr) return;

        if (iconVAO)          driver->deleteVertexArray(iconVAO);
        if (iconVertexBuffer) driver->deleteBuffer(iconVertexBuffer);
    }

    kObject *kObject::getParent()
    {
        return parent;
    }

    void kObject::setParent(kObject *newParent)
    {
        parent = newParent;
        parent->children.push_back(this);
    }

    void kObject::detachFromParent()
    {
        if (parent == nullptr) return;
        auto &siblings = parent->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        parent = nullptr;
    }

    void kObject::setParentKeepTransform(kObject *newParent)
    {
        // Same parent — nothing to do, and we'd otherwise double-append to its
        // children list.
        if (newParent == parent) return;

        // Snapshot our current world TRS. calculateModelMatrix() refreshes our
        // worldTransform from local TRS using the (about-to-be-old) parent's
        // current worldTransform, which the renderer keeps up to date.
        calculateModelMatrix();
        kVec3 wPos = getGlobalPosition();
        kQuat wRot = getGlobalRotation();
        kVec3 wScl = getGlobalScale();

        // Swap the parent edge in the scene graph.
        detachFromParent();
        parent = newParent;
        if (newParent != nullptr)
            newParent->children.push_back(this);

        // Resolve the new parent's world TRS (identity when reparenting to
        // root space).
        kVec3 pPos(0.0f);
        kQuat pRot(1.0f, 0.0f, 0.0f, 0.0f);
        kVec3 pScl(1.0f);
        if (newParent != nullptr)
        {
            newParent->calculateModelMatrix();
            pPos = newParent->getGlobalPosition();
            pRot = newParent->getGlobalRotation();
            pScl = newParent->getGlobalScale();
        }

        // Solve for the local TRS that keeps world TRS constant under
        // world = parent_world * T*R*S.  Safe-divide guards against a parent
        // axis with zero scale (which would otherwise propagate NaNs).
        auto safeDiv = [](const kVec3 &a, const kVec3 &b) {
            return kVec3(
                b.x != 0.0f ? a.x / b.x : a.x,
                b.y != 0.0f ? a.y / b.y : a.y,
                b.z != 0.0f ? a.z / b.z : a.z);
        };

        kQuat invPRot = glm::inverse(pRot);
        kVec3 newLocalPos = invPRot * safeDiv(wPos - pPos, pScl);
        kQuat newLocalRot = glm::normalize(invPRot * wRot);
        kVec3 newLocalScl = safeDiv(wScl, pScl);

        setPosition(newLocalPos);
        setRotation(newLocalRot);
        setScale(newLocalScl);

        calculateModelMatrix();
    }

    std::vector<kObject *> kObject::getChildren()
    {
        return children;
    }

    std::vector<kScript>& kObject::getScripts()
    {
        return scripts;
    }

    void kObject::addScript(const kScript& script)
    {
        scripts.push_back(script);
    }

    void kObject::removeScript(const kString& uuid)
    {
        scripts.erase(
            std::remove_if(scripts.begin(), scripts.end(),
                [&uuid](const kScript& s) { return s.uuid == uuid; }),
            scripts.end());
    }

    // --- Particles -----------------------------------------------------------

    std::vector<kParticle>& kObject::getParticles()
    {
        return particles;
    }

    void kObject::addParticle(const kParticle& particle)
    {
        particles.push_back(particle);
    }

    void kObject::removeParticle(const kString& uuid)
    {
        particles.erase(
            std::remove_if(particles.begin(), particles.end(),
                [&uuid](const kParticle& p) { return p.uuid == uuid; }),
            particles.end());
    }

    // --- Audio sources -------------------------------------------------------

    std::vector<kAudioSource>& kObject::getAudioSources()
    {
        return audioSources;
    }

    void kObject::addAudioSource(const kAudioSource& source)
    {
        audioSources.push_back(source);
    }

    void kObject::removeAudioSource(const kString& uuid)
    {
        audioSources.erase(
            std::remove_if(audioSources.begin(), audioSources.end(),
                [&uuid](const kAudioSource& s) { return s.uuid == uuid; }),
            audioSources.end());
    }

    // --- Audio listener ------------------------------------------------------

    std::vector<kAudioListener>& kObject::getAudioListeners()
    {
        return audioListeners;
    }

    void kObject::addAudioListener(const kAudioListener& listener)
    {
        audioListeners.push_back(listener);
    }

    void kObject::removeAudioListener(const kString& uuid)
    {
        audioListeners.erase(
            std::remove_if(audioListeners.begin(), audioListeners.end(),
                [&uuid](const kAudioListener& l) { return l.uuid == uuid; }),
            audioListeners.end());
    }

    // --- Physics descriptor --------------------------------------------------

    bool kObject::getHasPhysicsDesc() const
    {
        return hasPhysicsDesc;
    }

    void kObject::setHasPhysicsDesc(bool val)
    {
        hasPhysicsDesc = val;
    }

    kPhysicsObjectDesc& kObject::getPhysicsDesc()
    {
        return physicsDesc;
    }

    bool kObject::getHasCharacterDesc() const
    {
        return hasCharacterDesc;
    }

    void kObject::setHasCharacterDesc(bool val)
    {
        hasCharacterDesc = val;
    }

    kCharacterControllerDesc& kObject::getCharacterDesc()
    {
        return characterDesc;
    }

    bool kObject::getHasNavMeshDesc() const
    {
        return hasNavMeshDesc;
    }

    void kObject::setHasNavMeshDesc(bool val)
    {
        hasNavMeshDesc = val;
    }

    kNavMeshDesc& kObject::getNavMeshDesc()
    {
        return navMeshDesc;
    }

    kNodeType kObject::getType()
    {
        return type;
    }

    void kObject::setType(kNodeType newType)
    {
        type = newType;
    }

    bool kObject::getActive()
    {
        return isActive;
    }

    void kObject::setActive(bool newActive)
    {
        isActive = newActive;
    }

    bool kObject::getStatic()
    {
        return isStatic;
    }

    void kObject::setStatic(bool newStatic)
    {
        isStatic = newStatic;
    }

    bool kObject::getDebugMode()
    {
        return debugMode;
    }

    void kObject::setDebugMode(bool newMode)
    {
        debugMode = newMode;
    }

    unsigned int kObject::getId()
    {
        return id;
    }

    void kObject::setId(unsigned int newId)
    {
        id = newId;
    }

    kString kObject::getUuid()
    {
        return uuid;
    }

    void kObject::setUuid(kString newUuid)
    {
        uuid = newUuid;
    }

    kString kObject::getName()
    {
        return name;
    }

    void kObject::setName(kString newName)
    {
        name = newName;
    }

    kString kObject::getPrefabRef() const
    {
        return prefabRef;
    }

    void kObject::setPrefabRef(const kString &ref)
    {
        prefabRef = ref;
    }

    kString kObject::getTemplateUuid() const
    {
        return templateUuid;
    }

    void kObject::setTemplateUuid(const kString &uuid)
    {
        templateUuid = uuid;
    }

    kVec3 kObject::getPosition()
    {
        return position;
    }

    void kObject::setPosition(kVec3 newPosition)
    {
        position = newPosition;
    }

    kQuat kObject::getRotation()
    {
        return rotation;
    }

    kVec3 kObject::getRotationEuler()
    {
        kVec3 eulerAngles = glm::eulerAngles(rotation);
        kVec3 eulerAnglesDegrees = glm::degrees(eulerAngles);

        return eulerAnglesDegrees;
    }

    void kObject::setRotation(kQuat newRotation)
    {
        rotation = glm::normalize(newRotation);
    }

    kVec3 kObject::getScale()
    {
        return scale;
    }

    void kObject::setScale(kVec3 newScale)
    {
        scale = newScale;
    }

    kVec3 kObject::calculateRight()
    {
        return glm::normalize(glm::cross(calculateUp(), calculateForward()));
    }

    kVec3 kObject::calculateForward()
    {
        kVec3 worldForwardAxis = kVec3(0.0f, 0.0f, -1.0f); // Default forward direction in OpenGL (negative Z axis)

        // Rotate forward vector by quaternion
        kVec3 front = getRotation() * worldForwardAxis;

        return glm::normalize(front);
    }

    kVec3 kObject::calculateUp()
    {
        kVec3 worldUpAxis = kVec3(0.0f, 1.0f, 0.0f);

        return getRotation() * worldUpAxis;
    }

    void kObject::rotate(kVec3 rotationAxis, float angularSpeed)
    {
        /*
        // Convert Euler angles from degrees to radians
        kVec3 eulerAnglesRadians = glm::radians(eulerAnglesDegrees);

        // Create a quaternion from the Euler angles
        kQuat newRotation(eulerAnglesRadians);

        if (newRotation.w < 0)
        {
            newRotation = -newRotation; // Ensure positive w component
        }

        // Combine the new rotation with the current rotation
        rotation = newRotation * getRotation(); // Apply new rotation relative to current rotation
        */

        // Compute the amount of rotation in radians
        float angle = angularSpeed;

        // Create a quaternion representing the small rotation
        kQuat deltaRotation = glm::angleAxis(angle, glm::normalize(rotationAxis));

        // Apply the incremental rotation
        setRotation(deltaRotation * getRotation());
    }

    kVec3 kObject::getGlobalPosition()
    {
        // return globalPosition;

        kVec3 globalPos = kVec3(worldTransform[3]);

        return globalPos;
    }

    kQuat kObject::getGlobalRotation()
    {
        // return glm::normalize(globalRotation);

        // Extract global scale
        kVec3 globalSc = kVec3(
            glm::length(kVec3(worldTransform[0])), // X axis scale
            glm::length(kVec3(worldTransform[1])), // Y axis scale
            glm::length(kVec3(worldTransform[2]))  // Z axis scale
        );

        // Normalize rotation matrix by removing scaling
        kMat3 rotationMat = kMat3(
            kVec3(worldTransform[0]) / globalSc.x,
            kVec3(worldTransform[1]) / globalSc.y,
            kVec3(worldTransform[2]) / globalSc.z);

        // Convert to quaternion
        kQuat globalRot = glm::normalize(glm::quat_cast(rotationMat));

        return globalRot;
    }

    kVec3 kObject::getGlobalScale()
    {
        // return globalScale;

        // Extract global scale
        kVec3 globalSc = kVec3(
            glm::length(kVec3(worldTransform[0])), // X axis scale
            glm::length(kVec3(worldTransform[1])), // Y axis scale
            glm::length(kVec3(worldTransform[2]))  // Z axis scale
        );

        return globalSc;
    }

    void kObject::setMaterial(kMaterial *newMaterial, bool setChildren)
    {
        material = newMaterial;

        if (setChildren)
        {
            if (getChildren().size() > 0)
            {
                for (size_t i = 0; i < getChildren().size(); ++i)
                {
                    getChildren().at(i)->setMaterial(newMaterial);
                }
            }
        }
    }

    kMaterial *kObject::getMaterial()
    {
        return material;
    }

    void kObject::calculateModelMatrix()
    {
        // Local transformations
        kMat4 trans = glm::translate(kMat4(1.0), getPosition());
        kQuat quat = kQuat(getRotation());
        kMat4 rot = glm::toMat4(quat);
        kMat4 scale = glm::scale(kMat4(1.0), getScale());

        localTransform = trans * rot * scale;

        // Parent global transform
        if (parent != nullptr)
        {
            // Combine parent transformations
            worldTransform = parent->getModelMatrixWorld() * localTransform;
        }
        else
        {
            worldTransform = localTransform;
        }
    }

    kMat4 kObject::getModelMatrixWorld()
    {
        return worldTransform;
    }

    kMat4 kObject::getModelMatrixLocal()
    {
        return localTransform;
    }

    void kObject::draw()
    {
        if (material == nullptr) return;

        kDriver *driver = kDriver::getCurrent();
        if (driver == nullptr) return;

        // Lazy-init icon VAO/VBO
        if (iconVAO == 0)
        {
            iconVAO = driver->createVertexArray();
            driver->bindVertexArray(iconVAO);
            iconVertexBuffer = driver->createBuffer();
            driver->uploadVertexBuffer(iconVertexBuffer, iconVertices, sizeof(iconVertices));
            driver->setVertexAttribFloat(0, 3, 0, 0);
            driver->unbindVertexArray();
        }

        driver->drawArrays(iconVAO, kPrimitiveType::TRIANGLE_STRIP, 4);
    }

    json kObject::serialize()
    {
        json childrenData = json::array();
        if (getChildren().size() > 0)
        {
            for (size_t i = 0; i < getChildren().size(); ++i)
            {
                // Make sure UUID is not empty (means it's added by engine and children from import)
                if (!getChildren().at(i)->getUuid().empty())
                    childrenData.push_back(getChildren().at(i)->serialize());
            }
        }

        json scriptsData = json::array();
        if (getScripts().size() > 0)
        {
            for (size_t j = 0; j < getScripts().size(); ++j)
            {
                const kScript &sc = getScripts().at(j);
                scriptsData.push_back({
                    {"uuid",        sc.uuid},       // component (attachment) UUID
                    {"script_uuid", sc.scriptUuid}, // referenced script asset UUID
                    {"file_name",   sc.fileName},   // source path (fallback / editor)
                    {"checksum",    sc.checksum},   // source checksum at last compile
                    {"active",      sc.isActive},
                });
            }
        }

        json data =
            {
                {"type", "object"},
                {"uuid", getUuid()},
                {"name", getName()},
                {"active", getActive()},
                {"position",
                 {{"x", getPosition().x},
                  {"y", getPosition().y},
                  {"z", getPosition().z}}},
                {"rotation",
                 {{"x", getRotationEuler().x},
                  {"y", getRotationEuler().y},
                  {"z", getRotationEuler().z}}},
                {"scale",
                 {{"x", getScale().x},
                  {"y", getScale().y},
                  {"z", getScale().z}}},
                {"children", childrenData},
                {"script", scriptsData},
            };

        // Prefab linkage — only emit when set so unrelated objects stay unchanged.
        if (!prefabRef.empty())    data["prefab_ref"]    = prefabRef;
        if (!templateUuid.empty()) data["template_uuid"] = templateUuid;

        // Assigned material asset — only emit when set. The editor re-applies
        // the material from this UUID on load (see Manager::loadObjectFromJson).
        if (!materialUuid.empty()) data["material_uuid"] = materialUuid;

        // Physics body descriptor — only emitted when the object opted in.
        // The fields mirror kPhysicsObjectDesc; readers should populate the
        // descriptor and call setHasPhysicsDesc(true).
        if (hasPhysicsDesc)
        {
            json phys =
            {
                {"shape_type",      (int)physicsDesc.shape.type},
                {"half_extents",
                 {{"x", physicsDesc.shape.halfExtents.x},
                  {"y", physicsDesc.shape.halfExtents.y},
                  {"z", physicsDesc.shape.halfExtents.z}}},
                {"radius",          physicsDesc.shape.radius},
                {"height",          physicsDesc.shape.height},
                {"body_type",       (int)physicsDesc.type},
                {"mass",            physicsDesc.mass},
                {"friction",        physicsDesc.friction},
                {"restitution",     physicsDesc.restitution},
                {"linear_damping",  physicsDesc.linearDamping},
                {"angular_damping", physicsDesc.angularDamping},
                {"gravity_factor",  physicsDesc.gravityFactor},
            };
            data["physics"] = phys;
        }

        // Character controller descriptor — only emitted when opted in.
        if (hasCharacterDesc)
        {
            data["character"] =
            {
                {"radius",         characterDesc.radius},
                {"height",         characterDesc.height},
                {"mass",           characterDesc.mass},
                {"friction",       characterDesc.friction},
                {"gravity_factor", characterDesc.gravityFactor},
                {"slope_limit",    characterDesc.slopeLimit},
                {"step_height",    characterDesc.stepHeight},
            };
        }

        // Navigation surface descriptor — bake settings only; the baked mesh is
        // regenerated on demand and never serialised.
        if (hasNavMeshDesc)
        {
            const kNavBuildConfig &c = navMeshDesc.config;
            data["navmesh_surface"] =
            {
                {"use_area",   navMeshDesc.useArea},
                {"area_size",  {{"x", navMeshDesc.areaSize.x},
                                {"y", navMeshDesc.areaSize.y},
                                {"z", navMeshDesc.areaSize.z}}},
                {"cell_size",        c.cellSize},
                {"cell_height",      c.cellHeight},
                {"agent_height",     c.agentHeight},
                {"agent_radius",     c.agentRadius},
                {"agent_max_climb",  c.agentMaxClimb},
                {"agent_max_slope",  c.agentMaxSlope},
                {"tile_size",        c.tileSize},
            };
        }

        return data;
    }

    void kObject::deserialize(json data)
    {
    }

    // --- Physics -------------------------------------------------------------

    void kObject::attachPhysics(kPhysicsObject *physicsObj)
    {
        physicsObject = physicsObj;
    }

    void kObject::detachPhysics()
    {
        physicsObject = nullptr;
    }

    kPhysicsObject *kObject::getPhysicsObject()
    {
        return physicsObject;
    }

    void kObject::syncFromPhysics()
    {
        if (!physicsObject) return;

        position = physicsObject->getPosition();
        rotation = physicsObject->getRotation();
    }

    // --- Character controller -------------------------------------------------

    void kObject::attachCharacter(kCharacterController *character)
    {
        characterController = character;
    }

    void kObject::detachCharacter()
    {
        characterController = nullptr;
    }

    kCharacterController *kObject::getCharacterController()
    {
        return characterController;
    }

    void kObject::syncFromCharacter()
    {
        if (!characterController) return;

        position = characterController->getPosition();
        rotation = characterController->getRotation();
    }
}
