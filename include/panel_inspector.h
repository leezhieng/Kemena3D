#ifndef PANEL_INSPECTOR_H
#define PANEL_INSPECTOR_H

#include "kemena/kemena.h"
#include <kemena/kmesh.h>
#include <kemena/klight.h>
#include <kemena/kcamera.h>
#include <kemena/kscene.h>
#include <kemena/koffscreenrenderer.h>
#include <kemena/ktexture2d.h>
#include <glm/gtc/matrix_transform.hpp>

#include "manager.h"
#include "model_preview.h"

using namespace kemena;

/**
 * @brief Editor Inspector panel that displays and edits the properties of the current selection.
 *
 * Renders the ImGui inspector for the active selection, covering scene objects (mesh, light,
 * camera, scene) and project assets. Hosts several embedded offscreen-rendered previews: a
 * live GLSL shader preview, plus shared ModelPreview-based viewers for models, materials and
 * animations.
 */
class PanelInspector
{
public:
	/**
	 * @brief Constructs the inspector panel.
	 * @param setGuiManager GUI manager used to issue ImGui draw calls and load icon textures.
	 * @param setManager Editor manager providing access to the project, selection and assets.
	 */
	PanelInspector(kGuiManager *setGuiManager, Manager *setManager);

	/** @brief Destroys the panel and releases the preview/viewer render resources it owns. */
	~PanelInspector();

	/**
	 * @brief Draws the inspector panel for the current frame.
	 * @param opened Reference to the panel's visibility flag; cleared when the user closes it.
	 */
	void draw(bool &opened);

	Manager *manager; ///< Editor manager (project, selection, assets).
	kGuiManager *gui; ///< GUI manager used for ImGui rendering and texture loading.

	/** @brief True while the inspector window owns ImGui focus. */
	bool focused = false;

private:
	// Scene object type icons
	ImTextureRef iconObjMesh = nullptr;	   ///< Icon for mesh scene objects.
	ImTextureRef iconObjLight = nullptr;   ///< Icon for light scene objects.
	ImTextureRef iconObjCamera = nullptr;  ///< Icon for camera scene objects.
	ImTextureRef iconObjAudio = nullptr;   ///< Icon for audio emitter scene objects.
	ImTextureRef iconObjAudioListener = nullptr; ///< Icon for audio listener scene objects.
	ImTextureRef iconObjScene = nullptr;   ///< Icon for scene root objects.
	ImTextureRef iconObjTerrain = nullptr; ///< Icon for terrain objects.
	ImTextureRef iconObjPrefab = nullptr;  ///< Icon for prefab-instance roots.
	ImTextureRef iconObjPrefabEdited = nullptr; ///< Icon for dirty prefab-instance roots (cube-orange).

	// File type icons
	ImTextureRef iconFileModel = nullptr;	 ///< Icon for model/mesh asset files.
	ImTextureRef iconFileImage = nullptr;	 ///< Icon for image/texture asset files.
	ImTextureRef iconFileFolder = nullptr;	 ///< Icon for folders.
	ImTextureRef iconFileMaterial = nullptr; ///< Icon for material asset files.
	ImTextureRef iconFilePrefab = nullptr;	 ///< Icon for prefab asset files.
	ImTextureRef iconFileAudio = nullptr;	 ///< Icon for audio asset files.
	ImTextureRef iconFileVideo = nullptr;	 ///< Icon for video asset files.
	ImTextureRef iconFileScript = nullptr;	 ///< Icon for script asset files.
	ImTextureRef iconFileText = nullptr;	 ///< Icon for text asset files.
	ImTextureRef iconFileWorld = nullptr;	 ///< Icon for world/scene asset files.
	ImTextureRef iconFileOther = nullptr;	 ///< Fallback icon for unrecognised file types.

	/**
	 * @brief Returns the icon texture matching a given asset file type.
	 * @param fileType File type/extension string used to select the icon.
	 * @return Texture reference for the matching icon, or the "other" icon when unrecognised.
	 */
	ImTextureRef getFileTypeIcon(const kString &fileType) const;

	// -----------------------------------------------------------------------
	// Shader preview (shared ModelPreview widget)
	// -----------------------------------------------------------------------
	ModelPreview *shaderPreviewModel = nullptr;		 ///< Shared preview widget (sphere + shader material).
	kShader *previewShader = nullptr;				 ///< Preview shader, rebuilt on GLSL change.
	kMaterial *previewMat = nullptr;				 ///< Preview material, rebuilt on GLSL change.
	std::vector<kTexture2D *> previewDefaultTextures; ///< White 1x1 placeholder textures, one per sampler.

	std::string prevUuid;	///< UUID of the shader currently previewed.
	std::string prevGlsl;	///< Last GLSL source seen, used to detect changes.
	bool prevValid = false; ///< Whether the current preview shader compiled successfully.

	// Preview params
	float prevDiffuse[3] = {1.0f, 1.0f, 1.0f};	///< Preview diffuse colour (RGB).
	float prevSpecular[3] = {1.0f, 1.0f, 1.0f}; ///< Preview specular colour (RGB).
	float prevShininess = 32.0f;				///< Preview specular shininess exponent.
	float prevMetallic = 0.0f;					///< Preview metallic factor.
	float prevRoughness = 0.5f;					///< Preview roughness factor.

	/** @brief Draws the live shader preview, rebuilding the shader/material as needed. */
	void drawShaderPreview();

	/**
	 * @brief Loads persisted preview parameters for a shader from disk.
	 * @param uuid UUID of the shader whose preview parameters to load.
	 */
	void loadPreviewParams(const std::string &uuid);

	/**
	 * @brief Saves the current preview parameters for a shader to disk.
	 * @param uuid UUID of the shader whose preview parameters to save.
	 */
	void savePreviewParams(const std::string &uuid);

	/** @brief Rebuilds the preview shader and material from the current GLSL source. */
	void rebuildPreviewShader();

	/** @brief Applies the current preview parameters (colours, PBR factors) to the preview material. */
	void applyPreviewParams();

	// -----------------------------------------------------------------------
	// Model viewer (shared ModelPreview widget)
	// -----------------------------------------------------------------------
	ModelPreview *modelPreview = nullptr;		   ///< Shared preview widget for the model viewer.
	std::string modelViewUuid;					   ///< UUID of the model currently displayed.
	std::vector<kMaterial *> modelViewDefaultMats; ///< Default materials created for the viewed mesh.

	/**
	 * @brief Draws the embedded 3D model viewer for a selected model asset.
	 * @param asset The selected project asset to display.
	 */
	void drawModelViewer(const PanelProject::SelectedProjectAsset &asset);

	/**
	 * @brief Assigns a default material to every sub-mesh that lacks one.
	 * @param mesh Mesh whose sub-meshes are checked for missing materials.
	 * @param defaultShader Shader used to build the default materials.
	 */
	void applyDefaultMaterial(kMesh *mesh, kShader *defaultShader);

	// -----------------------------------------------------------------------
	// Prefab viewer (shared ModelPreview widget)
	// -----------------------------------------------------------------------
	ModelPreview *prefabPreview = nullptr; ///< Shared preview widget for the prefab viewer.
	std::string prefabViewUuid;            ///< UUID of the prefab currently displayed.

	/**
	 * @brief Draws the embedded 3D preview for a selected .prefab asset.
	 *
	 * The prefab's visuals are instantiated into the shared ModelPreview's
	 * scene (lights/cameras stripped) so a prefab looks the same as the other
	 * embedded model previews.
	 * @param asset The selected project asset to display.
	 */
	void drawPrefabPreview(const PanelProject::SelectedProjectAsset &asset);

	// -----------------------------------------------------------------------
	// Material inspector + viewer (shared live state)
	// -----------------------------------------------------------------------
	nlohmann::json matInspJson = nlohmann::json::object(); ///< Live JSON of the material being edited.
	std::string matInspUuid;							   ///< UUID of the material being inspected.
	bool matInspDirty = false;							   ///< Whether unsaved material edits are pending.
	int matInspVersion = 0;								   ///< Version counter, incremented on every property change.

	// Texture picker modal (replaces the per-slot texture dropdown).
	std::string texPickerParam;		///< Sampler uniform the picker is choosing for ("" = closed).
	std::string texPickerSelected;	///< UUID highlighted in the picker ("" = None).
	char texPickerSearch[128] = ""; ///< Name filter for the picker list.

	ModelPreview *matPreview = nullptr; ///< Shared preview widget for the material viewer.
	kMaterial *matViewMat = nullptr;	///< Material applied to the preview sphere, manually owned.
	int matViewVersion = -1;			///< Material version last rendered, to detect changes.
	std::string matViewUuid;			///< UUID of the material currently rendered.

	/**
	 * @brief Draws the embedded material preview viewer for a selected material asset.
	 * @param asset The selected project asset to display.
	 */
	void drawMaterialViewer(const PanelProject::SelectedProjectAsset &asset);

	/**
	 * @brief Draws the editable property UI for a selected material asset.
	 * @param asset The selected project asset to inspect and edit.
	 */
	void drawMaterialInspector(const PanelProject::SelectedProjectAsset &asset);

	/**
	 * @brief Rebuilds the preview material from a material JSON definition.
	 * @param matJson JSON describing the material properties to apply.
	 */
	void rebuildMatViewMaterial(const nlohmann::json &matJson);

	// -----------------------------------------------------------------------
	// Animation preview (shared ModelPreview widget)
	// -----------------------------------------------------------------------
	ModelPreview *animPreview = nullptr;		   ///< Shared preview widget for the animation preview.
	kMaterial *animPreviewMat = nullptr;		   ///< Material applied to the preview mesh, manually owned.
	kAnimator *animPreviewAnimator = nullptr;	   ///< Skeletal animator driving the preview.
	kSkeletalAnimation *animPreviewClip = nullptr; ///< Skeletal animation clip.

	std::string animPreviewUuid;		 ///< .animation asset UUID currently previewed.
	std::string animPreviewMeshUuid;	 ///< Mesh UUID linked to this animation.
	int animPreviewStartFrame = 0;		 ///< First frame of the animation clip.
	int animPreviewEndFrame   = 30;		 ///< Last frame of the animation clip.
	int animPreviewStartFrameDraft = 0;	 ///< Unsaved Start Frame value shown in the inspector.
	int animPreviewEndFrameDraft   = 30; ///< Unsaved End Frame value shown in the inspector.

	// Root-motion options stored in the .animation file. When enabled, at game
	// time the matching root-bone channel is redirected to scripts (as the
	// animator's delta position/rotation) instead of being applied to the
	// object. The animation preview always plays the full animation, including
	// root motion. *Draft values are shown/edited in the UI and committed to
	// the file when "Apply" is pressed.
	bool animPreviewRootMotionRotation  = false; ///< Root rotation channel committed.
	bool animPreviewRootMotionPositionY = false; ///< Root Y translation channel committed.
	bool animPreviewRootMotionPositionXZ = false; ///< Root XZ translation channel committed.
	bool animPreviewRootMotionRotationDraft  = false; ///< Root rotation channel draft.
	bool animPreviewRootMotionPositionYDraft = false; ///< Root Y translation channel draft.
	bool animPreviewRootMotionPositionXZDraft = false; ///< Root XZ translation channel draft.

	float animPreviewFrame = 0.0f;   ///< Current playback frame.
	bool animPreviewPlaying = false; ///< Whether playback is running.
	bool animPreviewLoop    = true;  ///< Whether playback loops.
	float animPreviewSpeed  = 1.0f;  ///< Playback speed multiplier.

	/**
	 * @brief Draws a 3D animation preview for a selected .animation asset.
	 *
	 * Renders the mesh with a default Phong material, plays the skeletal
	 * animation clip between startFrame and endFrame with play/pause/stop
	 * controls, a progress bar, and orbit/light interaction.
	 * @param asset The selected project asset to preview.
	 */
	void drawAnimationPreview(const PanelProject::SelectedProjectAsset &asset);
};

#endif
