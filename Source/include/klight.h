/**
 * @file klight.h
 * @brief Light source scene-graph node (point, directional, spotlight).
 */

#ifndef KLIGHT_H
#define KLIGHT_H

#include "kexport.h"

#include "kobject.h"

namespace kemena
{
    /**
     * @brief Scene-graph node that emits light.
     *
     * Supports three light types controlled by setLightType():
     * - @c LIGHT_TYPE_POINT      — omnidirectional, attenuated by distance.
     * - @c LIGHT_TYPE_SUN        — parallel rays (directional), no attenuation.
     * - @c LIGHT_TYPE_SPOTLIGHT  — cone-shaped, inner/outer cut-off angles.
     *
     * Phong colour components (diffuse, specular) apply to all types.
     * Attenuation coefficients apply to point and spot lights only.
     * Ambient lighting is handled at the scene level via kScene::setAmbientLightColor().
     */
    class KEMENA3D_API kLight : public kObject
    {
    public:
        /**
         * @brief Constructs a light node and optionally attaches it to a parent.
         * @param parentNode Parent scene-graph node, or nullptr.
         */
        kLight(kObject *parentNode = nullptr);

        /**
         * @brief Returns the diffuse colour component.
         * @return RGB diffuse colour (0..1 per channel).
         */
        kVec3 getDiffuseColor();

        /**
         * @brief Sets the diffuse colour component.
         * @param newColor RGB diffuse colour.
         */
        void setDiffuseColor(kVec3 newColor);

        /**
         * @brief Returns the specular colour component.
         * @return RGB specular colour (0..1 per channel).
         */
        kVec3 getSpecularColor();

        /**
         * @brief Sets the specular colour component.
         * @param newColor RGB specular colour.
         */
        void setSpecularColor(kVec3 newColor);

        /**
         * @brief Returns the light power (intensity multiplier).
         * @return Power value (default 10.0).
         */
        float getPower();

        /**
         * @brief Sets the light power (intensity multiplier).
         * @param newValue New power value.
         */
        void setPower(float newValue);

        /**
         * @brief Returns the constant attenuation term.
         * @return Constant term used in the attenuation formula.
         */
        float getConstant();

        /**
         * @brief Returns the linear attenuation term.
         * @return Linear term used in the attenuation formula.
         */
        float getLinear();

        /**
         * @brief Returns the quadratic attenuation term.
         * @return Quadratic term used in the attenuation formula.
         */
        float getQuadratic();

        /**
         * @brief Sets the constant attenuation term.
         * @param newValue New constant attenuation value.
         */
        void setConstant(float newValue);

        /**
         * @brief Sets the linear attenuation term.
         * @param newValue New linear attenuation value.
         */
        void setLinear(float newValue);

        /**
         * @brief Sets the quadratic attenuation term.
         * @param newValue New quadratic attenuation value.
         */
        void setQuadratic(float newValue);

        /**
         * @brief Returns the light direction (sun and spotlight only).
         * @return Normalised direction vector in world space.
         */
        kVec3 getDirection();

        /**
         * @brief Sets the light direction (sun and spotlight only).
         * @param newValue Normalised direction vector in world space.
         */
        void setDirection(kVec3 newValue);

        /**
         * @brief Returns the inner cut-off cosine (spotlight only).
         * @return cos(inner half-angle).
         */
        float getCutOff();

        /**
         * @brief Sets the inner cut-off angle (spotlight only).
         * @param newValue cos(inner half-angle).
         */
        void setCutOff(float newValue);

        /**
         * @brief Returns the outer cut-off cosine (spotlight only).
         * @return cos(outer half-angle).
         */
        float getOuterCutOff();

        /**
         * @brief Sets the outer cut-off angle (spotlight only).
         * @param newValue cos(outer half-angle).
         */
        void setOuterCutOff(float newValue);

        /**
         * @brief Returns the light type.
         * @return One of the kLightType enum values.
         */
        kLightType getLightType();

        /**
         * @brief Sets the light type.
         * @param newType New light type.
         */
        void setLightType(kLightType newType);

        /**
         * @brief Serialises this light node to JSON.
         *
         * Overrides kObject::serialize() to include the light-specific
         * properties (type, colours, power, attenuation, direction and
         * spotlight cut-off angles) in addition to the base node data.
         * @return JSON object describing the light.
         */
        json serialize() override;

    protected:
    private:
        kVec3 diffuseColor  = kVec3(1.0f, 1.0f, 1.0f); ///< Diffuse colour.
        kVec3 specularColor = kVec3(1.0f, 1.0f, 1.0f); ///< Specular colour.

        float power = 10.0f; ///< Intensity multiplier.

        // Point light attenuation
        float constant  = 1.0f; ///< Constant attenuation coefficient.
        float linear    = 0.7f; ///< Linear attenuation coefficient.
        float quadratic = 1.8f; ///< Quadratic attenuation coefficient.

        // Sun / directional
        kVec3 direction = kVec3(1.0f, 1.0f, 1.0f); ///< Light direction (normalised).

        // Spotlight cone
        float cutOff      = glm::cos(glm::radians(15.0f)); ///< Inner cone cosine.
        float outerCutOff = glm::cos(glm::radians(20.0f)); ///< Outer cone cosine.

        kLightType lightType = kLightType::LIGHT_TYPE_POINT; ///< Active light type.
    };
}

#endif // KLIGHT_H
