/**
 * @file kparticle.h
 * @brief Particle system component and manager.
 */

#ifndef KPARTICLE_H
#define KPARTICLE_H

#include "kexport.h"
#include "kdatatype.h"

#include <vector>

namespace kemena
{
    /**
     * @brief Data descriptor for a single particle system attached to a scene object.
     *
     * Store instances in kObject via addParticle() / removeParticle().
     * At game-start the runtime reads these descriptors and spawns live emitters.
     */
    struct KEMENA3D_API kParticle
    {
        kString uuid;                       ///< Unique identifier for this particle system.
        kString name     = "Particle System"; ///< Human-readable display name.
        bool    isActive = true;            ///< Whether this system emits/simulates particles.
        bool    looping  = true;            ///< Whether emission restarts after one cycle.

        // Emitter
        int   maxParticles = 100;           ///< Maximum number of live particles at once.
        float emissionRate = 10.0f;  ///< Particles spawned per second.
        float lifetime     = 2.0f;   ///< Each particle's lifespan in seconds.
        float gravityScale = 1.0f;          ///< Multiplier applied to gravity acting on particles.

        // Velocity
        kVec3 startVelocity    = kVec3(0.0f, 1.0f, 0.0f); ///< Base initial velocity direction/magnitude.
        float startSpeed       = 1.0f;                    ///< Scalar speed applied to the start velocity.
        kVec3 velocityVariance = kVec3(0.1f, 0.1f, 0.1f); ///< Per-axis random spread added to start velocity.

        // Visual
        kVec4 colorStart = kVec4(1.0f, 1.0f, 1.0f, 1.0f); ///< Particle color (RGBA) at birth.
        kVec4 colorEnd   = kVec4(1.0f, 1.0f, 1.0f, 0.0f); ///< Particle color (RGBA) at death.
        float sizeStart  = 0.1f;                          ///< Particle size at birth.
        float sizeEnd    = 0.0f;                          ///< Particle size at death.
    };

    /**
     * @brief Manages active particle emitters at runtime.
     *
     * Create one per scene/world.  Register particle descriptors with
     * addEmitter() at game-start, then call update() every frame.
     */
    class KEMENA3D_API kParticleManager
    {
    public:
        /** @brief Constructs an empty particle manager with no registered emitters. */
        kParticleManager()  = default;

        /** @brief Destroys the manager; does not own the registered descriptors. */
        ~kParticleManager() = default;

        /**
         * @brief Advances all active emitters by @p dt seconds.
         * @param dt Frame delta-time in seconds.
         */
        void update(float dt);

        /**
         * @brief Registers a particle descriptor for simulation.
         * @param particle Descriptor to track (caller retains ownership).
         */
        void addEmitter(kParticle *particle);

        /**
         * @brief Removes a particle descriptor from simulation by UUID.
         * @param uuid UUID of the kParticle to remove.
         */
        void removeEmitter(const kString &uuid);

    private:
        std::vector<kParticle *> emitters;
    };

} // namespace kemena

#endif // KPARTICLE_H
