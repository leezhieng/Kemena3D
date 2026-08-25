#include "kcamera.h"

namespace kemena
{
    namespace
    {
        // Refresh the world transform for every ancestor of @p node, root-first,
        // so getGlobalPosition()/getGlobalRotation() reflect the full parent chain.
        // Cheap (a handful of 4x4 multiplies) and idempotent — safe to call from
        // the render hot path.
        void refreshWorldTransforms(kObject *node)
        {
            if (node == nullptr)
                return;
            if (node->getParent() != nullptr)
                refreshWorldTransforms(node->getParent());
            node->calculateModelMatrix();
        }
    }

    kCamera::kCamera(kObject *parentNode, kCameraType type)
    {
        if (parentNode != nullptr)
            setParent(parentNode);
        setType(kNodeType::NODE_TYPE_CAMERA);

        setCameraType(type);
    }

    void kCamera::setCameraType(kCameraType newType)
    {
        cameraType = newType;

        //setLookAt(getLookAt());
    }

    kCameraType kCamera::getCameraType()
    {
        return cameraType;
    }

    void kCamera::setLookAt(kVec3 newLookAt)
    {
        // Look at is always the front of the camera, otherwise gizmo and icons will display at the wrong position
        if (cameraType == kCameraType::CAMERA_TYPE_FREE)
        {
            // Resolve the camera's world-space origin so the look direction is
            // computed against the parent chain, not just the local transform.
            refreshWorldTransforms(this);
            kVec3 worldPos = getGlobalPosition();

            kVec3 forward = newLookAt - worldPos;
            float len = glm::length(forward);
            if (len < 1e-6f)
                return; // target coincides with the camera — keep current orientation
            forward /= len;

            kVec3 defaultForward(0.0f, 0.0f, -1.0f);
            kQuat desiredWorldRot = glm::rotation(defaultForward, forward);

            // World orientation = parent's world rotation * local rotation, so
            // solve for the local rotation that yields the desired world look.
            kQuat parentWorldRot(1.0f, 0.0f, 0.0f, 0.0f);
            if (getParent() != nullptr)
                parentWorldRot = getParent()->getGlobalRotation();

            setRotation(glm::normalize(glm::inverse(parentWorldRot) * desiredWorldRot));
        }
        else
            lookAt = newLookAt;
    }

    kVec3 kCamera::getLookAt()
    {
        // Free camera will always return the forward direction as lookAt

        if (cameraType == kCameraType::CAMERA_TYPE_LOCKED)
            return lookAt;
        else if (cameraType == kCameraType::CAMERA_TYPE_FREE)
        {
            refreshWorldTransforms(this);
            return getGlobalPosition() + (getGlobalRotation() * kVec3(0.0f, 0.0f, -1.0f));
        }

        return lookAt;
    }

    void kCamera::setFOV(float newFOV)
    {
        fov = newFOV;
    }

    float kCamera::getFOV()
    {
        return fov;
    }

    void kCamera::setNearClip(float newNearClip)
    {
        nearClip = newNearClip;
    }

    float kCamera::getNearClip()
    {
        return nearClip;
    }

    void kCamera::setFarClip(float newFarClip)
    {
        farClip = newFarClip;
    }

    float kCamera::getFarClip()
    {
        return farClip;
    }

    void kCamera::setAspectRatio(float newAspecRatio)
    {
        aspectRatio = newAspecRatio;
    }

    float kCamera::getAspectRatio()
    {
        return aspectRatio;
    }

    kMat4 kCamera::calculateMVP(kMesh *mesh)
    {
        // Build the view from the camera's world transform so a camera nested
        // under a moving/rotating parent follows it.
        refreshWorldTransforms(this);

        kMat4 model = mesh->getModelMatrixWorld();

        kVec3 eye     = getGlobalPosition();
        kQuat worldRot = getGlobalRotation();
        kVec3 up       = worldRot * kVec3(0.0f, 1.0f, 0.0f);

        kVec3 center = (cameraType == kCameraType::CAMERA_TYPE_FREE)
                           ? eye + (worldRot * kVec3(0.0f, 0.0f, -1.0f))
                           : lookAt;

        kMat4 view       = glm::lookAt(eye, center, up);
        kMat4 projection = glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip);

        return projection * view * model;
    }

    kMat4 kCamera::getViewMatrix()
    {
        // The view matrix must be derived from the camera's world-space pose.
        // A camera parented under another object inherits that object's
        // translation/rotation/scale, so using only the local transform here
        // would make the camera ignore its parent.
        refreshWorldTransforms(this);

        kMat4 view;

        kVec3 eye     = getGlobalPosition();
        kQuat worldRot = getGlobalRotation();
        kVec3 up       = worldRot * kVec3(0.0f, 1.0f, 0.0f);

        if (cameraType == kCameraType::CAMERA_TYPE_FREE)
            view = glm::lookAt(eye, eye + (worldRot * kVec3(0.0f, 0.0f, -1.0f)), up);
        else if (cameraType == kCameraType::CAMERA_TYPE_LOCKED)
            view = glm::lookAt(eye, lookAt, up);

        return view;
    }

    kMat4 kCamera::getProjectionMatrix()
    {
        kMat4 projection = glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip);
        return projection;
    }

    void kCamera::rotateByMouse(kQuat rotation, float deltaX, float deltaY, float sensitivity, float pitchLimit)
    {
        float yawAngle = -deltaX * sensitivity;
        float pitchAngle = -deltaY * sensitivity;

        // Clamp pitch to prevent flipping
        const float pitchLimitRadians = glm::radians(89.0f);
        pitchAngle = glm::clamp(pitchAngle, -pitchLimitRadians, pitchLimitRadians);

        // Step 1: Yaw - rotate around global Y axis
        kQuat qYaw = glm::angleAxis(yawAngle, kVec3(0, 1, 0));
        kQuat yawedRotation = qYaw * rotation;

        // Step 2: Pitch - rotate around local right axis (after yaw)
        kVec3 right = yawedRotation * kVec3(1, 0, 0);
        kQuat qPitch = glm::angleAxis(pitchAngle, right);

        kQuat finalRotation = qPitch * yawedRotation;

        setRotation(finalRotation);
    }

    void kCamera::screenToRay(float mouseX, float mouseY,
                              float viewWidth, float viewHeight,
                              kVec3 &outOrigin, kVec3 &outDirection)
    {
        // Convert pixel coordinate to NDC [-1, 1].
        // Screen Y is top-down; NDC Y is bottom-up, so flip.
        float ndcX = (2.0f * mouseX / viewWidth)  - 1.0f;
        float ndcY =  1.0f - (2.0f * mouseY / viewHeight);

        kMat4 invVP = glm::inverse(getProjectionMatrix() * getViewMatrix());

        kVec4 nearClip = invVP * kVec4(ndcX, ndcY, -1.0f, 1.0f);
        kVec4 farClip  = invVP * kVec4(ndcX, ndcY,  1.0f, 1.0f);

        nearClip /= nearClip.w;
        farClip  /= farClip.w;

        outOrigin    = kVec3(nearClip);
        outDirection = glm::normalize(kVec3(farClip) - kVec3(nearClip));
    }

    void kCamera::setPosition(kVec3 newPosition)
    {
        kObject::setPosition(newPosition);
    }

    void kCamera::setRotation(kQuat newRotation)
    {
        kObject::setRotation(newRotation);
    }

    json kCamera::serialize()
    {
        kString typeDisplay = "unknown";
        if (getCameraType() == kCameraType::CAMERA_TYPE_FREE)
            typeDisplay = "free";
        else if (getCameraType() == kCameraType::CAMERA_TYPE_LOCKED)
            typeDisplay = "locked";

        // Delegate to the base so transform, children, scripts, physics,
        // character and navigation components serialize consistently, then add
        // the camera-specific fields.
        json data = kObject::serialize();
        data["type"]         = "camera";
        data["camera_type"]  = typeDisplay;
        data["look_at"]      = {{"x", getLookAt().x}, {"y", getLookAt().y}, {"z", getLookAt().z}};
        data["up_axis"]      = {{"x", calculateUp().x}, {"y", calculateUp().y}, {"z", calculateUp().z}};
        data["fov"]          = getFOV();
        data["near_clip"]    = getNearClip();
        data["far_clip"]     = getFarClip();
        data["aspect_ratio"] = getAspectRatio();
        data["scene_uuid"]   = sceneUuid;
        return data;
    }

    void kCamera::deserialize(json data)
    {
    }
}
