#include "kparticle.h"
#include "kdriver.h"

#include <algorithm>
#include <cstring>
#include <chrono>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace kemena
{
    // =========================================================================
    // Built-in particle shader sources (GLSL 330 core)
    // =========================================================================

    static const char *kParticleVS = R"(
#version 330 core

layout(location = 0) in vec2 aCorner;        // billboard quad corner {-0.5,-0.5 .. +0.5,+0.5}

// Per-instance attributes
layout(location = 1) in vec3 aInstancePos;    // world-space particle position
layout(location = 2) in float aInstanceSize;  // particle size
layout(location = 3) in vec4 aInstanceColor;  // particle colour (RGBA)

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;

out vec2 vTexCoord;
out vec4 vColor;

void main()
{
    // Billboard: offset the quad corner in world space by camera right/up,
    // scaled by the particle's size.
    vec3 worldPos = aInstancePos
                  + uCameraRight * aCorner.x * aInstanceSize
                  + uCameraUp    * aCorner.y * aInstanceSize;

    gl_Position = uProjection * uView * vec4(worldPos, 1.0);

    // Map corner {-0.5,-0.5 .. +0.5,+0.5} → UV {0,0 .. 1,1}
    vTexCoord = aCorner + vec2(0.5);
    vColor    = aInstanceColor;
}
)";

    static const char *kParticleFS = R"(
#version 330 core

in vec2 vTexCoord;
in vec4 vColor;

out vec4 fragColor;

void main()
{
    // Soft circular falloff — discards corners of the quad so it looks round.
    vec2  center = vTexCoord - vec2(0.5);
    float dist   = length(center);
    float alpha  = 1.0 - smoothstep(0.4, 0.5, dist);
    if (alpha < 0.01)
        discard;

    fragColor = vec4(vColor.rgb, vColor.a * alpha);
}
)";

    // =========================================================================
    // kParticleManager implementation
    // =========================================================================

    kParticleManager::kParticleManager()
    {
    }

    kParticleManager::~kParticleManager()
    {
        destroy();
    }

    bool kParticleManager::init()
    {
        if (initialized)
            return true;

        kDriver *driver = kDriver::getCurrent();
        if (!driver)
            return false;

        // --- Compile the particle shader -------------------------------------
        if (!compileShader())
            return false;

        // --- Billboard quad geometry (single quad, instanced N times) --------
        // Two triangles forming a unit square centred at origin.
        float corners[] = {
            -0.5f, -0.5f,  // bottom-left
             0.5f, -0.5f,  // bottom-right
            -0.5f,  0.5f,  // top-left
             0.5f,  0.5f,  // top-right
        };
        uint32_t indices[] = { 0, 1, 2, 2, 1, 3 };

        quadVao = driver->createVertexArray();
        driver->bindVertexArray(quadVao);

        quadVbo = driver->createBuffer();
        driver->uploadVertexBuffer(quadVbo, corners, sizeof(corners));
        driver->setVertexAttribFloat(0, 2, 2 * sizeof(float), 0); // aCorner

        quadEbo = driver->createBuffer();
        driver->uploadIndexBuffer(quadEbo, indices, sizeof(indices));

        // --- Instance data VBO (streaming, allocated on first upload) --------
        instanceVbo = driver->createBuffer();
        instanceCapacity = 0;

        // --- Set up per-instance attributes on the VAO -----------------------
        // Instance attributes use divisor=1 so they advance per-instance, not
        // per-vertex. The instance VBO is bound and attribute pointers are set;
        // actual data is streamed each frame via updateBufferSubData.
        {
            // Allocate initial storage for the instance VBO so it can be bound
            // to the VAO. The real capacity grows on first uploadInstanceData().
            const size_t initialBytes = 256 * sizeof(ParticleInstanceData);
            driver->uploadVertexBuffer(instanceVbo, nullptr, initialBytes);
            instanceCapacity = 256;

            // aInstancePos  (location=1): 3 floats, offset 0
            driver->setVertexAttribFloat(1, 3, sizeof(ParticleInstanceData), 0);
            driver->setVertexAttribDivisor(1, 1);
            // aInstanceSize (location=2): 1 float,  offset 12
            driver->setVertexAttribFloat(2, 1, sizeof(ParticleInstanceData), sizeof(float) * 3);
            driver->setVertexAttribDivisor(2, 1);
            // aInstanceColor(location=3): 4 floats, offset 16
            driver->setVertexAttribFloat(3, 4, sizeof(ParticleInstanceData), sizeof(float) * 4);
            driver->setVertexAttribDivisor(3, 1);
        }

        driver->unbindVertexArray();

        initialized = true;
        return true;
    }

    void kParticleManager::destroy()
    {
        kDriver *driver = kDriver::getCurrent();
        if (!driver)
            return;

        if (shaderProgram) { driver->deleteShaderProgram(shaderProgram); shaderProgram = 0; }
        if (quadVao)       { driver->deleteVertexArray(quadVao);      quadVao = 0; }
        if (quadVbo)       { driver->deleteBuffer(quadVbo);           quadVbo = 0; }
        if (quadEbo)       { driver->deleteBuffer(quadEbo);           quadEbo = 0; }
        if (instanceVbo)   { driver->deleteBuffer(instanceVbo);       instanceVbo = 0; }

        instanceCapacity = 0;
        emitters.clear();
        initialized = false;
    }

    bool kParticleManager::compileShader()
    {
        kDriver *driver = kDriver::getCurrent();
        if (!driver)
            return false;

        shaderProgram = driver->compileShaderProgram(kParticleVS, kParticleFS);
        return shaderProgram != 0;
    }

    // -------------------------------------------------------------------------
    // Emitter registration
    // -------------------------------------------------------------------------

    void kParticleManager::addEmitter(const kParticle &particle, const kVec3 &worldPosition)
    {
        Emitter e;
        e.desc          = particle;
        e.worldPosition = worldPosition;
        e.emissionAccum = 0.0f;

        // Seed the RNG from the current time + a hash of the UUID so each
        // emitter produces a different sequence.
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        uint64_t seed = static_cast<uint64_t>(now);
        // Mix in a simple hash of the UUID string
        for (char c : particle.uuid)
            seed = seed * 31 + static_cast<uint64_t>(c);
        e.rng    = std::mt19937(static_cast<unsigned int>(seed));
        e.dist01 = std::uniform_real_distribution<float>(0.0f, 1.0f);

        emitters.push_back(std::move(e));
    }

    void kParticleManager::removeEmitter(const kString &uuid)
    {
        emitters.erase(
            std::remove_if(emitters.begin(), emitters.end(),
                [&uuid](const Emitter &e) { return e.desc.uuid == uuid; }),
            emitters.end());
    }

    void kParticleManager::clear()
    {
        emitters.clear();
    }

    void kParticleManager::setEmitterPosition(const kString &uuid, const kVec3 &worldPosition)
    {
        for (Emitter &e : emitters)
        {
            if (e.desc.uuid == uuid)
            {
                e.worldPosition = worldPosition;
                return;
            }
        }
    }

    int kParticleManager::getLiveParticleCount() const
    {
        int count = 0;
        for (const Emitter &e : emitters)
            count += static_cast<int>(e.particles.size());
        return count;
    }

    // -------------------------------------------------------------------------
    // Spawning
    // -------------------------------------------------------------------------

    void kParticleManager::spawnParticle(Emitter &emitter)
    {
        const kParticle &desc = emitter.desc;

        Particle p;
        p.maxLife   = desc.lifetime;
        p.life      = desc.lifetime;
        p.colorStart = desc.colorStart;
        p.colorEnd   = desc.colorEnd;
        p.sizeStart  = desc.sizeStart;
        p.sizeEnd    = desc.sizeEnd;

        // --- Emission position based on shape --------------------------------
        kVec3 emitOffset(0.0f);
        switch (desc.emissionShape)
        {
        case kParticle::SHAPE_POINT:
            emitOffset = kVec3(0.0f);
            break;
        case kParticle::SHAPE_SPHERE:
        {
            // Random point inside unit sphere (rejection sampling for uniform volume)
            float r = desc.shapeSize.x; // radius
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                kVec3 cand(
                    (emitter.dist01(emitter.rng) * 2.0f - 1.0f) * r,
                    (emitter.dist01(emitter.rng) * 2.0f - 1.0f) * r,
                    (emitter.dist01(emitter.rng) * 2.0f - 1.0f) * r);
                if (glm::length(cand) <= r)
                {
                    emitOffset = cand;
                    break;
                }
            }
            break;
        }
        case kParticle::SHAPE_CONE:
        {
            // Random point inside a cone pointing up (+Y), then rotate
            float h  = desc.shapeSize.y; // height
            float r  = desc.shapeSize.x; // base radius
            float t  = emitter.dist01(emitter.rng);            // height fraction
            float maxR = r * (1.0f - t);                       // radius at this height
            float angle = emitter.dist01(emitter.rng) * 6.283185307f; // 2*PI
            float rad   = std::sqrt(emitter.dist01(emitter.rng)) * maxR;
            emitOffset = kVec3(std::cos(angle) * rad, t * h, std::sin(angle) * rad);
            break;
        }
        case kParticle::SHAPE_BOX:
        {
            kVec3 hs = desc.shapeSize; // half-extents
            emitOffset = kVec3(
                (emitter.dist01(emitter.rng) * 2.0f - 1.0f) * hs.x,
                (emitter.dist01(emitter.rng) * 2.0f - 1.0f) * hs.y,
                (emitter.dist01(emitter.rng) * 2.0f - 1.0f) * hs.z);
            break;
        }
        }

        p.position = emitter.worldPosition + emitOffset;

        // --- Velocity --------------------------------------------------------
        kVec3 baseVel = glm::normalize(
            desc.startVelocity.length() > 0.0001f
                ? desc.startVelocity
                : kVec3(0.0f, 1.0f, 0.0f));
        kVec3 var(
            (emitter.dist01(emitter.rng) * 2.0f - 1.0f) * desc.velocityVariance.x,
            (emitter.dist01(emitter.rng) * 2.0f - 1.0f) * desc.velocityVariance.y,
            (emitter.dist01(emitter.rng) * 2.0f - 1.0f) * desc.velocityVariance.z);
        p.velocity = (baseVel + var) * desc.startSpeed;

        emitter.particles.push_back(p);
    }

    // -------------------------------------------------------------------------
    // Simulation update
    // -------------------------------------------------------------------------

    void kParticleManager::update(float dt)
    {
        // Clamp dt to avoid explosions after a pause / breakpoint
        if (dt <= 0.0f || dt > 0.1f)
            return;

        const float gravity = -9.81f;

        for (Emitter &emitter : emitters)
        {
            const kParticle &desc = emitter.desc;

            if (!desc.isActive)
                continue;

            // --- Emission ----------------------------------------------------
            if (desc.emissionRate > 0.0f && desc.maxParticles > 0)
            {
                emitter.emissionAccum += desc.emissionRate * dt;
                int toSpawn = static_cast<int>(emitter.emissionAccum);
                emitter.emissionAccum -= static_cast<float>(toSpawn);

                // Clamp to max particles
                int capacity = desc.maxParticles - static_cast<int>(emitter.particles.size());
                if (capacity < 0) capacity = 0;
                if (toSpawn > capacity) toSpawn = capacity;

                for (int i = 0; i < toSpawn; ++i)
                    spawnParticle(emitter);
            }

            // --- Integration ------------------------------------------------
            for (size_t i = 0; i < emitter.particles.size(); )
            {
                Particle &p = emitter.particles[i];

                p.life -= dt;

                if (p.life <= 0.0f)
                {
                    // Dead — remove by swap-with-last
                    p = emitter.particles.back();
                    emitter.particles.pop_back();
                    // Don't increment i — we need to process the swapped-in particle
                    continue;
                }

                // Apply gravity
                p.velocity.y += gravity * desc.gravityScale * dt;

                // Integrate position
                p.position += p.velocity * dt;

                ++i;
            }
        }
    }

    // -------------------------------------------------------------------------
    // GPU instance data upload
    // -------------------------------------------------------------------------

    void kParticleManager::uploadInstanceData()
    {
        kDriver *driver = kDriver::getCurrent();
        if (!driver)
            return;

        // Count total live particles
        size_t totalCount = 0;
        for (const Emitter &e : emitters)
            totalCount += e.particles.size();

        if (totalCount == 0)
            return;

        // Build instance data array
        std::vector<ParticleInstanceData> instances;
        instances.reserve(totalCount);

        for (const Emitter &e : emitters)
        {
            for (const Particle &p : e.particles)
            {
                ParticleInstanceData id;
                id.posX = p.position.x;
                id.posY = p.position.y;
                id.posZ = p.position.z;

                // Interpolate size and colour over lifetime
                float t = 1.0f - (p.life / p.maxLife); // 0 = birth, 1 = death
                id.size = p.sizeStart + (p.sizeEnd - p.sizeStart) * t;

                kVec4 col = p.colorStart + (p.colorEnd - p.colorStart) * t;
                id.r = col.r;
                id.g = col.g;
                id.b = col.b;
                id.a = col.a;

                instances.push_back(id);
            }
        }

        size_t dataSize = instances.size() * sizeof(ParticleInstanceData);

        // Grow the instance VBO if needed
        if (instances.size() > instanceCapacity)
        {
            // Allocate a bit more than needed to reduce reallocations
            size_t newCap = instances.size() * 2;
            if (newCap < 256) newCap = 256;
            driver->uploadVertexBuffer(instanceVbo, nullptr, newCap * sizeof(ParticleInstanceData));
            instanceCapacity = newCap;

            // Re-bind instance attributes after resizing the buffer
            driver->bindVertexArray(quadVao);
            driver->setVertexAttribFloat(1, 3, sizeof(ParticleInstanceData), 0);
            driver->setVertexAttribFloat(2, 1, sizeof(ParticleInstanceData), sizeof(float) * 3);
            driver->setVertexAttribFloat(3, 4, sizeof(ParticleInstanceData), sizeof(float) * 4);
            driver->unbindVertexArray();
        }

        // Upload via sub-data update (faster than re-allocating)
        driver->updateBufferSubData(instanceVbo, instances.data(), dataSize, 0);
    }

    // -------------------------------------------------------------------------
    // Rendering
    // -------------------------------------------------------------------------

    void kParticleManager::render(const kMat4 &viewMatrix,
                                   const kMat4 &projectionMatrix,
                                   const kVec3 &cameraRight,
                                   const kVec3 &cameraUp)
    {
        if (!initialized)
            return;

        kDriver *driver = kDriver::getCurrent();
        if (!driver)
            return;

        // Count live particles
        size_t totalCount = 0;
        for (const Emitter &e : emitters)
            totalCount += e.particles.size();

        if (totalCount == 0)
            return;

        // Upload instance data for this frame
        uploadInstanceData();

        // --- Render state for particles --------------------------------------
        // Particles are additive-blended, depth-tested but not depth-writing,
        // so they sort correctly against opaque geometry and against each other
        // without occluding.
        driver->setBlend(true);
        driver->setBlendFunc(kBlendFactor::SRC_ALPHA, kBlendFactor::ONE_MINUS_SRC_ALPHA);
        driver->setDepthTest(true);
        driver->setDepthWrite(false);       // don't write depth — particles are transparent
        driver->setCullFace(false);         // billboards are double-sided

        // --- Bind shader and set uniforms ------------------------------------
        driver->bindShaderProgram(shaderProgram);
        driver->setUniformMat4(shaderProgram, "uView", viewMatrix);
        driver->setUniformMat4(shaderProgram, "uProjection", projectionMatrix);
        driver->setUniformVec3(shaderProgram, "uCameraRight", cameraRight);
        driver->setUniformVec3(shaderProgram, "uCameraUp", cameraUp);

        // --- Draw instanced --------------------------------------------------
        // The instance VBO is bound to the VAO with divisor=1 for attributes
        // 1,2,3, so each instance gets its own position/size/color. The quad
        // geometry (6 indices) is reused for every instance.
        driver->drawIndexedInstanced(quadVao, 6, static_cast<int>(totalCount));

        // --- Restore state ---------------------------------------------------
        driver->setBlend(false);
        driver->setDepthWrite(true);
        driver->setCullFace(true);
    }

} // namespace kemena
