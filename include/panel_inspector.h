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

using namespace kemena;

/**
 * @brief Editor Inspector panel that displays and edits the properties of the current selection.
 *
 * Renders the ImGui inspector for the active selection, covering scene objects (mesh, light,
 * camera, scene) and project assets. Hosts several embedded offscreen-rendered previews:
 * a live GLSL shader preview, a 3D model viewer, and a material viewer/inspector, each with
 * its own standalone world, scene, camera and light state.
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
	// Shader preview
	// -----------------------------------------------------------------------
	kOffscreenRenderer *previewRenderer = nullptr;	  ///< Offscreen renderer for the shader preview.
	kWorld *previewWorld = nullptr;					  ///< Standalone world for the preview, not the editor world.
	kScene *previewScene = nullptr;					  ///< Preview scene, owned by previewWorld.
	kCamera *previewCamera = nullptr;				  ///< Preview camera, manually owned.
	kLight *previewLight = nullptr;					  ///< Scene-owned sun light for the preview.
	kMesh *previewMesh = nullptr;					  ///< Scene-owned preview sphere.
	kShader *previewShader = nullptr;				  ///< Preview shader, rebuilt on GLSL change.
	kMaterial *previewMat = nullptr;				  ///< Preview material, rebuilt on GLSL change.
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

	// Light control
	bool lightEnabled = true;	  ///< Whether the preview light is enabled.
	float lightYaw = 45.0f;		  ///< Preview light azimuth, in degrees.
	float lightPitch = 60.0f;	  ///< Preview light elevation, in degrees.
	bool isDraggingLight = false; ///< Whether the user is dragging to rotate the preview light.

	/** @brief Draws the live shader preview, rebuilding the shader/material as needed. */
	void drawShaderPreview();

	/** @brief Lazily creates the standalone preview world, scene, camera, light and sphere. */
	void initPreviewScene();

	/** @brief Updates the preview light direction from the current yaw/pitch and enabled state. */
	void updatePreviewLight();

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
	// Model viewer
	// -----------------------------------------------------------------------
	kOffscreenRenderer *modelViewRenderer = nullptr; ///< Offscreen renderer for the model viewer.
	kWorld *modelViewWorld = nullptr;				 ///< Standalone world for the model viewer.
	kScene *modelViewScene = nullptr;				 ///< Model viewer scene, owned by modelViewWorld.
	kCamera *modelViewCamera = nullptr;				 ///< Model viewer orbit camera.
	kLight *modelViewLight = nullptr;				 ///< Model viewer sun light.
	kMesh *modelViewMesh = nullptr;					 ///< Displayed mesh, owned by the asset manager.
	std::vector<kMaterial *> modelViewDefaultMats;	 ///< Default materials created for the viewed mesh.

	std::string modelViewUuid;			 ///< UUID of the model currently displayed.
	bool modelViewLightEnabled = false;	 ///< Whether the model viewer light is enabled.
	float modelViewLightYaw = 45.0f;	 ///< Model viewer light azimuth, in degrees.
	float modelViewLightPitch = 60.0f;	 ///< Model viewer light elevation, in degrees.
	float modelViewRotX = 24.09f;		 ///< Orbit pitch; matches thumbnail dir normalize(0.5,0.5,1).
	float modelViewRotY = 26.57f;		 ///< Orbit yaw, in degrees.
	kVec3 modelViewCenter = kVec3(0.0f); ///< Orbit pivot at the model's bounding centre.
	float modelViewCamDist = 3.0f;		 ///< Orbit camera distance from the pivot.
	bool isDraggingMVLight = false;		 ///< Whether the user is dragging to rotate the viewer light.
	bool isDraggingMVModel = false;		 ///< Whether the user is dragging to orbit the model.

	/**
	 * @brief Draws the embedded 3D model viewer for a selected model asset.
	 * @param asset The selected project asset to display.
	 */
	void drawModelViewer(const PanelProject::SelectedProjectAsset &asset);

	/** @brief Lazily creates the standalone model-viewer world, scene, camera and light. */
	void initModelViewScene();

	/** @brief Updates the model-viewer light direction from its yaw/pitch and enabled state. */
	void updateModelViewLight();

	/** @brief Positions the orbit camera to frame the current model's bounds. */
	void frameModelViewCamera();

	/**
	 * @brief Assigns a default material to every sub-mesh that lacks one.
	 * @param mesh Mesh whose sub-meshes are checked for missing materials.
	 * @param defaultShader Shader used to build the default materials.
	 */
	void applyDefaultMaterial(kMesh *mesh, kShader *defaultShader);

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

	kOffscreenRenderer *matViewRenderer = nullptr; ///< Offscreen renderer for the material viewer.
	kWorld *matViewWorld = nullptr;				   ///< Standalone world for the material viewer.
	kScene *matViewScene = nullptr;				   ///< Material viewer scene, owned by matViewWorld.
	kCamera *matViewCamera = nullptr;			   ///< Material viewer camera.
	kLight *matViewLight = nullptr;				   ///< Material viewer sun light.
	kMesh *matViewMesh = nullptr;				   ///< Material preview sphere, manually owned.
	kMaterial *matViewMat = nullptr;			   ///< Material applied to the preview sphere, manually owned.
	int matViewVersion = -1;					   ///< Material version last rendered, to detect changes.
	std::string matViewUuid;					   ///< UUID of the material currently rendered.

	float matViewLightYaw = 45.0f;	 ///< Material viewer light azimuth, in degrees.
	float matViewLightPitch = 60.0f; ///< Material viewer light elevation, in degrees.
	bool matViewLightEnabled = true; ///< Whether the material viewer light is enabled.
	bool isDraggingMatLight = false; ///< Whether the user is dragging to rotate the viewer light.

	float matViewRotX = 6.0f;		 ///< Material viewer orbit pitch, in degrees.
	float matViewRotY = 0.0f;		 ///< Material viewer orbit yaw, in degrees.
	float matViewCamDist = 3.0f;	 ///< Material viewer orbit camera distance.
	bool isDraggingMatModel = false; ///< Whether the user is dragging (LMB) to orbit the preview.

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

	/** @brief Lazily creates the standalone material-viewer world, scene, camera, light and sphere. */
	void initMatViewScene();

	/** @brief Updates the material-viewer light direction from its yaw/pitch and enabled state. */
	void updateMatViewLight();

	/**
	 * @brief Rebuilds the preview material from a material JSON definition.
	 * @param matJson JSON describing the material properties to apply.
	 */
	void rebuildMatViewMaterial(const nlohmann::json &matJson);

	// -----------------------------------------------------------------------
	// Animation preview
	// -----------------------------------------------------------------------
	kOffscreenRenderer *animPreviewRenderer = nullptr; ///< Offscreen renderer for the animation preview.
	kWorld *animPreviewWorld = nullptr;                ///< Standalone world for the animation preview.
	kScene *animPreviewScene = nullptr;                ///< Animation preview scene, owned by animPreviewWorld.
	kCamera *animPreviewCamera = nullptr;              ///< Animation preview orbit camera.
	kLight *animPreviewLight = nullptr;                ///< Animation preview sun light.
	kMesh *animPreviewMesh = nullptr;                  ///< Displayed mesh, owned by the asset manager.
	kMaterial *animPreviewMat = nullptr;               ///< Material applied to the preview mesh, manually owned.
	kAnimator *animPreviewAnimator = nullptr;          ///< Skeletal animator driving the preview.
	kSkeletalAnimation *animPreviewClip = nullptr;     ///< Skeletal animation clip.

	std::string animPreviewUuid;         ///< .animation asset UUID currently previewed.
	std::string animPreviewMeshUuid;     ///< Mesh UUID linked to this animation.
	int animPreviewStartFrame = 0;       ///< First frame of the animation clip.
	int animPreviewEndFrame   = 30;      ///< Last frame of the animation clip.
	int animPreviewStartFrameDraft = 0;  ///< Unsaved Start Frame value shown in the inspector.
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

	bool animPreviewLightEnabled = false; ///< Whether the preview light is enabled.
	float animPreviewLightYaw   = 45.0f; ///< Preview light azimuth, in degrees.
	float animPreviewLightPitch = 60.0f; ///< Preview light elevation, in degrees.
	float animPreviewRotX = 24.09f;      ///< Orbit pitch; matches thumbnail dir normalize(0.5,0.5,1).
	float animPreviewRotY = 26.57f;      ///< Orbit yaw, in degrees.
	kVec3 animPreviewCenter = kVec3(0.0f); ///< Orbit pivot at the model's bounding centre.
	float animPreviewCamDist = 3.0f;     ///< Orbit camera distance from the pivot.
	bool isDraggingAPLight = false;      ///< Whether the user is dragging to rotate the preview light.
	bool isDraggingAPModel = false;      ///< Whether the user is dragging to orbit the model.

	float animPreviewFrame = 0.0f;       ///< Current playback frame.
	bool animPreviewPlaying = false;     ///< Whether playback is running.
	bool animPreviewLoop    = true;      ///< Whether playback loops.
	float animPreviewSpeed  = 1.0f;      ///< Playback speed multiplier.

	/**
	 * @brief Draws a 3D animation preview for a selected .animation asset.
	 *
	 * Renders the mesh with a default Phong material, plays the skeletal
	 * animation clip between startFrame and endFrame with play/pause/stop
	 * controls, a progress bar, and orbit/light interaction.
	 * @param asset The selected project asset to preview.
	 */
	void drawAnimationPreview(const PanelProject::SelectedProjectAsset &asset);

	/** @brief Lazily creates the standalone animation-preview world, scene, camera and light. */
	void initAnimPreviewScene();

	/** @brief Updates the animation-preview light direction from its yaw/pitch and enabled state. */
	void updateAnimPreviewLight();

	/** @brief Positions the orbit camera to frame the animation preview mesh's bounds. */
	void frameAnimPreviewCamera();
};

#endif