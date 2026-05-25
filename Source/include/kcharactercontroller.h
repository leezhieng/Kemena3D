/**
 * @file kcharactercontroller.h
 * @brief Capsule character controller built on Jolt's JPH::Character.
 *
 * A character controller is configured in the editor (radius/height/slope/etc.)
 * but, like rigid bodies, is only instantiated and simulated once the game is
 * playing. Movement is driven by gameplay code via setLinearVelocity().
 */

#ifndef KCHARACTERCONTROLLER_H
#define KCHARACTERCONTROLLER_H

#include "kexport.h"
#include "kdatatype.h"

namespace kemena
{
    /**
     * @brief Editor-authored description of a character controller.
     *
     * The capsule's origin is at the object's feet (the collision capsule is
     * offset upward by height/2), matching how character controllers are placed
     * in most engines.
     */
    struct KEMENA3D_API kCharacterControllerDesc
    {
        float radius        = 0.3f;  ///< Capsule radius.
        float height        = 1.8f;  ///< Total capsule height (feet to head).
        float mass          = 80.0f; ///< Body mass in kg.
        float friction      = 0.5f;  ///< Surface friction.
        float gravityFactor = 1.0f;  ///< Multiplier on world gravity.
        float slopeLimit    = 45.0f; ///< Maximum walkable slope, in degrees.
        float stepHeight     = 0.3f; ///< Reserved for step-up handling.

        kVec3 position = kVec3(0.0f, 0.0f, 0.0f);
        kQuat rotation = kQuat(1.0f, 0.0f, 0.0f, 0.0f);
    };

    /**
     * @brief Runtime wrapper around a Jolt character.
     *
     * Created by kPhysicsManager::createCharacter(); never instantiated directly
     * by editor code. Call update() once per physics step (kPhysicsManager does
     * this) and read getPosition() to drive the scene node.
     */
    class KEMENA3D_API kCharacterController
    {
    public:
        kCharacterController();
        ~kCharacterController();

        /**
         * @brief Initialises the Jolt character.
         * @param physicsSystem Opaque JPH::PhysicsSystem* from kPhysicsManager.
         * @param desc          Capsule + motion parameters.
         * @return true on success.
         */
        bool init(void *physicsSystem, const kCharacterControllerDesc &desc);

        /** @brief Removes the character from the physics system. */
        void uninit();

        /**
         * @brief Per-step maintenance (Jolt PostSimulation). Called by
         *        kPhysicsManager::update() after the world steps.
         */
        void update(float deltaTime);

        kVec3 getPosition() const;
        void  setPosition(const kVec3 &position);
        kQuat getRotation() const;
        void  setRotation(const kQuat &rotation);

        /** @brief Sets the character's world-space velocity (m/s). */
        void  setLinearVelocity(const kVec3 &velocity);
        kVec3 getLinearVelocity() const;

        /** @brief True when the character is standing on walkable ground. */
        bool  isOnGround() const;

    private:
        struct Impl;
        Impl *m_impl;
    };
}

#endif // KCHARACTERCONTROLLER_H
