#include "kemena/kemena.h"

#include "manager.h"
#include "commands.h"
#include "mainmenu.h"
#include "panel_world.h"
#include "panel_inspector.h"
#include "panel_hierarchy.h"
#include "panel_project.h"
#include "panel_console.h"
#include "panel_shadergraph.h"
#include "panel_logicgraph.h"
#include "panel_game.h"
#include "panel_prefab.h"
#include "panel_terrain.h"
#include "panel_animator.h"
#include "panel_animation.h"
#include "panel_particle.h"
#include "splash_screen.h"
#include "crashhandler.h"

#include "imgui_internal.h" // <-- required for ImGuiSettingsHandler

using namespace kemena;

const kString windowTitle = "Kemena3D Studio";

// Project config
kString projectName = "New Game";
kString developerName = "My Company";
kString projectVersion = "0.0.1";

int main()
{
	// Install the crash reporter before anything else so an early fault still
	// produces kemena3d_crash.log (the Release GUI build has no console).
	installCrashHandler();

	{
		std::ofstream f("D:\\Projects\\Kemena3D\\animator_debug.log", std::ios::app);
		if (f.is_open())
			f << "[startup] Kemena3DStudio launched" << std::endl;
	}

	// Create window and renderers.
	// Three separate kRenderer instances are created:
	//   rendererWorld  — renders the game scene into the World panel viewport
	//   rendererPrefab — renders the isolated prefab scene into the Prefab panel
	//   rendererGame   — (future) renders the game view with post-processing
	// Each owns its own kDriver (OpenGL context) with shared resources so
	// textures from one context can be used as ImGui images in another.
	kWindow *window = createWindow(1024, 768, windowTitle, true);

	kRenderer *rendererWorld = createRenderer(window);
	rendererWorld->setEnableScreenBuffer(true);
	rendererWorld->setEnableShadow(true);
	rendererWorld->setEnableObjectPicking(true);
	rendererWorld->setClearColor(kVec4(0.2f, 0.2f, 0.2f, 1.0f));

	kRenderer *rendererPrefab = createRenderer(window);
	rendererPrefab->setEnableScreenBuffer(true);
	rendererPrefab->setEnableShadow(true);
	rendererPrefab->setEnableObjectPicking(true);
	rendererPrefab->setClearColor(kVec4(0.2f, 0.2f, 0.2f, 1.0f));

	// Use rendererWorld as the primary renderer (drives the GUI manager and main loop).
	kRenderer *renderer = rendererWorld;

	// Ensure the world renderer's driver is the globally "current" one before
	// any further initialisation.  createRenderer() for the prefab panel left
	// its own driver as current via kDriver::setCurrent(), but all scene-graph
	// resources (VAOs, textures, shaders) live in the world driver's GL context.
	kDriver *worldDriver = rendererWorld->getDriver();
	worldDriver->makeCurrent(window);
	kDriver::setCurrent(worldDriver);

	// Setup GUI manager
	kGuiManager *gui = createGuiManager(renderer);

	// Create the asset manager
	kAssetManager *assetManager = createAssetManager();

	// Switch default font
	gui->loadDefaultFontFromResource("FONT_OPENSANS");

	// Create the world and scene
	kWorld *world = createWorld(assetManager);
	kScene *sceneEditor = world->createScene("_EditorScene_");
	kScene *scene = world->createScene("Scene");

	// Editor manager
	Manager *manager = new Manager(window, world, rendererWorld);
	manager->setScene(scene);
	manager->setGui(gui);

	// Assign the prefab's dedicated renderer to the manager so it can drive
	// the isolated prefab viewport rendering.
	manager->prefabRenderer = rendererPrefab;

	// Thumbnail renderer also loads preview / shadow shaders from resources.
	manager->thumbnailRenderer.setAssetManager(assetManager);

	// Initialize panels
	MainMenu *mainmenu = new MainMenu(gui, manager);
	PanelWorld *panelWorld = new PanelWorld(gui, manager);
	PanelInspector *panelInspector = new PanelInspector(gui, manager);
	PanelProject *panelProject = new PanelProject(gui, manager, assetManager);
	PanelHierarchy *panelHierarchy = new PanelHierarchy(gui, manager, assetManager, world);
	PanelConsole *panelConsole = new PanelConsole(gui, manager);
	PanelShaderGraph *panelShaderGraph = new PanelShaderGraph(gui, manager);
	manager->panelShaderGraph = panelShaderGraph;
	PanelLogicGraph *panelLogicGraph = new PanelLogicGraph(gui, manager);
	PanelGame *panelGame = new PanelGame(gui, manager);
	manager->panelGame = panelGame;
	PanelTerrain *panelTerrain = new PanelTerrain(gui, manager);
	manager->panelTerrain = panelTerrain;
	PanelAnimator *panelAnimator = new PanelAnimator(gui, manager);
	manager->panelAnimator = panelAnimator;
	PanelAnimation *panelAnimation = new PanelAnimation(gui, manager);
	manager->panelAnimation = panelAnimation;
	PanelParticle *panelParticle = new PanelParticle(gui, manager);
	manager->panelParticle = panelParticle;
	PanelPrefab *panelPrefab = new PanelPrefab(gui, manager);

	// Route .shader and .prefab double-clicks from the project panel.
	panelProject->onFileDoubleClicked = [&](const std::string &path)
	{
		if (path.size() >= 7 && path.substr(path.size() - 7) == ".shader")
		{
			showPanel.shaderEditor = true;
			panelShaderGraph->openFile(path);
		}
		else if (path.size() >= 6 && path.substr(path.size() - 6) == ".logic")
		{
			showPanel.scriptEditor = true;
			panelLogicGraph->openFile(path);
		}
		else if (path.size() >= 7 && path.substr(path.size() - 7) == ".prefab")
		{
			showPanel.prefab = true;
			manager->setEditorMode(Manager::EditorMode::PrefabPreview, path, ".prefab");
			manager->editPrefab(path);
			showPanel.prefab = manager->prefabEditing;
		}
		else if (path.size() >= 9 && path.substr(path.size() - 9) == ".animator")
		{
			showPanel.animatorEditor = true;
			panelAnimator->openFile(path);
			manager->setEditorMode(Manager::EditorMode::AnimatorPreview, path, ".animator");
		}
		else if (path.size() >= 10 && path.substr(path.size() - 10) == ".cinematic")
		{
			showPanel.animationEditor = true;
			panelAnimation->openFile(path);
		}
		else if (path.size() >= 9 && path.substr(path.size() - 9) == ".particle")
		{
			showPanel.particleEditor = true;
			panelParticle->openFile(path);
			manager->setEditorMode(Manager::EditorMode::ParticlePreview, path, ".particle");
		}
	};

	// Register the custom panel-state handler and load the default editor
	// layout. When a project is opened, Manager::loadProjectWorkspace() loads
	// that project's Config/workspace.ini (or falls back to this default).
	mainmenu->registerPanelStateHandler();
	manager->loadDefaultWorkspace();

	// Default skybox — shared helper so the inspector's "Apply Default
	// Skybox" button and the per-frame scene-change guard can reuse it.
	manager->applyDefaultSkybox(scene);

	// Editor grid
	kMesh *gridMesh = kMeshGenerator::generatePlane();
	gridMesh->setPosition(kVec3(0.0f, -0.01f, 0.0f));
	sceneEditor->setFrustumCullingEnabled(false);
	sceneEditor->addMesh(gridMesh);
	kShader *gridShader = assetManager->loadGlslFromResource("SHADER_GRID");
	kMaterial *gridMat = assetManager->createMaterial(gridShader);
	gridMat->setTransparent(kTransparentType::TRANSP_TYPE_BLEND);
	gridMat->setSingleSided(false);
	gridMesh->setMaterial(gridMat);

	// Default scene content (cube + sun light + game camera) lives in the
	// embedded WORLD_DEFAULT RCDATA — see res/default.world. Loading it here
	// keeps the placeholder data in one file instead of hand-coded objects.
	manager->loadDefaultWorldInto(scene);

	// Editor camera
	kCamera *cameraEditor = world->addCamera(kVec3(-7, 4, 12), kVec3(0, 3.5, 0), kCameraType::CAMERA_TYPE_FREE);
	cameraEditor->setFOV(60.0f);
	world->setMainCamera(cameraEditor);
	manager->editorCamera = cameraEditor;

	bool dragging = false;
	kVec2 dragStart;
	kQuat camRot;

	// Middle-mouse pan state. Captures the camera position and orbit pivot at
	// drag start so the per-frame motion handler can slide both along the
	// camera-space right/up axes without drift.
	bool panning = false;
	kVec2 panStart;
	kVec3 panStartCamPos;
	kVec3 panStartPivot;

	// Editor camera orbit state. F-frames-selected updates the pivot; the
	// drag-rotate path keeps the camera at orbitDistance from orbitPivot;
	// the wheel changes orbitDistance (dolly-toward-pivot).
	kVec3 cameraOrbitPivot = kVec3(0.0f, 3.5f, 0.0f);
	float cameraOrbitDistance = glm::length(kVec3(-7.0f, 4.0f, 12.0f) - cameraOrbitPivot);

	// F-focus tween. When the user presses F the camera doesn't snap — it
	// glides into the framed pose over `cameraTweenDuration` seconds with a
	// smoothstep ease. Drag / wheel input cancels the tween.
	const float cameraTweenDuration = 0.5f;
	bool cameraTweenActive = false;
	float cameraTweenT = 0.0f;
	kVec3 cameraTweenFromPos = kVec3(0);
	kVec3 cameraTweenToPos = kVec3(0);
	kQuat cameraTweenFromRot = kQuat(1, 0, 0, 0);
	kQuat cameraTweenToRot = kQuat(1, 0, 0, 0);
	kVec3 cameraTweenToPivot = kVec3(0);
	float cameraTweenToDist = 1.0f;

	// --- Prefab panel camera controls (mirrors world panel) ---
	bool prefabDragging = false;
	kVec2 prefabDragStart;
	kQuat prefabCamRot;

	bool prefabPanning = false;
	kVec2 prefabPanStart;
	kVec3 prefabPanStartCamPos;
	kVec3 prefabPanStartPivot;

	kVec3 prefabOrbitPivot = kVec3(0.0f, 1.0f, 0.0f);
	float prefabOrbitDistance = 9.0f;

	bool altPressed = false;
	bool ctrlPressed = false;
	bool shiftPressed = false;

	// Splash screen (shown at startup until a project is chosen or dismissed)
	SplashScreen *splashScreen = new SplashScreen(gui, assetManager, manager);

	// Game loop
	kSystemEvent event;
	while (window->getRunning())
	{
		// Must reset the layout at the beginning of the frame
		if (isReloadLayout)
		{
			showPanel = ShowPanel();
			gui->loadIniSettingsFromDisk(layoutFileName);
			MainMenu::loadPanelStateFromFile(layoutFileName);
			// Re-open the graph editor files that were saved with the workspace
			// (Logic Graph, Animator, Shader Graph, Cinematic).
			manager->restoreOpenEditorFiles(layoutFileName);

			isReloadLayout = false;
		}

		if (isReloadDefaultLayout)
		{
			showPanel = ShowPanel();
			manager->loadDefaultWorkspace();

			isReloadDefaultLayout = false;
		}

		// A world load (triggered by a menu action during a previous frame's
		// panel draw) restored the editor camera pose and staged its orbit
		// framing. Adopt it into our live locals BEFORE the mirror below, or the
		// mirror would overwrite the restored pivot/distance with stale values
		// and the first orbit drag would snap the camera. Cancel any focus tween
		// so nothing fights the restore.
		if (manager->editorCamLoadPending)
		{
			cameraOrbitPivot = manager->editorCamOrbitPivot;
			cameraOrbitDistance = manager->editorCamOrbitDistance;
			camRot = cameraEditor->getRotation();
			cameraTweenActive = false;
			manager->editorCamLoadPending = false;
		}

		// Keep the manager's copy of the editor-camera orbit state current so a
		// File>Save handled during this frame persists the live viewpoint.
		manager->editorCamOrbitPivot = cameraOrbitPivot;
		manager->editorCamOrbitDistance = cameraOrbitDistance;

		float deltaTime = window->getTimer()->getDeltaTime();
		gui->processEvent(event);

		// Event
		if (event.hasEvent())
		{
			int eventType = event.getType();

			if (eventType == K_EVENT_QUIT)
			{
				manager->closeEditor();
			}
			else if (eventType == SDL_EVENT_WINDOW_FOCUS_GAINED)
			{
				// Check asset changes
				if (manager->projectOpened && !manager->showImportPopup)
				{
					manager->checkAssetChange();
				}
			}
			else if (eventType == SDL_EVENT_DROP_FILE)
			{
				// OS file dropped onto the window — copy it into the currently-
				// browsed project folder so checkAssetChange picks it up via the
				// existing import pipeline. Drops outside an open project are
				// ignored.
				const char *droppedPath = event.getSdlEvent()->drop.data;
				if (droppedPath && manager->projectOpened)
				{
					try
					{
						std::filesystem::path src(droppedPath);
						if (std::filesystem::exists(src) && std::filesystem::is_regular_file(src))
						{
							std::filesystem::path destDir = manager->getCurrentDirPath();
							std::filesystem::path dst = destDir / src.filename();
							int counter = 1;
							while (std::filesystem::exists(dst))
							{
								std::string stem = src.stem().string();
								std::string ext = src.extension().string();
								dst = destDir / (stem + " " + std::to_string(counter) + ext);
								counter++;
							}
							std::filesystem::copy_file(src, dst);
							manager->checkAssetChange();
							if (manager->panelProject)
								manager->panelProject->triggerRefresh();
						}
					}
					catch (const std::exception &e)
					{
						std::cerr << "drop file: " << e.what() << "\n";
					}
				}
			}
			else if (eventType == K_EVENT_MOUSEBUTTONDOWN)
			{
				if (panelWorld->enabled && panelWorld->hovered)
				{
					if (event.getMouseButton() == K_MOUSEBUTTON_LEFT && altPressed && panelWorld->focused)
					{
						dragging = true;

						dragStart.x = event.getMouseX();
						dragStart.y = event.getMouseY();

						camRot = cameraEditor->getRotation();
					}
					else if (event.getMouseButton() == K_MOUSEBUTTON_MIDDLE && panelWorld->focused)
					{
						panning = true;
						panStart.x = event.getMouseX();
						panStart.y = event.getMouseY();
						panStartCamPos = cameraEditor->getPosition();
						panStartPivot = cameraOrbitPivot;
						// User is steering — cancel any in-flight F tween.
						cameraTweenActive = false;
					}
					else if (event.getMouseButton() == K_MOUSEBUTTON_LEFT && !altPressed && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
					{
						// Snapshot selection before picking (for undo)
						auto selBefore = manager->selectedObjects;
						auto selObjBefore = manager->selectedObject;

						// ImGui::GetIO().MousePos and panelPos are both in the same
						// screen-absolute coordinate space. Multiply by 2 for physical pixels.
						kVec2 imMouse = gui->getMousePos();
						int vpMouseX = (int)((imMouse.x - panelWorld->panelPos.x) * 2.0f);
						int vpMouseY = (int)((imMouse.y - panelWorld->panelPos.y) * 2.0f);

						kScene *pickScene = scene;

						kObject *picked = renderer->pickObject(
							world, pickScene,
							vpMouseX, vpMouseY,
							panelWorld->width * 2, panelWorld->height * 2);

						// Walk up to the direct child of the scene root so we always
						// select the top-level object, not a sub-mesh leaf.
						if (picked != nullptr)
						{
							kObject *sceneRoot = pickScene->getRootNode();
							while (picked->getParent() != nullptr && picked->getParent() != sceneRoot)
								picked = picked->getParent();
						}

						// Disable terrain sculpt mode when clicking a non-terrain
						// object (or empty space) in the scene viewport.
						if (manager->panelTerrain && manager->panelTerrain->sculpt.active)
						{
							bool isTerrainClick = false;
							if (picked != nullptr)
							{
								kMesh *mesh = dynamic_cast<kMesh *>(picked);
								if (mesh && mesh->getSerializeType() == "terrain")
									isTerrainClick = true;
							}
							if (!isTerrainClick)
								manager->panelTerrain->sculpt.active = false;
						}

						if (picked != nullptr)
						{
							manager->worldSelected = false;
							manager->selectedScene = nullptr;
							manager->selectedObject = picked;
							manager->selectObject(picked->getUuid(), !shiftPressed);
							if (manager->panelProject != nullptr)
								manager->panelProject->clearSelection();
						}
						else if (!shiftPressed)
						{
							manager->worldSelected = false;
							manager->selectedObject = nullptr;
							manager->selectedObjects.clear();
						}

						// Push selection undo if it changed
						auto selAfter = manager->selectedObjects;
						auto selObjAfter = manager->selectedObject;
						if (selBefore != selAfter || selObjBefore != selObjAfter)
						{
							manager->undoRedo.push(std::make_unique<SelectCommand>(
								manager,
								selBefore, selObjBefore,
								selAfter, selObjAfter));
						}
					}
				}
				else
				{
					dragging = false;
				}

				// --- Prefab panel click-to-select (picking, uses OWN selection) ---
				if (panelPrefab->enabled && panelPrefab->hovered &&
				    manager->prefabRenderer && manager->prefabWorld && manager->prefabScene)
				{
					if (event.getMouseButton() == K_MOUSEBUTTON_LEFT &&
					    !altPressed && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
					{
						auto selBefore = manager->prefabSelectedObjects;
						auto selObjBefore = manager->prefabSelectedObject;

						kVec2 imMouse = gui->getMousePos();
						int vpMouseX = (int)((imMouse.x - panelPrefab->panelPos.x) * 2.0f);
						int vpMouseY = (int)((imMouse.y - panelPrefab->panelPos.y) * 2.0f);

						kObject *picked = manager->prefabRenderer->pickObject(
							manager->prefabWorld, manager->prefabScene,
							vpMouseX, vpMouseY,
							panelPrefab->width * 2, panelPrefab->height * 2);

						if (picked && manager->prefabScene->getRootNode())
						{
							kObject *sceneRoot = manager->prefabScene->getRootNode();
							while (picked->getParent() && picked->getParent() != sceneRoot)
								picked = picked->getParent();
						}

						if (picked)
						{
							manager->prefabSelectedObject = picked;
							if (!shiftPressed)
								manager->prefabSelectedObjects.clear();
							manager->prefabSelectedObjects.push_back(picked->getUuid());
						}
						else if (!shiftPressed)
						{
							manager->prefabSelectedObject = nullptr;
							manager->prefabSelectedObjects.clear();
						}

						auto selAfter = manager->prefabSelectedObjects;
						auto selObjAfter = manager->prefabSelectedObject;
						if (selBefore != selAfter || selObjBefore != selObjAfter)
						{
							manager->undoRedo.push(std::make_unique<SelectCommand>(
								manager, selBefore, selObjBefore,
								selAfter, selObjAfter));
						}
					}
				}

				// --- Prefab panel mouse-down events (camera) ---
				if (panelPrefab->enabled && panelPrefab->hovered)
				{
					if (event.getMouseButton() == K_MOUSEBUTTON_LEFT && altPressed && panelPrefab->focused)
					{
						prefabDragging = true;
						prefabDragStart.x = event.getMouseX();
						prefabDragStart.y = event.getMouseY();
						if (manager->prefabCamera)
							prefabCamRot = manager->prefabCamera->getRotation();
					}
					else if (event.getMouseButton() == K_MOUSEBUTTON_MIDDLE && panelPrefab->focused)
					{
						prefabPanning = true;
						prefabPanStart.x = event.getMouseX();
						prefabPanStart.y = event.getMouseY();
						if (manager->prefabCamera)
						{
							prefabPanStartCamPos = manager->prefabCamera->getPosition();
							prefabPanStartPivot = prefabOrbitPivot;
						}
					}
				}
			}
			else if (eventType == K_EVENT_MOUSEBUTTONUP)
			{
				if (dragging)
					dragging = false;

				if (panning && event.getMouseButton() == K_MOUSEBUTTON_MIDDLE)
					panning = false;

				if (panelWorld->enabled && panelWorld->hovered)
				{
					if (event.getMouseButton() == K_MOUSEBUTTON_LEFT)
					{
						camRot = cameraEditor->getRotation();
					}
				}

				// --- Prefab panel mouse-up events ---
				if (prefabDragging && event.getMouseButton() == K_MOUSEBUTTON_LEFT)
					prefabDragging = false;
				if (prefabPanning && event.getMouseButton() == K_MOUSEBUTTON_MIDDLE)
					prefabPanning = false;
			}
			else if (eventType == K_EVENT_MOUSEMOTION)
			{
				// --- World panel camera motion ---
				if (panelWorld->enabled && panelWorld->hovered)
				{
					if (dragging)
					{
						float deltaX = dragStart.x - event.getMouseX();
						float deltaY = dragStart.y - event.getMouseY();

						if (cameraEditor->getCameraType() == kCameraType::CAMERA_TYPE_FREE)
						{
							cameraTweenActive = false;
							cameraEditor->rotateByMouse(camRot, -deltaX, -deltaY);
							kVec3 fwd = cameraEditor->calculateForward();
							cameraEditor->setPosition(cameraOrbitPivot - fwd * cameraOrbitDistance);
						}
					}
					else if (panning)
					{
						float deltaX = event.getMouseX() - panStart.x;
						float deltaY = event.getMouseY() - panStart.y;

						float panScale = cameraOrbitDistance * 0.0025f;
						kVec3 right = cameraEditor->calculateRight();
						kVec3 up = cameraEditor->calculateUp();
						kVec3 offset = (right * deltaX + up * deltaY) * panScale;

						cameraEditor->setPosition(panStartCamPos + offset);
						cameraOrbitPivot = panStartPivot + offset;
					}
				}

				// --- Prefab panel camera motion ---
				if (panelPrefab->enabled && panelPrefab->hovered && manager->prefabCamera)
				{
					kCamera *pcam = manager->prefabCamera;
					if (prefabDragging)
					{
						float deltaX = prefabDragStart.x - event.getMouseX();
						float deltaY = prefabDragStart.y - event.getMouseY();

						pcam->rotateByMouse(prefabCamRot, -deltaX, -deltaY);
						kVec3 fwd = pcam->calculateForward();
						pcam->setPosition(prefabOrbitPivot - fwd * prefabOrbitDistance);
					}
					else if (prefabPanning)
					{
						float deltaX = event.getMouseX() - prefabPanStart.x;
						float deltaY = event.getMouseY() - prefabPanStart.y;

						float panScale = prefabOrbitDistance * 0.0025f;
						kVec3 right = pcam->calculateRight();
						kVec3 up = pcam->calculateUp();
						kVec3 offset = (right * deltaX + up * deltaY) * panScale;

						pcam->setPosition(prefabPanStartCamPos + offset);
						prefabOrbitPivot = prefabPanStartPivot + offset;
					}
				}
			}
			else if (eventType == K_EVENT_MOUSEWHEEL)
			{
				// --- World panel zoom ---
				if (panelWorld->enabled && panelWorld->hovered)
				{
					cameraTweenActive = false;
					float wheel = event.getMouseWheelY();
					cameraOrbitDistance = std::max(0.1f, cameraOrbitDistance - wheel * 2.0f);
					kVec3 fwd = cameraEditor->calculateForward();
					cameraEditor->setPosition(cameraOrbitPivot - fwd * cameraOrbitDistance);
				}

				// --- Prefab panel zoom ---
				if (panelPrefab->enabled && panelPrefab->hovered && manager->prefabCamera)
				{
					float wheel = event.getMouseWheelY();
					prefabOrbitDistance = std::max(0.1f, prefabOrbitDistance - wheel * 2.0f);
					kVec3 fwd = manager->prefabCamera->calculateForward();
					manager->prefabCamera->setPosition(prefabOrbitPivot - fwd * prefabOrbitDistance);
				}
			}
			else if (eventType == K_EVENT_KEYDOWN)
			{
				if (event.getKeyButton() == K_KEY_W)
				{
					if (panelWorld->enabled && panelWorld->hovered)
						manager->manipulatorType = ImGuizmo::TRANSLATE;
					else if (panelPrefab->enabled && panelPrefab->hovered)
						manager->prefabManipulatorType = ImGuizmo::TRANSLATE;
				}
				else if (event.getKeyButton() == K_KEY_E)
				{
					if (panelWorld->enabled && panelWorld->hovered)
						manager->manipulatorType = ImGuizmo::ROTATE;
					else if (panelPrefab->enabled && panelPrefab->hovered)
						manager->prefabManipulatorType = ImGuizmo::ROTATE;
				}
				else if (event.getKeyButton() == K_KEY_R)
				{
					if (panelWorld->enabled && panelWorld->hovered)
						manager->manipulatorType = ImGuizmo::SCALE;
					else if (panelPrefab->enabled && panelPrefab->hovered)
						manager->prefabManipulatorType = ImGuizmo::SCALE;
				}
				else if (event.getKeyButton() == K_KEY_LALT)
				{
					altPressed = true;
				}
				else if (event.getKeyButton() == K_KEY_LCTRL)
				{
					ctrlPressed = true;
				}
				else if (event.getKeyButton() == K_KEY_LSHIFT)
				{
					shiftPressed = true;
				}
				else if (event.getKeyButton() == K_KEY_DELETE)
				{
					// Only delete scene objects when the World or Hierarchy panel
					// has focus — other panels (script/shader editors, project)
					// handle Delete for their own selections in their draw().
					if (!gui->getWantTextInput() && manager->projectOpened &&
						(panelWorld->focused || panelHierarchy->focused))
						manager->deleteSelectedObjects();
				}
				else if (event.getKeyButton() == K_KEY_Z && ctrlPressed)
				{
					if (!gui->getWantTextInput())
						manager->undoRedo.undo();
				}
				else if (event.getKeyButton() == K_KEY_Y && ctrlPressed)
				{
					if (!gui->getWantTextInput())
						manager->undoRedo.redo();
				}
				else if (event.getKeyButton() == K_KEY_S && ctrlPressed)
				{
					// Ctrl+S routes to whichever editor has focus.
					// Fall back to the project (world) save for everything else
					// — covers World, Hierarchy, Inspector, Project, Console, etc.
					if (gui->getWantTextInput())
					{
						// User is typing — let the input field consume the keystroke.
					}
					else if (panelLogicGraph->focused)
						panelLogicGraph->saveCurrent();
					else if (panelShaderGraph->focused)
						panelShaderGraph->saveCurrent();
					else if (panelAnimator->focused)
						panelAnimator->saveCurrent();
					else if (panelAnimation->focused)
						panelAnimation->saveCurrent();
					else if (panelParticle->focused)
						panelParticle->saveCurrent();
					else if (manager->projectOpened)
						manager->saveWorld();
				}
				else if (event.getKeyButton() == K_KEY_D && ctrlPressed)
				{
					// Ctrl+D duplicates the current selection. Fires from the
					// World OR Hierarchy panel — anywhere a scene-object
					// selection is the active context — but stays out of the
					// script/shader editors and any text-input field.
					if (!gui->getWantTextInput() && manager->projectOpened &&
						(panelWorld->focused || panelHierarchy->focused))
						manager->duplicateSelectedObjects();
				}
				else if (event.getKeyButton() == K_KEY_F && !ctrlPressed)
				{
					// F frames the editor camera on the selected object and
					// sets the orbit pivot to its centre, so alt+drag from
					// now on rotates around it. Only fires from the World or
					// Hierarchy panel — anywhere else, F is free for shortcuts.
					if (!gui->getWantTextInput() && manager->projectOpened &&
						(panelWorld->focused || panelHierarchy->focused) &&
						!manager->selectedObjects.empty())
					{
						// Union AABB across all selected objects. Mesh nodes
						// contribute their world AABB; non-mesh nodes (lights,
						// cameras, empties) contribute a small bounding cube
						// around their position so they still pull the framing
						// rectangle without dominating it.
						kVec3 unionMin(std::numeric_limits<float>::infinity());
						kVec3 unionMax(-std::numeric_limits<float>::infinity());
						int count = 0;
						for (const kString &uuid : manager->selectedObjects)
						{
							kObject *o = manager->findObjectByUuid(uuid);
							if (!o)
								continue;
							if (o->getType() == NODE_TYPE_MESH)
							{
								kAABB box = ((kMesh *)o)->getWorldAABB();
								unionMin = glm::min(unionMin, box.min);
								unionMax = glm::max(unionMax, box.max);
							}
							else
							{
								kVec3 p = o->getGlobalPosition();
								unionMin = glm::min(unionMin, p - kVec3(0.25f));
								unionMax = glm::max(unionMax, p + kVec3(0.25f));
							}
							++count;
						}

						if (count > 0)
						{
							kVec3 target = (unionMin + unionMax) * 0.5f;
							float radius = glm::length((unionMax - unionMin) * 0.5f);
							if (radius < 0.5f)
								radius = 0.5f;

							float fovRad = glm::radians(cameraEditor->getFOV());
							float distance = (radius / std::sin(fovRad * 0.5f)) * 1.4f;

							// Keep the current viewing direction, but rebuild
							// the rotation cleanly from forward + world-up so
							// accumulated roll from previous drags is removed
							// — that's the "tilt after focus" the user saw.
							kVec3 forward = glm::normalize(cameraEditor->calculateForward());
							kVec3 worldUp = kVec3(0.0f, 1.0f, 0.0f);
							// If the user is staring straight up/down, give
							// quatLookAt a non-degenerate up vector.
							if (std::abs(glm::dot(forward, worldUp)) > 0.999f)
								worldUp = kVec3(0.0f, 0.0f, 1.0f);
							kQuat targetRot = glm::quatLookAt(forward, worldUp);
							kVec3 targetPos = target - forward * distance;

							// Start the tween — interpolation happens in the
							// per-frame block below.
							cameraTweenFromPos = cameraEditor->getPosition();
							cameraTweenToPos = targetPos;
							cameraTweenFromRot = cameraEditor->getRotation();
							cameraTweenToRot = targetRot;
							cameraTweenToPivot = target;
							cameraTweenToDist = distance;
							cameraTweenT = 0.0f;
							cameraTweenActive = true;

							// Update the look-at marker immediately so
							// other code (camera frustum debug, etc.) sees the
							// new focus point without waiting for the tween.
							cameraEditor->setLookAt(target);
						}
					}
				}
			}
			else if (eventType == K_EVENT_KEYUP)
			{
				if (event.getKeyButton() == K_KEY_LALT)
				{
					altPressed = false;
				}
				else if (event.getKeyButton() == K_KEY_LCTRL)
				{
					ctrlPressed = false;
				}
				else if (event.getKeyButton() == K_KEY_LSHIFT)
				{
					shiftPressed = false;
				}
			}
		}

		// (WASD camera movement removed — selection-driven framing via F is
		// the preferred navigation now. Alt+drag orbits, wheel zooms.)

		// Camera focus tween — F-frame-selected populates the from/to state
		// above; we advance the parameter here and write the interpolated
		// pose every frame. Smoothstep gives a soft start and stop.
		if (cameraTweenActive)
		{
			cameraTweenT += deltaTime / cameraTweenDuration;
			float u = std::min(cameraTweenT, 1.0f);
			float ease = u * u * (3.0f - 2.0f * u);

			kVec3 pos = glm::mix(cameraTweenFromPos, cameraTweenToPos, ease);
			kQuat rot = glm::slerp(cameraTweenFromRot, cameraTweenToRot, ease);
			cameraEditor->setPosition(pos);
			cameraEditor->setRotation(rot);

			if (cameraTweenT >= 1.0f)
			{
				cameraOrbitPivot = cameraTweenToPivot;
				cameraOrbitDistance = cameraTweenToDist;
				cameraTweenActive = false;
			}
		}

		// Safety: ensure world driver is current at the start of each frame.
		// kMesh::draw() uses kDriver::getCurrent(), which must match the
		// context that owns the meshes' VAOs.
		{
			kDriver *wd = rendererWorld->getDriver();
			if (wd)
			{
				wd->makeCurrent(window);
				kDriver::setCurrent(wd);
			}
		}

		renderer->clear();

		// PrefabPreview mode is now handled by its OWN renderer + world
		// (see the prefab rendering block below).  Only particle/animator
		// preview modes still swap the main viewport to the preview world.
		bool isPreviewMode = (manager->activeMode != Manager::EditorMode::GameWorld &&
		                      manager->activeMode != Manager::EditorMode::PrefabPreview);

		// Re-sync the local scene pointer from the manager. Manager::loadWorld
		// destroys the original game scene and creates new ones, so the local
		// here could otherwise dangle after a project open — which silently
		// produces garbage for things like scene->getShadowsEnabled() below.
		// When the pointer changes we also re-apply the default skybox so
		// the editor view doesn't end up empty, and re-arm the shadow
		// allocator in case it skipped earlier.
		{
			static kScene *lastSyncedScene = nullptr;
			if (manager->getScene() != nullptr)
			{
				scene = manager->getScene();
				if (scene != lastSyncedScene)
				{
					if (scene->getSkyboxMaterial() == nullptr)
						manager->applyDefaultSkybox(scene);
					renderer->setEnableShadow(scene->getShadowsEnabled());
		
				// In preview mode (particle/animator), swap the render target to the preview world.
				// PrefabPreview no longer uses this path — it renders via its own kRenderer below.
				if (isPreviewMode && manager->previewWorld)
				{
					world = manager->previewWorld;
					scene = manager->getActiveScene();
					// Use preview camera for the editor camera during preview.
					if (manager->previewCamera)
						cameraEditor = manager->previewCamera;
				}
				else if (!isPreviewMode)
				{
					// Restore game world pointers.
					world = manager->getWorld();
					scene = manager->getScene();
					cameraEditor = manager->editorCamera;
				}
					renderer->setShadowBias(scene->getShadowBias());
					renderer->setShadowNormalBias(scene->getShadowNormalBias());
					renderer->setShadowSoftness(scene->getShadowSoftness());
					// Resolution allocates the shadow texture, so only push it
					// on scene change (not every frame).
					if (renderer->getShadowResolution() != scene->getShadowMapResolution())
						renderer->setShadowResolution(scene->getShadowMapResolution());
					lastSyncedScene = scene;
				}
			}
		}

		// The World panel always renders the main scene from the editor camera.
		// The prefab editor (below) uses its OWN renderer, so opening it never
		// disturbs this view.
		world->setMainCamera(cameraEditor);

		int viewportW = panelWorld->width;
		int viewportH = panelWorld->height;

		// Fix aspect ratio
		if (viewportW > 0 && viewportH > 0)
		{
			// Pass 0 as deltaTime when paused so physics/animations freeze
			float gameDt = panelGame->getEffectiveDeltaTime(deltaTime);

			// Game-specific logic — only in GameWorld mode.
			if (!isPreviewMode)
			{
				// Physics only ticks while Playing (not Paused, not Stopped, never
				// during prefab editing). gameDt is already 0 when Paused, but we
				// also gate on play state so a Stopped editor session never builds
				// momentum or does collision callbacks.
				if (panelGame->getPlayState() == GamePlayState::Playing &&
					!manager->prefabEditing && gameDt > 0.0f)
				{
					manager->stepPhysics(gameDt);
					// Advance named input so scripts see fresh getAction()/getAxis() state.
					manager->stepInput();
					// Dispatch FixedUpdate() then Update()/LateUpdate() to scripts.
					world->fixedUpdateScripts(gameDt);
					world->updateScripts(gameDt);
				}

				// Advance .animator controllers (no-op when stopped; frozen
				// when paused because gameDt is 0).
				manager->stepAnimators(gameDt);

				// While stopped, watch script source files and recompile on save.
				if (panelGame->getPlayState() == GamePlayState::Stopped)
					manager->pollScriptChanges(deltaTime);

				// Mirror the active scene's shadow toggle into the renderer.
				// setEnableShadow is lazy/idempotent in the SDK so this is cheap.
				renderer->setEnableShadow(scene ? scene->getShadowsEnabled() : true);
				if (scene)
				{
					renderer->setShadowBias(scene->getShadowBias());
					renderer->setShadowNormalBias(scene->getShadowNormalBias());
					renderer->setShadowSoftness(scene->getShadowSoftness());
					if (renderer->getShadowResolution() != scene->getShadowMapResolution())
						renderer->setShadowResolution(scene->getShadowMapResolution());
				}
			}
			else
			{
				// Preview mode: enable shadows on the preview scene.
				if (scene)
					renderer->setEnableShadow(scene->getShadowsEnabled());
			}

			renderer->render(world, scene, 0, 0, viewportW * 2, viewportH * 2, gameDt, false);

			// Editor scene (grid) only in GameWorld mode.
			if (!isPreviewMode)
			{
				kRenderMode savedMode = renderer->getRenderMode();
				renderer->setRenderMode(kRenderMode::RENDER_MODE_FULL);
				renderer->render(world, sceneEditor, 0, 0, viewportW * 2, viewportH * 2, deltaTime, false);
				renderer->setRenderMode(savedMode);
			}

			// Picking pass — only in GameWorld mode.
			if (!isPreviewMode)
				renderer->renderPickingPass(world, scene, viewportW * 2, viewportH * 2);

			// Outline / debug shapes — only in GameWorld mode.
			if (!isPreviewMode)
			{
				// Outline selected objects (orange)
				if (manager->projectOpened && !manager->selectedObjects.empty())
					renderer->renderOutline(world, scene, manager->selectedObjects,
											kVec4(1.0f, 0.55f, 0.0f, 1.0f), 3.0f);

				// Drag-hover outline (yellow)
				if (manager->projectOpened && !manager->dragHoverObjectUuid.empty())
				{
					std::vector<kString> hoverList = {manager->dragHoverObjectUuid};
					renderer->renderOutline(world, scene, hoverList,
											kVec4(1.0f, 0.85f, 0.0f, 0.85f), 3.0f);
				}

				// Debug shapes for selected lights and cameras
				if (manager->projectOpened && !manager->selectedObjects.empty())
					renderer->renderDebugShapes(world, scene, manager->selectedObjects);

				// Octree debug visualization
				if (manager->projectOpened)
					renderer->renderOctreeDebug(world, scene);
			}

			// Nav-mesh wireframe (blue) for a selected, baked navigation object.
			if (manager->projectOpened && manager->selectedObject &&
				manager->selectedObject->getHasNavMeshDesc())
			{
				kNavMesh *nav = manager->getBakedNavMesh(manager->selectedObject);
				if (nav && nav->isBaked())
				{
					std::vector<kVec3> navLines;
					nav->getDebugLines(navLines);
					renderer->renderDebugLines(world, navLines, kVec3(0.25f, 0.55f, 1.0f));
				}
			}

			// Thumbnail generation (one per frame, main thread only)
			if (manager->projectOpened)
			{
				manager->processThumbnailQueue(panelConsole);
				// Rebuild scene instances of any re-imported mesh (deferred from
				// the inspector's Apply so teardown never happens mid panel-draw).
				manager->processPendingMeshReloads();
			}
		}

		// --- Prefab panel rendering (separate kRenderer, separate kWorld) ---
		// The prefab editor uses its OWN kRenderer with its OWN kDriver.  We
		// switch to the prefab driver before rendering and restore the world
		// driver afterwards so ImGui draws with the correct context.
		if (manager->prefabEditing && manager->prefabScene && manager->prefabCamera &&
			manager->prefabWorld && manager->prefabRenderer &&
			panelPrefab->width > 0 && panelPrefab->height > 0)
		{
			// Switch to the prefab renderer's driver and make its GL context current.
			kDriver *prefabDriver = manager->prefabRenderer->getDriver();
			kDriver *worldDriver  = rendererWorld->getDriver();
			if (prefabDriver && worldDriver && prefabDriver != worldDriver)
			{
				prefabDriver->makeCurrent(window);
				kDriver::setCurrent(prefabDriver);
			}

			int pw = panelPrefab->width * 2;
			int ph = panelPrefab->height * 2;

			manager->prefabRenderer->clear();

			// Render the isolated prefab scene (game content).
			manager->prefabWorld->setMainCamera(manager->prefabCamera);
			manager->prefabRenderer->render(manager->prefabWorld, manager->prefabScene,
			                                0, 0, pw, ph, deltaTime, false);

			// Render the duplicated editor grid scene on top.
			if (manager->prefabEditorScene)
			{
				kRenderMode savedMode = manager->prefabRenderer->getRenderMode();
				manager->prefabRenderer->setRenderMode(kRenderMode::RENDER_MODE_FULL);
				manager->prefabRenderer->render(manager->prefabWorld, manager->prefabEditorScene,
				                                0, 0, pw, ph, deltaTime, false);
				manager->prefabRenderer->setRenderMode(savedMode);
			}

			// Picking pass for the prefab panel (enables click-to-select).
			manager->prefabRenderer->renderPickingPass(
				manager->prefabWorld, manager->prefabScene, pw, ph);

			// Restore the world renderer's driver so subsequent ImGui and swap
			// operations use the correct context.
			if (prefabDriver && worldDriver && prefabDriver != worldDriver)
			{
				worldDriver->makeCurrent(window);
				kDriver::setCurrent(worldDriver);
			}
		}

		// std::cout << panelWorld->width << "," << panelWorld->height << std::endl;

		// Safety: ensure the world renderer's driver is current before any
		// ImGui panel draws (some panels create/use kOffscreenRenderer which
		// captures kDriver::getCurrent() at construction time).
		{
			kDriver *wd = rendererWorld->getDriver();
			if (wd)
			{
				wd->makeCurrent(window);
				kDriver::setCurrent(wd);
			}
		}

		gui->canvasStart();
		gui->dockSpaceStart("MainDockSpace");

		mainmenu->draw(window, showPanel);

		manager->shaderPreview.active = showPanel.shaderEditor;

		// The World panel stays visible even while the prefab editor is open —
		// the two now render to separate targets.
		bool worldVisible = showPanel.world;
		panelWorld->draw(worldVisible, renderer, cameraEditor);
		panelInspector->draw(showPanel.inspector);
		panelHierarchy->draw(showPanel.hierarchy);
		panelProject->draw(showPanel.project);
		panelConsole->draw(showPanel.console);
		panelShaderGraph->draw(showPanel.shaderEditor);
		panelLogicGraph->draw(showPanel.scriptEditor);
		panelGame->draw(showPanel.game);
		panelPrefab->draw(showPanel.prefab);
		// Consume pending Cinematic Editor open request from inspector
		if (manager->pendingOpenAnimationEditor)
		{
			showPanel.animationEditor = true;
			manager->pendingOpenAnimationEditor = false;
		}

		panelAnimator->draw(showPanel.animatorEditor);
		panelAnimation->draw(showPanel.animationEditor);
		panelParticle->draw(showPanel.particleEditor);

		// Track which panel was last focused to drive the hierarchy panel.
		// When the world panel is focused, the hierarchy shows the game world's
		// scene graph.  When the prefab panel is focused, it shows the prefab's
		// isolated scene graph.
		{
			bool oldPrefabFocus = manager->hierarchyShowsPrefab;
			if (panelPrefab->enabled && panelPrefab->focused)
				manager->hierarchyShowsPrefab = true;
			else if (panelWorld->enabled && panelWorld->focused)
				manager->hierarchyShowsPrefab = false;
			// If focus changed, rebuild the hierarchy tree.
			if (manager->hierarchyShowsPrefab != oldPrefabFocus)
				panelHierarchy->refreshList();
		}

		// Track the last focused panel to drive what the Inspector displays.
		// The Inspector reads this on the next frame, so whichever panel the
		// user interacted with most recently becomes the active context.
		{
			if (panelProject->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Project;
			else if (panelHierarchy->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Hierarchy;
			else if (panelWorld->enabled && panelWorld->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Scene;
			else if (panelLogicGraph->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Logic;
			else if (panelAnimator->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Animator;
			else if (panelParticle->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Particle;
			else if (panelShaderGraph->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Shader;
			else if (panelAnimation->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Animation;
		}

		// If there's a need to import assets
		manager->drawImportPopup(panelConsole);
		if (manager->showImportPopup)
			gui->openPopup("Importing Assets...");

		gui->dockSpaceEnd();

		if (showSplashScreen)
		{
			splashScreen->show();
			showSplashScreen = false;
		}
		if (splashScreen->isOpen())
			splashScreen->draw();

		mainmenu->drawAbout();

		manager->drawNewProjectDialog();

		gui->canvasEnd();

		window->swap();
	}

	// Clean up
	delete panelAnimation;
	delete panelParticle;
	delete panelAnimator;
	delete panelTerrain;
	delete splashScreen;
	delete manager;         // shuts down persistent audio engine
	gui->destroy();
	rendererWorld->destroy();
	rendererPrefab->destroy();
	window->destroy();
	return 0;
}
