#include "panel_hierarchy.h"
#include "panel_project.h"
#include "commands.h"

using namespace kemena;

PanelHierarchy::PanelHierarchy(kGuiManager *setGuiManager, Manager *setManager, kAssetManager *assetManager, kWorld *setWorld)
	: root("World", "world", 0, "world")
{
	gui = setGuiManager;
	manager = setManager;
	manager->panelHierarchy = this;

	kTexture2D *tex_add = assetManager->loadTexture2DFromResource("ICON_ADD_ROUND_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconAdd = tex_add->getTextureID();

	kTexture2D *tex_mag = assetManager->loadTexture2DFromResource("ICON_MAGNIFIER_LABEL", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconMag = tex_mag->getTextureID();

	// Object type icons
	kTexture2D *tex_world = assetManager->loadTexture2DFromResource("ICON_OBJECT_WORLD", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconWorld = tex_world->getTextureID();

	kTexture2D *tex_scene = assetManager->loadTexture2DFromResource("ICON_OBJECT_SCENE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconScene = tex_scene->getTextureID();

	kTexture2D *tex_mesh = assetManager->loadTexture2DFromResource("ICON_OBJECT_MESH", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconMesh = tex_mesh->getTextureID();

	kTexture2D *tex_empty = assetManager->loadTexture2DFromResource("ICON_OBJECT_EMPTY", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconEmpty = tex_empty->getTextureID();

	kTexture2D *tex_light = assetManager->loadTexture2DFromResource("ICON_OBJECT_LIGHT", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconLight = tex_light->getTextureID();

	kTexture2D *tex_camera = assetManager->loadTexture2DFromResource("ICON_OBJECT_CAMERA", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconCamera = tex_camera->getTextureID();

	kTexture2D *tex_audio = assetManager->loadTexture2DFromResource("ICON_OBJECT_AUDIO", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconAudio = tex_audio->getTextureID();

	kTexture2D *tex_audio_listener = assetManager->loadTexture2DFromResource("ICON_OBJECT_AUDIO_LISTENER", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconAudioListener = tex_audio_listener->getTextureID();

	kTexture2D *tex_prefab = assetManager->loadTexture2DFromResource("ICON_OBJECT_PREFAB", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconPrefab = tex_prefab->getTextureID();

	kTexture2D *tex_prefab_edited = assetManager->loadTexture2DFromResource("ICON_OBJECT_PREFAB_EDITED", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconPrefabEdited = tex_prefab_edited->getTextureID();

	kTexture2D *tex_terrain = assetManager->loadTexture2DFromResource("ICON_OBJECT_TERRAIN", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconTerrain = tex_terrain->getTextureID();

	world = setWorld;

	root = Node("World", "world", iconWorld, "world");
}

void PanelHierarchy::deselectAll(Node &root)
{
	root.isSelected = false;
	for (auto &child : root.children)
	{
		deselectAll(*child);
	}
}

void PanelHierarchy::drawNode(Node &node, Node &root, int level)
{
	// Sync selection state from the active panel's selection.
	auto &activeSel = manager->getActiveSelectedObjects();
	node.isSelected = std::find(activeSel.begin(), activeSel.end(),
	                             node.uuid) != activeSel.end();

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (node.isSelected)
		flags |= ImGuiTreeNodeFlags_Selected;
	if (node.children.empty())
		flags |= ImGuiTreeNodeFlags_Leaf;

	// Only expand world and scene by default
	if (level <= 1)
	{
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}

	// Dirty prefab instance roots show a different icon (cube-orange) AND
	// orange text to indicate non-transform changes (e.g. active/static).
	uint32_t drawIcon = node.icon;
	bool isPrefabRoot = false;
	if (!node.isPrefabDescendant && node.type == "prefab")
	{
		// Check if this node corresponds to a dirty prefab instance.
		if (manager->objectMap.count(node.uuid))
		{
			kObject *obj = manager->objectMap[node.uuid].object;
			if (obj && !obj->getPrefabRef().empty() && manager->isPrefabDirty(obj))
			{
				drawIcon = iconPrefabEdited;
				isPrefabRoot = true;
			}
		}
	}

	// Draw the icon
	gui->image(drawIcon, kVec2(16, 16));

	gui->sameLine();

	// Prefab descendants render greyed-out and don't accept clicks/drag-drop;
	// structural changes inside a prefab go through the prefab editor.
	if (node.isPrefabDescendant)
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

	if (isPrefabRoot)
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.0f, 1.0f)); // yellow/orange

	bool nodeOpen = gui->treeStartEx(node.uuid, node.name, flags);

	if (node.isPrefabDescendant || isPrefabRoot)
		ImGui::PopStyleColor();

	// In prefab mode the hierarchy is flattened (no World/Scene wrapper),
	// so objects start at level 1 instead of level 2.
	int  objectStartLevel = manager->hierarchyShowsPrefab ? 1 : 2;
	bool isObjectRow = (level >= objectStartLevel);

	// Drag source: only for real scene objects, never prefab descendants.
	if (isObjectRow && !node.isPrefabDescendant &&
		ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		ImGui::SetDragDropPayload("SCENE_OBJECT",
								  node.uuid.c_str(), node.uuid.size() + 1);
		ImGui::TextUnformatted(node.name.c_str());
		ImGui::EndDragDropSource();
	}

	// Drop target: split the row vertically into a top "drop-before"
	// (reorder as preceding sibling) and a bottom "drop-as-child" (reparent).
	// We use AcceptBeforeDelivery so the payload is accessible during hover,
	// letting us draw a custom indicator (line for drop-before, rect for
	// drop-as-child) rather than ImGui's default highlight.
	if (!node.isPrefabDescendant && ImGui::BeginDragDropTarget())
	{
		ImVec2 rmin = ImGui::GetItemRectMin();
		ImVec2 rmax = ImGui::GetItemRectMax();
		float midY = (rmin.y + rmax.y) * 0.5f;
		bool dropBefore = isObjectRow && (ImGui::GetMousePos().y < midY);

		const ImGuiDragDropFlags hoverFlags =
			ImGuiDragDropFlags_AcceptBeforeDelivery |
			ImGuiDragDropFlags_AcceptNoDrawDefaultRect;

		const ImGuiPayload *pProj = ImGui::AcceptDragDropPayload("PROJECT_ASSET", hoverFlags);
		const ImGuiPayload *pScn = ImGui::AcceptDragDropPayload("SCENE_OBJECT", hoverFlags);
		const ImGuiPayload *p = pProj ? pProj : pScn;

		if (p)
		{
			// --- Visual indicator -----------------------------------------
			ImU32 col = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
			if (dropBefore)
			{
				// Thin horizontal line above the row → "insert as previous sibling".
				ImGui::GetForegroundDrawList()->AddLine(
					ImVec2(rmin.x, rmin.y), ImVec2(rmax.x, rmin.y),
					col, 2.0f);
			}
			else
			{
				// Outlined rectangle around the row → "drop as child".
				ImGui::GetForegroundDrawList()->AddRect(
					rmin, rmax, col, 0.0f, 0, 2.0f);
			}

			// --- Delivery -------------------------------------------------
			if (p->IsDelivery())
			{
				if (pProj)
				{
					kString assetUuid((const char *)p->Data);
					// Multi-file project drags pack newline-separated UUIDs; spawn the first.
					{
						auto nl = assetUuid.find('\n');
						if (nl != kString::npos)
							assetUuid = assetUuid.substr(0, nl);
					}
					kObject *spawned = manager->instantiateAssetFromUuid(assetUuid);
					if (spawned && isObjectRow)
					{
						if (dropBefore && manager->objectMap.count(node.uuid))
						{
							kObject *target = manager->objectMap[node.uuid].object;
							kObject *targetParent = target ? target->getParent() : nullptr;
							kString parentUuid = (targetParent && targetParent != manager->getScene()->getRootNode())
													 ? targetParent->getUuid()
													 : kString("");
							manager->reparentObject(spawned->getUuid(), parentUuid);
							manager->reorderBefore(spawned->getUuid(), node.uuid);
						}
						else
						{
							manager->reparentObject(spawned->getUuid(), node.uuid);
						}
					}
				}
				else /* pScn */
				{
					kString draggedUuid((const char *)p->Data);
					if (isObjectRow)
					{
						if (dropBefore && manager->objectMap.count(node.uuid))
						{
							kObject *target = manager->objectMap[node.uuid].object;
							kObject *targetParent = target ? target->getParent() : nullptr;
							kString parentUuid = (targetParent && targetParent != manager->getScene()->getRootNode())
													 ? targetParent->getUuid()
													 : kString("");
							manager->reparentObject(draggedUuid, parentUuid);
							manager->reorderBefore(draggedUuid, node.uuid);
						}
						else
						{
							manager->reparentObject(draggedUuid, node.uuid);
						}
					}
					else if (level == 1)
					{
						// Dropped onto a scene row → reparent to scene root.
						manager->reparentObject(draggedUuid, kString(""));
					}
				}
			}
		}

		ImGui::EndDragDropTarget();
	}

	// Item clicked
	if (gui->isItemClicked() && !node.isPrefabDescendant)
	{
		// Clicking a scene object in particle/animator preview mode returns to GameWorld.
		// PrefabPreview is NOT switched here — the hierarchy shows the prefab scene
		// graph when editing a prefab, and clicking objects there is intentional.
		if (manager->activeMode != Manager::EditorMode::GameWorld &&
		    manager->activeMode != Manager::EditorMode::PrefabPreview)
			manager->setEditorMode(Manager::EditorMode::GameWorld);

		// Disable terrain sculpt mode when selecting any object from the hierarchy
		if (manager->panelTerrain)
			manager->panelTerrain->sculpt.active = false;

		// Snapshot selection before change (for undo)
		auto selBefore = manager->getActiveSelectedObjects();
		auto selObjBefore = manager->getActiveSelectedObject();

		if (!gui->isKeyShift())
		{
			deselectAll(root);
		}
		node.isSelected = !node.isSelected || gui->isKeyShift();

		if (manager->panelProject != nullptr)
			manager->panelProject->clearSelection();

		auto &activeSel = manager->getActiveSelectedObjects();
		if (gui->isKeyShift())
		{
			auto it = std::find(activeSel.begin(), activeSel.end(), node.uuid);
			if (it == activeSel.end())
				activeSel.push_back(node.uuid);
		}
		else
		{
			activeSel.clear();
			activeSel.push_back(node.uuid);
		}

		std::cout << "Object clicked: " << node.uuid.c_str() << " ,Level:" << level << std::endl;

		kObject *newSelObj = manager->getActiveSelectedObject();

		if (level == 0)
		{
			// Root node.
			if (manager->hierarchyShowsPrefab)
			{
				// In prefab mode, the root IS the first prefab object — select it.
				manager->worldSelected = false;
				manager->selectedScene = nullptr;
				if (manager->objectMap.count(node.uuid))
					manager->getActiveSelectedObject() = manager->objectMap[node.uuid].object;
			}
			else
			{
				manager->worldSelected = true;
				manager->selectedObject = nullptr;
				manager->selectedScene = nullptr;
			}
		}
		else if (level == 1 && !manager->hierarchyShowsPrefab)
		{
			// Scene node clicked (game world only — prefab mode has no scene level).
			manager->worldSelected = false;
			manager->selectedObject = nullptr;
			manager->selectedScene = nullptr;
			kWorld *hWorld = manager->getHierarchyWorld();
			if (hWorld)
			{
				for (kScene *s : hWorld->getScenes())
				{
					if (s->getUuid() == node.uuid)
					{
						manager->selectedScene = s;
						break;
					}
				}
			}
		}
		else
		{
			// Objects
			manager->worldSelected = false;
			manager->selectedScene = nullptr;
			if (manager->objectMap[node.uuid.c_str()].object != nullptr)
			{
				std::cout << "FOUND: " << node.uuid.c_str() << std::endl;
				manager->getActiveSelectedObject() = manager->objectMap[node.uuid.c_str()].object;
				newSelObj = manager->getActiveSelectedObject();
			}
			else
			{
				std::cout << "NOT FOUND: " << node.uuid.c_str() << std::endl;
			}
		}

		// Push selection undo command
		auto selAfter = manager->getActiveSelectedObjects();
		auto selObjAfter = newSelObj;
		if (selBefore != selAfter || selObjBefore != selObjAfter)
		{
			manager->undoRedo.push(std::make_unique<SelectCommand>(
				manager,
				selBefore, selObjBefore,
				selAfter, selObjAfter));
		}
	}

	// Right-click context menu for object rows
	if (isObjectRow && !node.isPrefabDescendant)
	{
		if (ImGui::BeginPopupContextItem("##HierarchyCtx"))
		{
			kObject *ctxObj = nullptr;
			if (manager->objectMap.count(node.uuid))
				ctxObj = manager->objectMap[node.uuid].object;

			bool isPrefabRoot = (ctxObj && !ctxObj->getPrefabRef().empty());

			if (isPrefabRoot)
			{
				// Prefab instance root — only show Unpack Prefab.
				if (ImGui::MenuItem("Unpack Prefab"))
					manager->unpackPrefabInstance(ctxObj);
			}
			else
			{
				// Regular object — show Rename, Duplicate, Delete.
				if (ImGui::MenuItem("Rename"))
				{
					strncpy_s(renameBuffer, sizeof(renameBuffer), node.name.c_str(), _TRUNCATE);
					renameNodeUuid = node.uuid;
					manager->selectObject(node.uuid, true);
				}

				if (ImGui::MenuItem("Duplicate"))
				{
					manager->selectObject(node.uuid, true);
					manager->duplicateSelectedObjects();
				}

				if (ImGui::MenuItem("Delete"))
				{
					manager->selectObject(node.uuid, true);
					manager->deleteSelectedObjects();
				}
			}

			ImGui::EndPopup();
		}
	}

	if (nodeOpen)
	{
		level++;

		for (auto &child : node.children)
		{
			drawNode(*child, root, level);
		}
		gui->treePop();
	}
}

void PanelHierarchy::drawHierarchyPanel(Node &root, bool *opened)
{
	// Apply any refresh that was deferred because refreshList() was called
	// while drawNode was still iterating the tree (e.g. from a context-menu
	// action like Duplicate / Delete).
	if (pendingRefresh)
		refreshList();

	gui->beginDisabled(!manager->projectOpened);

	gui->windowStart("Hierarchy", opened);
	{
		focused = gui->isWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		gui->pushStyleColor(ImGuiCol_Button, kVec4(0, 0, 0, 0));		// Background
		gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0, 0, 0, 0)); // Hover
		gui->pushStyleColor(ImGuiCol_ButtonActive, kVec4(0, 0, 0, 0));	// Pressed

		gui->pushStyleVar(ImGuiStyleVar_ItemSpacing, kVec2(2, 0)); // smaller gap (2px horizontal, 0 vertical)
		gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(2, 3)); // compact icon button

		// Add button
		{
			if (gui->imageButton("AddButton", iconAdd, kVec2(16, 16), kVec2(0, 0), kVec2(1, 1), addTint))
			{
			}
			addTint = gui->isItemActive() ? kVec4(1, 1, 1, 0.5f) : kVec4(1, 1, 1, 1);
		}

		gui->popStyleVar(); // FramePadding
		gui->popStyleVar(); // ItemSpacing
		gui->popStyleColor(3);

		// Put search box on the same line
		gui->sameLine(0.0f, 2.0f);

		// Search bar
		gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(4, (22 - gui->getFontSize()) * 0.5f));
		gui->pushItemWidth(-FLT_MIN);

		gui->groupStart();
		{
			float iconSize = gui->getFontSize() * 0.8f; // scale relative to text height
			kVec2 cursor = gui->getCursorScreenPos();

			// Draw the icon over the input box (aligned left-center)
			gui->drawListAddImage(
				iconMag,
				kVec2(cursor.x + 4, cursor.y + (gui->getFrameHeight() - iconSize) * 0.5f),
				kVec2(cursor.x + 4 + iconSize, cursor.y + (gui->getFrameHeight() + iconSize) * 0.5f));

			gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(iconSize + 8, 3));

			// Input aligned with button height
			gui->setNextItemWidth(-FLT_MIN);
			gui->inputTextWithHint("##SearchHierarchy", "Search...", searchBuffer);
		}
		gui->groupEnd();

		gui->popItemWidth();
		gui->popStyleVar(2);

		// Tree view
		{
			float availableHeight = gui->getContentRegionAvail().y - 4; // 4 px spacing

			gui->childStart("HierarchyTree", kVec2(0, availableHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
			{
				if (manager->projectOpened)
					{
						drawingTree = true;
						drawNode(root, root, 0);
						drawingTree = false;
					}
				else
				{
					kString text = "No active world";
					float textWidth = gui->calcTextSize(text).x;
					float columnWidth = gui->getColumnWidth();
					float textX = gui->getCursorPosX() + (columnWidth - textWidth) * 0.5f; // center horizontally
					gui->setCursorPosX(textX);

					gui->text(text);
				}
			}
			gui->childEnd();
		}

		// Inline rename popup — opened from the right-click context menu.
		if (!renameNodeUuid.empty())
		{
			ImGui::OpenPopup("##RenameObject");
			ImVec2 center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (ImGui::BeginPopup("##RenameObject"))
			{
				ImGui::Text("Rename object:");
				ImGui::SetNextItemWidth(250.0f);
				if (ImGui::InputText("##RenameInput", renameBuffer, sizeof(renameBuffer),
									 ImGuiInputTextFlags_EnterReturnsTrue))
				{
					// Apply rename
					if (manager->objectMap.count(renameNodeUuid))
					{
						kObject *obj = manager->objectMap[renameNodeUuid].object;
						if (obj && renameBuffer[0] != '\0')
						{
							obj->setName(kString(renameBuffer));
							refreshList();
						}
					}
					renameNodeUuid.clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("OK"))
				{
					if (manager->objectMap.count(renameNodeUuid))
					{
						kObject *obj = manager->objectMap[renameNodeUuid].object;
						if (obj && renameBuffer[0] != '\0')
						{
							obj->setName(kString(renameBuffer));
							refreshList();
						}
					}
					renameNodeUuid.clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
				{
					renameNodeUuid.clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
	}
	gui->windowEnd();

	gui->endDisabled();
}

void PanelHierarchy::draw(bool &opened)
{
	if (opened)
		drawHierarchyPanel(root, &opened);
}

void PanelHierarchy::refreshList()
{
	// If called while the tree is being drawn (e.g. from a right-click
	// context-menu action like Duplicate or Delete), defer the rebuild
	// until the next frame.  Destroying the Node tree mid-iteration is a
	// use-after-free that crashes inside drawNode().
	if (drawingTree)
	{
		pendingRefresh = true;
		return;
	}
	pendingRefresh = false;

	manager->objectMap.clear();
	root.children.clear();

	auto pickIcon = [&](kObject *obj, kString &outType) -> uint32_t
	{
		// Prefab-instance roots use the prefab icon regardless of the
		// underlying object type, so they're visually distinct in the tree.
		if (!obj->getPrefabRef().empty())
		{
			outType = "prefab";
			return iconPrefab;
		}
		switch (obj->getType())
		{
		case kNodeType::NODE_TYPE_TERRAIN:
			outType = "terrain";
			return iconTerrain;
		case kNodeType::NODE_TYPE_OBJECT:
		{
			// Check for mesh-based terrain tiles (getSerializeType == "terrain")
			kMesh *mesh = dynamic_cast<kMesh *>(obj);
			if (mesh && mesh->getSerializeType() == "terrain")
			{
				outType = "terrain";
				return iconTerrain;
			}
			outType = "object";
			return iconEmpty;
		}
		case kNodeType::NODE_TYPE_MESH:
		{
			// Check for mesh-based terrain tiles (getSerializeType == "terrain")
			kMesh *mesh = dynamic_cast<kMesh *>(obj);
			if (mesh && mesh->getSerializeType() == "terrain")
			{
				outType = "terrain";
				return iconTerrain;
			}
			outType = "mesh";
			return iconMesh;
		}
		case kNodeType::NODE_TYPE_LIGHT:
			outType = "light";
			return iconLight;
		case kNodeType::NODE_TYPE_CAMERA:
			outType = "camera";
			return iconCamera;
		case kNodeType::NODE_TYPE_DECAL:
			outType = "decal";
			return iconMesh;
		case kNodeType::NODE_TYPE_AUDIO:
			if (!obj->getAudioListeners().empty() && obj->getAudioSources().empty())
			{
				outType = "audio_listener";
				return iconAudioListener;
			}
			outType = "audio";
			return iconAudio;
		default:
			outType = "unknown";
			return iconEmpty;
		}
	};

	// Walk the scene-graph subtree below `obj` and mirror it into the
	// hierarchy panel. `insidePrefab` propagates the disabled state down to
	// every descendant of a prefab instance root.
	std::function<void(Node *, kObject *, bool)> addObject =
		[&](Node *parent, kObject *obj, bool insidePrefab)
	{
		// Skip internal objects whose names start with '_' (e.g. _SunLight_).
		const kString &objName = obj->getName();
		if (!objName.empty() && objName[0] == '_')
			return;

		kString type;
		uint32_t icon = pickIcon(obj, type);

		auto childNode = std::make_unique<Node>(objName, obj->getUuid(), icon, type);
		childNode->isPrefabDescendant = insidePrefab;
		Node *raw = childNode.get();
		parent->children.emplace_back(std::move(childNode));

		manager->objectMap[obj->getUuid()] = ObjectInfo{obj};

		// Anything below a prefab instance root is a prefab descendant.
		bool nextInside = insidePrefab || !obj->getPrefabRef().empty();

		for (kObject *c : obj->getChildren())
			addObject(raw, c, nextInside);
	};

	// Use the manager's active hierarchy world — this switches between the
	// game world and the prefab world depending on which panel is focused.
	kWorld *hWorld = manager->getHierarchyWorld();
	if (!hWorld) return;

	// Filter out internal/editor-overlay scenes (names starting with "_").
	const auto &scenes = hWorld->getScenes();
	std::vector<kScene *> visibleScenes;
	for (kScene *s : scenes)
	{
		if (s && !s->getName().empty() && s->getName()[0] == '_')
			continue;
		visibleScenes.push_back(s);
	}

	bool isPrefab = manager->hierarchyShowsPrefab;

	if (isPrefab && !visibleScenes.empty())
	{
		// Prefab mode: the first top-level prefab object becomes the hierarchy
		// root (replacing "World"), and its children appear recursively beneath.
		kScene *scene0 = visibleScenes[0];
		kObject *sceneRoot = scene0->getRootNode();
		if (sceneRoot && !sceneRoot->getChildren().empty())
		{
			kObject *firstObj = sceneRoot->getChildren()[0];
			kString type;
			uint32_t icon = pickIcon(firstObj, type);
			root.name = firstObj->getName();
			root.uuid = firstObj->getUuid();
			root.icon = icon;
			root.type = type;
			manager->objectMap[firstObj->getUuid()] = ObjectInfo{firstObj};

			// Children of the first object go under the root.
			for (kObject *child : firstObj->getChildren())
				addObject(&root, child, /*insidePrefab*/ false);
		}
	}
	else
	{
		// Game world mode: standard World → Scene → Objects tree.
		root.name = "World";

		if (!visibleScenes.empty())
		{
			kScene *scene0 = visibleScenes[0];

			auto &sceneNode = root.children.emplace_back(
				std::make_unique<Node>(scene0->getName(), scene0->getUuid(), iconScene, "scene"));

			kObject *rootNode = scene0->getRootNode();
			if (rootNode != nullptr)
			{
				for (kObject *child : rootNode->getChildren())
					addObject(sceneNode.get(), child, /*insidePrefab*/ false);
			}
		}

		for (size_t i = 1; i < visibleScenes.size(); ++i)
		{
			kScene *scene = visibleScenes[i];

			auto &sceneNode = root.children.emplace_back(
				std::make_unique<Node>(scene->getName(), scene->getUuid(), iconScene, "scene"));

			kObject *rootNode = scene->getRootNode();
			if (rootNode == nullptr)
				continue;
			for (kObject *child : rootNode->getChildren())
				addObject(sceneNode.get(), child, /*insidePrefab*/ false);
		}
	}
}
