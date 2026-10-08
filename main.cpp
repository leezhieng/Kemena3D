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
#include "panel_gui.h"
#include "splash_screen.h"
#include "crashhandler.h"

#include <cstring>

#include "imgui_internal.h" // <-- required for ImGuiSettingsHandler

using namespace kemena;

const kString windowTitle = "Kemena3D";

// Project config
kString projectName = "New Game";
kString developerName = "My Company";
kString projectVersion = "0.0.1";

int main()
{
	// Install the crash reporter before anything else so an early fault still
	// produces Kemena3D_crash.log (the Release GUI build has no console).
	installCrashHandler();

	{
		std::ofstream f("D:\\Projects\\Kemena3D\\animator_debug.log", std::ios::app);
		if (f.is_open())
			f << "[startup] Kemena3D launched" << std::endl;
	}

	// Create window and renderers.
	// Three separate kRenderer instances are created:
	//   rendererWorld  — renders the game scene into the World panel viewport
	//   rendererPrefab — renders the isolated prefab scene into the Prefab panel
	//   rendererGame   — renders the game view (game camera) into the Game panel
	// Each owns its own kDriver (OpenGL context) with shared resources so
	// textures from one context can be used as ImGui images in another.
	kWindow *window = createWindow(1024, 768, windowTitle, true);

	kRenderer *rendererWorld = createRenderer(window);
	rendererWorld->setEnableScreenBuffer(true);
	rendererWorld->setEnableShadow(true);
	rendererWorld->setEnableObjectPicking(true);
	rendererWorld->setClearColor(kVec4(0.2f, 0.2f, 0.2f, 1.0f));
	// Editor viewport: draw the camera/light/audio/decal gizmo billboards.
	rendererWorld->setEditorGizmosEnabled(true);

	kRenderer *rendererPrefab = createRenderer(window);
	rendererPrefab->setEnableScreenBuffer(true);
	rendererPrefab->setEnableShadow(true);
	rendererPrefab->setEnableObjectPicking(true);
	rendererPrefab->setClearColor(kVec4(0.2f, 0.2f, 0.2f, 1.0f));
	// Editor viewport (prefab): gizmo billboards are editor scaffolding too.
	rendererPrefab->setEditorGizmosEnabled(true);

	// Dedicated renderer for the in-editor Game panel. The game view shares the
	// world/scene with the World panel but renders from the game camera, so it
	// needs its own kRenderer (see Manager::gameRenderer). It SHARES
	// rendererWorld's context because mesh VAOs are context-local — a separate
	// context could not draw the main scene's meshes. Screen buffer gives us
	// getFboTexture() for the ImGui panel image; shadows match the editor.
	kRenderer *rendererGame = createRendererSharedContext(window, rendererWorld->getDriver());
	rendererGame->setEnableScreenBuffer(true);
	rendererGame->setEnableShadow(true);
	rendererGame->setClearColor(kVec4(0.0f, 0.0f, 0.0f, 1.0f));

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

	// Assign the game panel's dedicated renderer so the main loop can render
	// the game viewport (through the game camera) independently of the World
	// panel, which uses the editor camera on rendererWorld.
	manager->gameRenderer = rendererGame;

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
	PanelGui *panelGui = new PanelGui(gui, manager);
	manager->panelGui = panelGui;
	PanelPrefab *panelPrefab = new PanelPrefab(gui, manager);

	// Route file double-clicks from the project panel to the editor panel that
	// owns that file type: show the panel if it is hidden and record it in
	// `pendingFocusWindow` so the render loop brings it to the front (via
	// ImGui::SetNextWindowFocus) right before it is drawn. Generic asset types
	// with no dedicated editor surface (mesh, image, audio, ...) are previewed
	// by the Inspector, so they focus that instead.
	panelProject->onFileDoubleClicked = [&](const std::string &path)
	{
		auto endsWith = [&path](const char *ext) -> bool
		{
			const size_t n = std::strlen(ext);
			return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
		};

		if (endsWith(".shader"))
		{
			showPanel.shaderEditor = true;
			panelShaderGraph->openFile(path);
			pendingFocusWindow = "Shader";
		}
		else if (endsWith(".logic"))
		{
			showPanel.scriptEditor = true;
			panelLogicGraph->openFile(path);
			pendingFocusWindow = "Logic Graph";
		}
		else if (endsWith(".prefab") || endsWith(".pfb"))
		{
			showPanel.prefab = true;
			manager->setEditorMode(Manager::EditorMode::PrefabPreview, path, ".prefab");
			manager->editPrefab(path);
			showPanel.prefab = manager->prefabEditing;
			if (showPanel.prefab)
				pendingFocusWindow = "Prefab";
		}
		else if (endsWith(".animator"))
		{
			// The Animator Editor has its own panel; opening an .animator must
			// not hijack the World/Scene viewport with an animator preview.
			showPanel.animatorEditor = true;
			panelAnimator->openFile(path);
			pendingFocusWindow = "AnimatorEditor";
		}
		else if (endsWith(".cinematic"))
		{
			showPanel.animationEditor = true;
			panelAnimation->openFile(path);
			pendingFocusWindow = "CinematicEditor";
		}
		else if (endsWith(".particle"))
		{
			showPanel.particleEditor = true;
			panelParticle->openFile(path);
			manager->setEditorMode(Manager::EditorMode::ParticlePreview, path, ".particle");
			pendingFocusWindow = "Particle Editor";
		}
		else if (endsWith(".ui"))
		{
			showPanel.guiEditor = true;
			panelGui->openFile(path);
			pendingFocusWindow = "IngameUI";
		}
		else if (endsWith(".world"))
		{
			// Load the world into the viewport, then show and focus the World panel.
			manager->loadWorld(path);
			showPanel.world = true;
			pendingFocusWindow = "World";
		}
		else
		{
			// No dedicated editor for this type — the Inspector previews it.
			showPanel.inspector = true;
			pendingFocusWindow = "Inspector";
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

	// Mouse-driven camera navigation is editor-only: the World panel steers the
	// editor camera and the Prefab panel steers its preview camera. The Game
	// panel's camera is gameplay-owned and must not be moved by Alt+click.
	// The prefab preview camera's orbit pivot/distance live on the Manager so
	// the Prefab panel's Preview Camera settings can read and persist them.

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

		// Drain EVERY pending SDL event this frame. Processing only a single
		// event per frame (and forwarding the previous frame's event to ImGui)
		// lets the SDL queue back up under high-rate input such as mouse
		// motion. Once that queue overflows SDL drops events, and a dropped
		// KEYUP leaves the key stuck "down" in SDL_GetKeyboardState() — which
		// is what kInputManager polls — so getAction()/getAxis() never clear
		// and the character keeps running in the last direction while other
		// keys appear dead. Draining the whole queue also delivers input to
		// ImGui on the same frame it arrives.
		while (event.hasEvent())
		{
			gui->processEvent(event);

			int eventType = event.getType();

			if (eventType == K_EVENT_QUIT)
			{
				manager->closeEditor();
			}
			else if (eventType == SDL_EVENT_WINDOW_FOCUS_GAINED)
			{
				// Drop any key state that latched while we were unfocused, so a
				// key whose KEYUP was delivered elsewhere cannot keep an action
				// "held" once we are interacting again.
				SDL_ResetKeyboard();
				altPressed   = false;
				ctrlPressed  = false;
				shiftPressed = false;

				// Check asset changes
				if (manager->projectOpened && !manager->showImportPopup)
				{
					manager->checkAssetChange();
				}
			}
			else if (eventType == SDL_EVENT_WINDOW_FOCUS_LOST)
			{
				// When the window loses focus (alt-tab, clicking another app) the
				// OS stops delivering key events to us, so a KEYUP for a key that
				// was held at that moment is never seen. SDL_GetKeyboardState() —
				// which kInputManager polls — would keep reporting that key as
				// held, and gameplay would latch getAction()/getAxis() on forever
				// (the character keeps running and never stops after the key is
				// released). Clear SDL's keyboard state and our modifier tracking
				// so input is clean when focus returns.
				SDL_ResetKeyboard();
				altPressed   = false;
				ctrlPressed  = false;
				shiftPressed = false;
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
					// Screen-space cursor position (same coordinate space as
					// panelPos). overViewport is true only when the cursor is
					// actually inside the rendered scene image, so clicking the
					// toolbar options above the viewport does not clear the
					// current selection.
					kVec2 wMouse = gui->getMousePos();
					bool overViewport =
						wMouse.x >= panelWorld->panelPos.x &&
						wMouse.x <= panelWorld->panelPos.x + (float)panelWorld->width &&
						wMouse.y >= panelWorld->panelPos.y &&
						wMouse.y <= panelWorld->panelPos.y + (float)panelWorld->height;

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
					else if (event.getMouseButton() == K_MOUSEBUTTON_LEFT && !altPressed && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() && overViewport)
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
							prefabPanStartPivot = manager->prefabOrbitPivot;
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
						pcam->setPosition(manager->prefabOrbitPivot - fwd * manager->prefabOrbitDistance);
					}
					else if (prefabPanning)
					{
						float deltaX = event.getMouseX() - prefabPanStart.x;
						float deltaY = event.getMouseY() - prefabPanStart.y;

						float panScale = manager->prefabOrbitDistance * 0.0025f;
						kVec3 right = pcam->calculateRight();
						kVec3 up = pcam->calculateUp();
						kVec3 offset = (right * deltaX + up * deltaY) * panScale;

						pcam->setPosition(prefabPanStartCamPos + offset);
						manager->prefabOrbitPivot = prefabPanStartPivot + offset;
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
					manager->prefabOrbitDistance = std::max(0.1f, manager->prefabOrbitDistance - wheel * 2.0f);
					// Keep the persisted Preview Camera setting in step with the
					// interactive zoom so the panel slider reflects it.
					manager->prefabSceneSettings.camOrbitDistance = manager->prefabOrbitDistance;
					kVec3 fwd = manager->prefabCamera->calculateForward();
					manager->prefabCamera->setPosition(manager->prefabOrbitPivot - fwd * manager->prefabOrbitDistance);
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
					else if (panelGui->focused)
						panelGui->saveCurrent();
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
					renderer->setShadowNormalOffset(scene->getShadowNormalOffset());
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

		// Pass 0 as deltaTime when paused so physics/animations freeze.
		float gameDt = panelGame->getEffectiveDeltaTime(deltaTime);

		// Game-specific logic — runs regardless of the World panel's viewport
		// size so Play keeps simulating when the World panel is hidden and the
		// user drives the game from the Game panel alone (which has its own
		// renderer). Physics/scripts are also allowed during prefab editing:
		// the prefab editor operates on a fully isolated world.
		if (!isPreviewMode)
		{
			// Physics/scripts tick while Playing (not Paused/Stopped). gameDt is
			// already 0 when Paused, but we also gate on play state so a Stopped
			// editor session never builds momentum or does collision callbacks.
			if (panelGame->getPlayState() == GamePlayState::Playing && gameDt > 0.0f)
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
				renderer->setShadowNormalOffset(scene->getShadowNormalOffset());
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

		// World-panel viewport render (only when the panel has a valid size).
		if (viewportW > 0 && viewportH > 0)
		{
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

				// Decal projection volume (cyan) for selected decals, so the
				// direction / distance / size are visible while authoring.
				if (manager->projectOpened && !manager->selectedObjects.empty())
				{
					std::vector<kVec3> decalLines;
					for (const kString &uuid : manager->selectedObjects)
					{
						kObject *o = manager->findObjectByUuid(uuid);
						if (o != nullptr && o->getType() == kNodeType::NODE_TYPE_DECAL)
						{
							kDecal *decal = static_cast<kDecal *>(o);
							decal->calculateModelMatrix();
							decal->appendProjectionDebugLines(decalLines);
						}
					}
					if (!decalLines.empty())
						renderer->renderDebugLines(world, decalLines, kVec3(0.15f, 0.9f, 1.0f));
				}

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

		// --- Game panel rendering (dedicated kRenderer) ---------------------
		// The game view shares the world/scene with the World panel but renders
		// from the game camera, so it uses Manager::gameRenderer (its own FBO and
		// driver). Switch to its driver, render through PanelGame::renderGame()
		// (which promotes the game camera to the world's main camera for the
		// pass), then restore the world driver before ImGui draws the panel image.
		if (showPanel.game && manager->gameRenderer && panelGame->width > 0 && panelGame->height > 0)
		{
			// gameRenderer shares rendererWorld's context/driver, so no context
			// switch is needed — just make sure the world driver is current.
			kDriver *worldDriver = rendererWorld->getDriver();
			if (worldDriver)
			{
				worldDriver->makeCurrent(window);
				kDriver::setCurrent(worldDriver);
			}

			panelGame->renderGame(panelGame->width, panelGame->height, deltaTime);
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

		// ImGuizmo must be told a frame has begun exactly once per frame, before
		// any panel calls Manipulate(). Both the World and Prefab panels host a
		// gizmo; calling BeginFrame() from each of them in the same frame made the
		// later call overwrite the earlier panel's "gizmo hovered" snapshot, so
		// clicking the Prefab gizmo registered as a viewport click and deselected
		// the object instead of starting a drag. One call here fixes that.
		ImGuizmo::BeginFrame();

		gui->dockSpaceStart("MainDockSpace");

		mainmenu->draw(window, showPanel);

		manager->shaderPreview.active = showPanel.shaderEditor;

		// Bring a panel opened from the project panel (see onFileDoubleClicked)
		// to the front. SetNextWindowFocus targets the very next Begin(), so it
		// must be called immediately before the owning panel's draw — and only
		// when that panel is actually visible, or it would focus the wrong
		// window. `pendingFocusWindow` is cleared once consumed, so panels drawn
		// earlier in the frame are focused on the following frame.
		auto applyPendingFocus = [&](const char *windowId, bool visible)
		{
			if (visible && !pendingFocusWindow.empty() && pendingFocusWindow == windowId)
			{
				ImGui::SetNextWindowFocus();
				pendingFocusWindow.clear();
			}
		};

		// The World panel stays visible even while the prefab editor is open —
		// the two now render to separate targets.
		bool worldVisible = showPanel.world;
		applyPendingFocus("World", worldVisible);
		panelWorld->draw(worldVisible, renderer, cameraEditor);
		applyPendingFocus("Inspector", showPanel.inspector);
		panelInspector->draw(showPanel.inspector);
		panelHierarchy->draw(showPanel.hierarchy);
		panelProject->draw(showPanel.project);
		panelConsole->draw(showPanel.console);
		applyPendingFocus("Shader", showPanel.shaderEditor);
		panelShaderGraph->draw(showPanel.shaderEditor);
		applyPendingFocus("Logic Graph", showPanel.scriptEditor);
		panelLogicGraph->draw(showPanel.scriptEditor);
		panelGame->draw(showPanel.game);
		applyPendingFocus("Prefab", showPanel.prefab);
		panelPrefab->draw(showPanel.prefab);
		// Consume pending Cinematic Editor open request from inspector
		if (manager->pendingOpenAnimationEditor)
		{
			showPanel.animationEditor = true;
			manager->pendingOpenAnimationEditor = false;
		}

		applyPendingFocus("AnimatorEditor", showPanel.animatorEditor);
		panelAnimator->draw(showPanel.animatorEditor);
		applyPendingFocus("CinematicEditor", showPanel.animationEditor);
		panelAnimation->draw(showPanel.animationEditor);
		applyPendingFocus("Particle Editor", showPanel.particleEditor);
		panelParticle->draw(showPanel.particleEditor);
		applyPendingFocus("IngameUI", showPanel.guiEditor);
		panelGui->draw(showPanel.guiEditor);

		// Track the last focused panel to drive what the Inspector displays and
		// which world the Hierarchy edits. The Inspector reads this on the next
		// frame, so whichever panel the user interacted with most recently
		// becomes the active context. The flags are only trusted while the
		// owning panel is actually visible (a hidden panel clears its own
		// focused flag in its draw(), but the visibility check makes this
		// explicit and avoids stale state).
		{
			if (panelProject->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Project;
			else if (panelHierarchy->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Hierarchy;
			else if (panelPrefab->enabled && showPanel.prefab && panelPrefab->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Prefab;
			else if (panelWorld->enabled && showPanel.world && panelWorld->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Scene;
			else if (panelGame->focused)
				// The Game panel is another view of the game world, so focusing
				// it selects the same Hierarchy context as the World panel.
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
			else if (panelGui->focused)
				manager->lastFocusedPanel = Manager::FocusedPanel::Gui;
		}

		// Drive the Hierarchy's editing context from the *sticky* last-focused
		// panel rather than the transient per-frame focus flags. This makes the
		// switch reliable: it follows the Prefab viewport while that panel is
		// the active context, returns to the game world as soon as the World
		// viewport takes focus, and stays put when the user clicks the Hierarchy
		// or Inspector (so prefab children can be selected and edited without
		// the tree snapping back to the world). Any other panel also holds the
		// current context. Once prefab editing ends the hierarchy always shows
		// the game world again.
		{
			bool oldPrefabFocus = manager->hierarchyShowsPrefab;
			if (!manager->prefabEditing)
				manager->hierarchyShowsPrefab = false;
			else if (manager->lastFocusedPanel == Manager::FocusedPanel::Prefab)
				manager->hierarchyShowsPrefab = true;
			else if (manager->lastFocusedPanel == Manager::FocusedPanel::Scene)
				manager->hierarchyShowsPrefab = false;
			// If the context changed, rebuild the hierarchy tree.
			if (manager->hierarchyShowsPrefab != oldPrefabFocus)
				panelHierarchy->refreshList();
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
	delete panelGui;
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
