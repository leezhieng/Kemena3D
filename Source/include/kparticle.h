/**
 * @file kparticle.h
 * @brief Particle system component and manager.
 */

#ifndef KPARTICLE_H
#define KPARTICLE_H

#include "kexport.h"
#include "kdatatype.h"

#include <vector>
#include <random>

namespace kemena
{
    // Forward declarations
    class kDriver;

    // =========================================================================
    // kParticle — Editor-side descriptor for a particle system component
    // =========================================================================

    /**
     * @brief Data descriptor for a single particle system attached to a scene object.
     *
     * Store instances in kObject via addParticle() / removeParticle().
     * At game-start the runtime reads these descriptors and spawns live emitters.
     */
    struct KEMENA3D_API kParticle
    {
        kString uuid;                         ///< Unique identifier for this particle system.
        kString name     = "Particle System"; ///< Human-readable display name.
        bool    isActive = true;              ///< Whether this system emits/simulates particles.
        bool    looping  = true;              ///< Whether emission restarts after one cycle.

        // Emitter
        int   maxParticles  = 100;      ///< Maximum number of live particles at once.
        float emissionRate  = 10.0f;    ///< Particles spawned per second.
        float lifetime      = 2.0f;     ///< Each particle's lifespan in seconds.
        float gravityScale  = 1.0f;     ///< Multiplier applied to gravity acting on particles.

        // Velocity
        kVec3 startVelocity    = kVec3(0.0f, 1.0f, 0.0f); ///< Base initial velocity direction/magnitude.
        float startSpeed       = 1.0f;                     ///< Scalar speed applied to the start velocity.
        kVec3 velocityVariance = kVec3(0.1f, 0.1f, 0.1f);  ///< Per-axis random spread added to start velocity.

        // Visual
        kVec4 colorStart = kVec4(1.0f, 1.0f, 1.0f, 1.0f); ///< Particle color (RGBA) at birth.
        kVec4 colorEnd   = kVec4(1.0f, 1.0f, 1.0f, 0.0f); ///< Particle color (RGBA) at death.
        float sizeStart  = 0.1f;                           ///< Particle size at birth.
        float sizeEnd    = 0.0f;                           ///< Particle size at death.

        // Emission shape
        enum EmissionShape
        {
            SHAPE_POINT,  ///< Emit from a single point.
            SHAPE_SPHERE, ///< Emit from within a sphere volume.
            SHAPE_CONE,   ///< Emit from within a cone volume.
            SHAPE_BOX     ///< Emit from within a box volume.
        };
        EmissionShape emissionShape = SHAPE_POINT; ///< Shape of the emission volume.
        kVec3 shapeSize = kVec3(0.5f, 0.5f, 0.5f); ///< Half-extents or radius of the emission shape.

        // Billboard texture
        kString texturePath; ///< Optional path to a sprite texture; empty = flat-shaded quad.
    };

    // =========================================================================
    // kParticleManager — Runtime particle simulation and rendering
    // =========================================================================

    /**
     * @brief Manages active particle emitters at runtime.
     *
     * Create one per world. Register particle descriptors with
     * addEmitter() at game-start, then call update() every frame to
     * advance simulation, and render() to draw all live particles.
     *
     * Particles are rendered as instanced camera-facing billboard quads
     * using a built-in GPU shader. No external shader setup is required.
     */
    class KEMENA3D_API kParticleManager
    {
    public:
        /** @brief Constructs an empty particle manager. */
        kParticleManager();

        /** @brief Destroys the manager and all GPU resources. */
        ~kParticleManager();

        // --- Lifecycle --------------------------------------------------------

        /**
         * @brief Initialises GPU resources (shader, billboard VAO, instance VBO).
         *
         * Must be called after a kDriver is current. Safe to call multiple times
         * (no-op after the first successful init).
         * @return true on success.
         */
        bool init();

        /** @brief Releases all GPU resources held by the manager. */
        void destroy();

        /**
         * @brief Advances all active emitters by @p dt seconds.
         *
         * Spawns new particles according to each emitter's rate, integrates
         * velocities, applies gravity, and kills expired particles.
         * @param dt Frame delta-time in seconds.
         */
        void update(float dt);

        /**
         * @brief Renders all live particles as instanced billboards.
         *
         * Requires a valid view/projection transform from the world's main camera.
         * Blending and depth-write state are managed internally.
         * @param viewMatrix       Camera view matrix.
         * @param projectionMatrix Camera projection matrix.
         * @param cameraRight      Camera's world-space right vector (for billboarding).
         * @param cameraUp         Camera's world-space up vector (for billboarding).
         */
        void render(const kMat4 &viewMatrix,
                    const kMat4 &projectionMatrix,
                    const kVec3 &cameraRight,
                    const kVec3 &cameraUp);

        // --- Emitter registration ---------------------------------------------

        /**
         * @brief Registers a particle descriptor for simulation.
         *
         * The descriptor is copied internally; the caller retains ownership of
         * the original.
         * @param particle Descriptor to register.
         * @param worldPosition World-space position of the owning object (used as emitter origin).
         */
        void addEmitter(const kParticle &particle, const kVec3 &worldPosition);

        /**
         * @brief Removes a particle descriptor from simulation by UUID.
         * @param uuid UUID of the kParticle to remove.
         */
        void removeEmitter(const kString &uuid);

        /** @brief Removes all registered emitters and their live particles. */
        void clear();

        /**
         * @brief Updates the world-space position of an existing emitter.
         * @param uuid UUID of the emitter to update.
         * @param worldPosition New world-space position.
         */
        void setEmitterPosition(const kString &uuid, const kVec3 &worldPosition);

        /** @brief Returns the total number of live particles across all emitters. */
        int getLiveParticleCount() const;

        /** @brief Returns true if the GPU resources have been initialised. */
        bool isInitialized() const { return initialized; }

    private:
        // =====================================================================
        // Internal runtime structures
        // =====================================================================

        /** @brief A single live particle on the CPU side. */
        struct Particle
        {
            kVec3 position;   ///< World-space position.
            kVec3 velocity;   ///< Current velocity (units/sec).
            float life;       ///< Remaining lifetime in seconds.
            float maxLife;    ///< Total lifetime at spawn (for interpolation).
            kVec4 colorStart; ///< RGBA at birth.
            kVec4 colorEnd;   ///< RGBA at death.
            float sizeStart;  ///< Size at birth.
            float sizeEnd;    ///< Size at death.
        };

        /** @brief Per-instance data uploaded to the GPU each frame. */
        struct ParticleInstanceData
        {
            float posX, posY, posZ; ///< World-space position (3 floats, 12 bytes).
            float size;             ///< Current size (1 float, 4 bytes).
            float r, g, b, a;       ///< Current colour (4 floats, 16 bytes).
        };

        static_assert(sizeof(ParticleInstanceData) == 32,
                      "ParticleInstanceData must be tightly packed (32 bytes)");

        /** @brief Internal runtime emitter tracking one kParticle descriptor. */
        struct Emitter
        {
            kParticle desc;                    ///< Copy of the descriptor.
            kVec3     worldPosition;           ///< World-space emitter origin.
            float     emissionAccum = 0.0f;    ///< Fractional particle accumulator for rate.
            std::vector<Particle> particles;   ///< Live particles owned by this emitter.

            // Random engine (seeded per emitter for determinism)
            std::mt19937                          rng;
            std::uniform_real_distribution<float> dist01;
        };

        // =====================================================================
        // Private helpers
        // =====================================================================

        /** @brief Spawns a single particle from the given emitter. */
        void spawnParticle(Emitter &emitter);

        /** @brief Builds/updates the instanced instance-data VBO for the current frame. */
        void uploadInstanceData();

        /** @brief Compiles the built-in particle shader. */
        bool compileShader();

        // =====================================================================
        // GPU resources
        // =====================================================================

        bool initialized = false; ///< True after a successful init().

        // Shader
        uint32_t shaderProgram = 0; ///< Compiled particle shader program.

        // Billboard quad (single quad, instanced N times)
        uint32_t quadVao = 0;  ///< VAO for the billboard quad.
        uint32_t quadVbo = 0;  ///< VBO with 4 corner vertices of the quad.
        uint32_t quadEbo = 0;  ///< EBO with 2 triangles (6 indices).

        // Instance data buffer (streamed every frame)
        uint32_t instanceVbo = 0;      ///< VBO for per-instance ParticleInstanceData.
        size_t   instanceCapacity = 0; ///< Allocated capacity of instanceVbo (in ParticleInstanceData elements).

        // =====================================================================
        // Emitter registry
        // =====================================================================

        std::vector<Emitter> emitters; ///< All registered emitters (descriptors + live particles).
    };

} // namespace kemena

#endif // KPARTICLE_H
