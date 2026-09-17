#include "panel_project.h"

#include <cstring>
#include <functional>
#include <vector>
#include <algorithm>

using namespace kemena;

PanelProject::PanelProject(kGuiManager* setGuiManager, Manager* setManager, kAssetManager* assetManager)
	: rootTree("Assets", "asset", nullptr, 0), rootThumbnail("Assets", "asset", nullptr, 0)
{
    gui = setGuiManager;
	manager = setManager;
	manager->panelProject = this;
	this->assetManager = assetManager;

	// Button icons
	kTexture2D* tex_up = assetManager->loadTexture2DFromResource("ICON_FOLDER_UP_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconUp = (ImTextureRef)(intptr_t)tex_up->getTextureID();

	kTexture2D* tex_add = assetManager->loadTexture2DFromResource("ICON_ADD_ROUND_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconAdd = tex_add->getTextureID();

	kTexture2D* tex_mag = assetManager->loadTexture2DFromResource("ICON_MAGNIFIER_LABEL", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconMag = (ImTextureRef)(intptr_t)tex_mag->getTextureID();

	// List and thumbnail icon
	kTexture2D* tex_thumb = assetManager->loadTexture2DFromResource("ICON_SCENE_LABEL", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconThumbnail = (ImTextureRef)(intptr_t)tex_thumb->getTextureID();

	kTexture2D* tex_list = assetManager->loadTexture2DFromResource("ICON_HIERARCHY_LABEL", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconList = (ImTextureRef)(intptr_t)tex_list->getTextureID();

	// File icons (built-in)
	kTexture2D* tex_folder = assetManager->loadTexture2DFromResource("ICON_FOLDER_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconFolder = (ImTextureRef)(intptr_t)tex_folder->getTextureID();

	kTexture2D* tex_text = assetManager->loadTexture2DFromResource("ICON_TEXT_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconText = (ImTextureRef)(intptr_t)tex_text->getTextureID();

	kTexture2D* tex_image = assetManager->loadTexture2DFromResource("ICON_IMAGE_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconImage = (ImTextureRef)(intptr_t)tex_image->getTextureID();

	kTexture2D* tex_script = assetManager->loadTexture2DFromResource("ICON_SCRIPT_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconScript = (ImTextureRef)(intptr_t)tex_script->getTextureID();

	kTexture2D* tex_audio = assetManager->loadTexture2DFromResource("ICON_AUDIO_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconAudio = (ImTextureRef)(intptr_t)tex_audio->getTextureID();

	kTexture2D* tex_video = assetManager->loadTexture2DFromResource("ICON_VIDEO_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconVideo = (ImTextureRef)(intptr_t)tex_video->getTextureID();

	kTexture2D* tex_animation = assetManager->loadTexture2DFromResource("ICON_ANIMATION_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconAnimation = (ImTextureRef)(intptr_t)tex_animation->getTextureID();

	kTexture2D* tex_model = assetManager->loadTexture2DFromResource("ICON_MODEL_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconModel = (ImTextureRef)(intptr_t)tex_model->getTextureID();

	kTexture2D* tex_prefab = assetManager->loadTexture2DFromResource("ICON_PREFAB_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconPrefab = (ImTextureRef)(intptr_t)tex_prefab->getTextureID();

	kTexture2D* tex_world = assetManager->loadTexture2DFromResource("ICON_WORLD_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconWorld = (ImTextureRef)(intptr_t)tex_world->getTextureID();

	kTexture2D* tex_material = assetManager->loadTexture2DFromResource("ICON_MATERIAL_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconMaterial = (ImTextureRef)(intptr_t)tex_material->getTextureID();

	kTexture2D* tex_logic = assetManager->loadTexture2DFromResource("ICON_LOGIC_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconLogic = (ImTextureRef)(intptr_t)tex_logic->getTextureID();

	kTexture2D* tex_shader = assetManager->loadTexture2DFromResource("ICON_SHADER_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconShader = (ImTextureRef)(intptr_t)tex_shader->getTextureID();

	kTexture2D* tex_shader_script = assetManager->loadTexture2DFromResource("ICON_SHADER_SCRIPT_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconShaderScript = (ImTextureRef)(intptr_t)tex_shader_script->getTextureID();

	kTexture2D* tex_particle = assetManager->loadTexture2DFromResource("ICON_PARTICLE_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconParticle = (ImTextureRef)(intptr_t)tex_particle->getTextureID();

	kTexture2D* tex_gui = assetManager->loadTexture2DFromResource("ICON_GUI_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconGui = (ImTextureRef)(intptr_t)tex_gui->getTextureID();

	kTexture2D* tex_other = assetManager->loadTexture2DFromResource("ICON_OTHER_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconOther = (ImTextureRef)(intptr_t)tex_other->getTextureID();

	rootTree = Node("Asset", "asset", (ImTextureID)(intptr_t)tex_folder->getTextureID(), 0);
	rootThumbnail = Node("Asset", "asset", (ImTextureID)(intptr_t)tex_folder->getTextureID(), 0);
}

ImTextureRef PanelProject::getThumbnailIcon(const kString& uuid, ImTextureRef defaultIcon)
{
	if (uuid.empty()) return defaultIcon;

	auto it = thumbnailCache.find(uuid);
	if (it != thumbnailCache.end())
		return it->second.second;

	fs::path thumbPath = manager->projectPath / "Library" / "Thumbnails" / (uuid + ".png");
	if (!fs::exists(thumbPath)) return defaultIcon;

	try
	{
		kString thumbKey = uuid + "_thumb";
		kTexture2D* tex = assetManager->loadTexture2D(
			thumbPath.string(), thumbKey, kTextureFormat::TEX_FORMAT_RGBA, false);
		if (tex && tex->getTextureID() != 0)
		{
			uint32_t glId = tex->getTextureID();
			ImTextureRef ref = (ImTextureRef)(intptr_t)glId;
			thumbnailCache[uuid] = { glId, ref };
			return ref;
		}
	}
	catch (...) {}

	return defaultIcon;
}

PanelProject::SelectedProjectAsset PanelProject::getProjectSelection() const
{
	SelectedProjectAsset result;

	// Collect all selected nodes from the active view (flat for thumbnail, recursive for tree)
	std::vector<const Node*> selected;

	std::function<void(const Node&)> collect = [&](const Node& n) {
		for (auto& child : n.children) {
			if (child->isSelected)
				selected.push_back(child.get());
			collect(*child);
		}
	};

	collect(displayThumbnail ? rootThumbnail : rootTree);

	result.count = (int)selected.size();
	if (result.count == 1)
	{
		const Node* n   = selected[0];
		result.name     = n->name;
		result.uuid     = n->uuid;
		result.isFolder = (n->type == 0);
		result.thumbnail = n->icon;

		if (!result.isFolder && !result.uuid.empty())
		{
			auto it = manager->fileMap.find(result.uuid);
			if (it != manager->fileMap.end())
				result.fileType = it->second.type;
			result.metaPath = manager->projectPath / "Library" / "Metadata" / (result.uuid + ".json");
		}
	}

	return result;
}

void PanelProject::deselectAll(Node& root)
{
	root.isSelected = false;
	for (auto& child : root.children)
		deselectAll(*child);
}

void PanelProject::clearSelection()
{
	deselectAll(rootTree);
	deselectAll(rootThumbnail);
	pendingSelectUuid = ""; // drop any pending re-selection too
	selectionAnchorUuid = "";
}

void PanelProject::handleSelectionClick(Node& root, Node& clicked)
{
	// Disable terrain sculpt mode when selecting a project asset
	if (manager->panelTerrain)
		manager->panelTerrain->sculpt.active = false;

	const bool ctrl  = ImGui::GetIO().KeyCtrl;
	const bool shift = ImGui::GetIO().KeyShift;

	if (shift && !selectionAnchorUuid.empty())
	{
		// Flatten the view in depth-first display order, then select the
		// inclusive range between the anchor and the clicked node.
		std::vector<Node*> order;
		std::function<void(Node&)> flatten = [&](Node& n) {
			for (auto& c : n.children) { order.push_back(c.get()); flatten(*c); }
		};
		flatten(root);

		int ai = -1, ci = -1;
		for (int i = 0; i < (int)order.size(); ++i)
		{
			if (order[i]->uuid == selectionAnchorUuid && ai < 0) ai = i;
			if (order[i] == &clicked) ci = i;
		}

		if (ai >= 0 && ci >= 0)
		{
			if (!ctrl) deselectAll(root); // Shift replaces; Ctrl+Shift extends
			int lo = std::min(ai, ci), hi = std::max(ai, ci);
			for (int i = lo; i <= hi; ++i) order[i]->isSelected = true;
		}
		else
		{
			deselectAll(root);
			clicked.isSelected = true;
			selectionAnchorUuid = clicked.uuid;
		}
	}
	else if (ctrl)
	{
		clicked.isSelected = !clicked.isSelected; // toggle, keep others
		selectionAnchorUuid = clicked.uuid;
	}
	else
	{
		deselectAll(root);
		clicked.isSelected = true;
		selectionAnchorUuid = clicked.uuid;
	}

	// Selecting a project asset clears any scene/world selection.
	manager->selectedObjects.clear();
	manager->selectedObject = nullptr;
	manager->selectedScene  = nullptr;
	manager->worldSelected  = false;
	pendingSelectUuid = "";
}

void PanelProject::beginAssetDragSource(Node& node)
{
	// A drag started, so the deferred plain-click must not fire on release and
	// collapse the selection to a single row.
	clickPendingUuid = "";

	// Drag the whole selection when the grabbed node is part of it; otherwise
	// the grabbed node becomes the sole selection and is dragged alone.
	if (!node.isSelected)
	{
		deselectAll(rootTree);
		deselectAll(rootThumbnail);
		node.isSelected = true;
		selectionAnchorUuid = node.uuid;
	}

	// Gather every selected *file* UUID from the active view.
	std::vector<kString> uuids;
	std::function<void(Node&)> collect = [&](Node& n) {
		for (auto& c : n.children)
		{
			if (c->isSelected && c->type == 1 && !c->uuid.empty())
				uuids.push_back(c->uuid);
			collect(*c);
		}
	};
	collect(displayThumbnail ? rootThumbnail : rootTree);
	if (uuids.empty() && node.type == 1 && !node.uuid.empty())
		uuids.push_back(node.uuid);

	// Payload is a newline-separated UUID list (multi-file move).
	kString payload;
	for (size_t i = 0; i < uuids.size(); ++i)
	{
		if (i) payload += "\n";
		payload += uuids[i];
	}

	ImGui::SetDragDropPayload("PROJECT_ASSET", payload.c_str(), payload.size() + 1);
	ImGui::Image(node.icon, ImVec2(16, 16));
	ImGui::SameLine();
	if (uuids.size() > 1)
		ImGui::Text("%d items", (int)uuids.size());
	else
		ImGui::TextUnformatted(node.name.c_str());
}

void PanelProject::acceptAssetDropInto(const fs::path& targetDir)
{
	if (!ImGui::BeginDragDropTarget()) return;
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET"))
	{
		// Split the newline-separated UUID list and move each asset.
		kString data(static_cast<const char*>(payload->Data));
		bool moved = false;
		size_t start = 0;
		while (start <= data.size())
		{
			size_t nl = data.find('\n', start);
			kString uuid = data.substr(start, nl == kString::npos ? kString::npos : nl - start);
			if (!uuid.empty())
			{
				fs::path src = manager->findAssetPathByUuid(uuid);
				if (!src.empty() && manager->moveAsset(src, targetDir))
					moved = true;
			}
			if (nl == kString::npos) break;
			start = nl + 1;
		}
		if (moved)
		{
			clearSelection();
			needRefreshList = true;
		}
	}
	else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
	{
		kString objectUuid(static_cast<const char*>(payload->Data));
		if (!objectUuid.empty())
		{
			// Look up the object to check if it's already a prefab instance.
			kObject *obj = nullptr;
			if (manager->objectMap.count(objectUuid))
				obj = manager->objectMap[objectUuid].object;

			bool isPrefabInstance = (obj && !obj->getPrefabRef().empty());

			if (isPrefabInstance)
			{
				// Store pending state and open the confirmation popup.
				pendingPrefabObjUuid = objectUuid;
				pendingPrefabTargetDir = targetDir;
				ImGui::OpenPopup("CreatePrefabFromInstance?");
			}
			else
			{
				manager->createPrefabFromObject(objectUuid, targetDir);
			}
		}
	}
	ImGui::EndDragDropTarget();
}

void PanelProject::drawProjectPanel(Node& rootTree, Node& rootThumbnail, bool* opened)
{
	gui->beginDisabled(!manager->projectOpened);

	// The panel header is fixed-height and the assets live in a scrollable
	// child region below, so disable the panel window's own scrollbars —
	// only the child asset list should scroll vertically.
	gui->windowStart("Project", opened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	focused = gui->isWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	{
		gui->pushStyleColor(ImGuiCol_Button, kVec4(0, 0, 0, 0));
		gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0, 0, 0, 0));
		gui->pushStyleColor(ImGuiCol_ButtonActive, kVec4(0, 0, 0, 0));

		gui->pushStyleVar(ImGuiStyleVar_ItemSpacing, kVec2(2, 0));
		gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(3, 3)); // square icon buttons

		// Up button
		{
			if (ImGui::ImageButton("UpButton", iconUp, ImVec2(20, 20)))
			{
				manager->closeFolder();
				needRefreshList = true;
			}
			upTint = gui->isItemActive() ? ImVec4(1,1,1,0.5f) : ImVec4(1,1,1,1);

			if (gui->isItemHovered())
				gui->setItemTooltip("Go to parent directory");
		}

		gui->sameLine();

		// Add button
		{
			if (gui->imageButton("AddButton", iconAdd, kVec2(20, 20), kVec2(0, 0), kVec2(1, 1), addTint))
			{
			}
			addTint = gui->isItemActive() ? kVec4(1, 1, 1, 0.5f) : kVec4(1, 1, 1, 1);

			if (gui->isItemHovered())
				gui->setItemTooltip("Import assets to project");
		}

		gui->popStyleVar();      // FramePadding
		gui->popStyleVar();      // ItemSpacing
		gui->popStyleColor(3);

		gui->sameLine(0.0f, 2.0f);

		gui->pushStyleVar(ImGuiStyleVar_ItemSpacing, kVec2(0, 0));

		// Search bar
		gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(4, (26 - gui->getFontSize()) * 0.5f));
		gui->pushItemWidth(-FLT_MIN);

		gui->popStyleVar();

		gui->groupStart();
		{
			const float searchH = 26.0f; // Search input / toolbar control height.
			float iconSize = gui->getFontSize(); // Bigger magnifier icon.
			kVec2 cursor = gui->getCursorScreenPos();
			float buttonWidth = 26.0f; // List/thumbnail button is now larger.
			float searchWidth = gui->getContentRegionAvail().x - buttonWidth - 15;

			ImGui::GetWindowDrawList()->AddImage(
				iconMag,
				ImVec2(cursor.x + 4, cursor.y + (searchH - iconSize) * 0.5f),
				ImVec2(cursor.x + 4 + iconSize, cursor.y + (searchH + iconSize) * 0.5f)
			);

			gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(iconSize + 8, (searchH - gui->getFontSize()) * 0.5f));
			gui->setNextItemWidth(searchWidth);
			ImGui::InputTextWithHint("##SearchProject", "Search...", searchBuffer, IM_ARRAYSIZE(searchBuffer));
			gui->popStyleVar();

			gui->sameLine(0.0f, 8.0f);

			// List or Thumbnail button
			gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(3, 3)); // square icon button
			{
				if (displayThumbnail)
				{
					if (ImGui::ImageButton("ListButton", iconList, ImVec2(20, 20)))
						displayThumbnail = false;

					if (gui->isItemHovered())
						gui->setItemTooltip("Switch to list view");
				}
				else
				{
					if (ImGui::ImageButton("ThumbnailButton", iconThumbnail, ImVec2(20, 20)))
						displayThumbnail = true;

					if (gui->isItemHovered())
						gui->setItemTooltip("Switch to thumbnail view");
				}
			}
			gui->popStyleVar();
		}
		gui->groupEnd();

		gui->popItemWidth();
		gui->popStyleVar();

		gui->spacing();

		float availableHeight = gui->getContentRegionAvail().y - 4;

		if (displayThumbnail)
		{
			gui->childStart("ProjectThumbnail", kVec2(0, availableHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
			{
				drawBreadcrumb();
				drawThumbnailNode(rootThumbnail);
			}
			gui->childEnd();
		}
		else
		{
			gui->childStart("ProjectTree", kVec2(0, availableHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
			{
				drawTreeNode(rootTree, rootTree);
			}
			gui->childEnd();
		}

		// Fallback drop target at window level. If a SCENE_OBJECT payload
		// wasn't consumed by a folder target (drop on file / empty space),
		// accept it here and create a prefab in the current browsed directory.
		{
			fs::path curDir = manager->getCurrentDirPath();
			if (!curDir.empty() && ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload *pPayload =
					ImGui::AcceptDragDropPayload("SCENE_OBJECT");
				if (pPayload && pPayload->IsDelivery())
				{
					kString objectUuid(
						static_cast<const char *>(pPayload->Data));
					if (!objectUuid.empty())
						manager->createPrefabFromObject(objectUuid, curDir);
				}
				ImGui::EndDragDropTarget();
			}
		}

		gui->spacing();

		// Delete key shortcut
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		    ImGui::IsKeyPressed(ImGuiKey_Delete) &&
		    getProjectSelection().count > 0)
		{
			executeDeleteSelected();
		}

		drawRenameModal();

		// Confirmation popup when dragging a prefab instance from the hierarchy
		// to the project panel to create a new, independent prefab.
		if (ImGui::BeginPopupModal("CreatePrefabFromInstance?", nullptr,
		                           ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextWrapped(
				"This object is already a prefab instance.");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"Creating a new prefab from it will break the link to "
				"the existing prefab. The object will become an instance "
				"of the new prefab instead.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (ImGui::Button("Create New Prefab", ImVec2(160, 0)))
			{
				if (!pendingPrefabObjUuid.empty())
				{
					manager->createPrefabFromObject(
						pendingPrefabObjUuid, pendingPrefabTargetDir);
				}
				pendingPrefabObjUuid.clear();
				pendingPrefabTargetDir.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				pendingPrefabObjUuid.clear();
				pendingPrefabTargetDir.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
	gui->windowEnd();

	gui->endDisabled();
}

void PanelProject::draw(bool& opened)
{
	focused = false;
	if (manager->projectOpened)
	{
		if (needRefreshList)
		{
			clearThumbnailCache();
			refreshTreeList();
			refreshThumbnailList();
			needRefreshList = false;

			if (!pendingSelectUuid.empty())
			{
				// Re-assert the pending selection on every rebuild so it survives
				// the refreshes triggered by thumbnail (re)generation. It persists
				// until the user explicitly selects another asset (the click
				// handlers and clearSelection() reset it); this is what keeps a
				// material focused in the inspector after pressing Apply.
				std::function<void(Node&)> sel = [&](Node& n) {
					for (auto& child : n.children) {
						if (child->uuid == pendingSelectUuid) child->isSelected = true;
						sel(*child);
					}
				};
				sel(displayThumbnail ? rootThumbnail : rootTree);
			}
		}

		drawProjectPanel(rootTree, rootThumbnail, &opened);
	}
}

void PanelProject::clearThumbnailCache()
{
	for (auto& [uuid, entry] : thumbnailCache)
	{
		if (entry.first) glDeleteTextures(1, &entry.first);
	}
	thumbnailCache.clear();
}

void PanelProject::invalidateThumbnail(const kString& uuid)
{
	auto it = thumbnailCache.find(uuid);
	if (it != thumbnailCache.end())
	{
		if (it->second.first) glDeleteTextures(1, &it->second.first);
		thumbnailCache.erase(it);
	}
}

void PanelProject::refreshTreeList()
{
	if (!manager->projectOpened)
		return;

	fs::path fullPath = manager->projectPath;
	fullPath /= "Assets"; // Tree view always show the full project structure

	if (!fs::exists(fullPath) || !fs::is_directory(fullPath))
	{
		std::cerr << "Path does not exist or is not a directory: " << fullPath << "\n";
		return;
	}

	// Root node setup
	kString folderName = fullPath.filename().string();
	rootTree.name = folderName;
	rootTree.uuid = "asset";
	rootTree.icon = iconFolder;
	rootTree.type = 0;
	rootTree.isSelected = false;
	rootTree.fullPath = fullPath; // so dropping on the root moves into Assets/
	rootTree.children.clear();

	// Fill recursively
	populateTree(rootTree, fullPath);
}

void PanelProject::drawTreeNode(Node& node, Node& rootTree, int level)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (node.isSelected) flags |= ImGuiTreeNodeFlags_Selected;
	if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;

	// Only expand the first node by default
	if (&node == &rootTree)
	{
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}

	ImGui::Image(node.icon, ImVec2(16, 16));
	gui->sameLine();

	bool nodeOpen = gui->treeStartEx(node.uuid, node.name, flags);

	if (gui->isItemHovered() && gui->isMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		// Files open in their owning editor panel (World, Animator, Logic
		// Graph, ...). Folders keep ImGui's default tree-node toggle.
		if (node.type == 1 && !node.fullPath.empty() && onFileDoubleClicked)
			onFileDoubleClicked(node.fullPath.string());
	}

	// Selection. A plain click on an already-selected row is deferred to release
	// so that pressing it to start a drag keeps the whole multi-selection
	// (mirrors how the thumbnail view's Selectable behaves). Ctrl/Shift and
	// clicks on unselected rows act immediately.
	{
		const bool ctrl  = ImGui::GetIO().KeyCtrl;
		const bool shift = ImGui::GetIO().KeyShift;
		if (gui->isItemClicked())
		{
			if (ctrl || shift || !node.isSelected)
			{
				handleSelectionClick(rootTree, node);
				clickPendingUuid = "";
			}
			else
			{
				clickPendingUuid = node.uuid; // maybe a drag — decide on release
			}
		}
		if (!clickPendingUuid.empty() && clickPendingUuid == node.uuid &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			if (ImGui::IsItemHovered())
				handleSelectionClick(rootTree, node); // released without dragging
			clickPendingUuid = "";
		}
	}

	// Drag source for file items in the tree view (carries the whole selection).
	if (node.type == 1 && !node.uuid.empty() &&
		ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		beginAssetDragSource(node);
		ImGui::EndDragDropSource();
	}

	// Drop target — folders accept PROJECT_ASSET (move) and SCENE_OBJECT (prefab).
	if (node.type == 0)
		acceptAssetDropInto(node.fullPath);

	// "Drop between" indicator for SCENE_OBJECT — shows a horizontal line
	// at the top of the row so the user can drop between items to create
	// a prefab in the directory containing those items.
	{
		// Determine the target directory for this row:
		//   folder → its own path
		//   file   → parent directory
		fs::path targetDir;
		if (node.type == 0)
			targetDir = node.fullPath;
		else if (node.type == 1 && !node.fullPath.empty())
			targetDir = node.fullPath.parent_path();

		if (!targetDir.empty() && ImGui::BeginDragDropTarget())
		{
			ImVec2 rmin = ImGui::GetItemRectMin();
			ImVec2 rmax = ImGui::GetItemRectMax();

			const ImGuiPayload *pPayload =
				ImGui::AcceptDragDropPayload("SCENE_OBJECT",
					ImGuiDragDropFlags_AcceptBeforeDelivery |
					ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
			if (pPayload)
			{
				ImU32 col = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
				ImGui::GetForegroundDrawList()->AddLine(
					ImVec2(rmin.x, rmin.y), ImVec2(rmax.x, rmin.y), col, 2.0f);

				if (pPayload->IsDelivery())
				{
					kString objectUuid(
						static_cast<const char *>(pPayload->Data));
					if (!objectUuid.empty() && !targetDir.empty())
						manager->createPrefabFromObject(objectUuid, targetDir);
				}
			}
			ImGui::EndDragDropTarget();
		}
	}

	if (ImGui::BeginPopupContextItem("##TreeCtx"))
	{
		if (!node.isSelected)
		{
			deselectAll(rootTree);
			node.isSelected = true;
		}
		if (ImGui::MenuItem("Rename"))
			beginRename(node.fullPath, node.name);
		if (ImGui::MenuItem("Duplicate"))
			{ manager->duplicateAsset(node.fullPath); needRefreshList = true; }
		if (ImGui::MenuItem("Delete"))
			executeDeleteSelected();
		ImGui::Separator();
		if (ImGui::MenuItem("Reimport"))
			{ manager->reimportAsset(node.uuid); needRefreshList = true; }
		ImGui::EndPopup();
	}

	if (nodeOpen)
	{
		level++;

		for (auto& child : node.children)
			drawTreeNode(*child, rootTree, level);

		gui->treePop();
	}
}

void PanelProject::populateTree(Node& parent, const fs::path& fullPath)
{
	if (fs::is_empty(fullPath))
		return;

    fs::path assetsPath = manager->projectPath / "Assets";

	// --- First: Directories ---
	for (const auto& entry : fs::directory_iterator(fullPath))
	{
		if (entry.is_directory())
		{
			auto child = std::make_unique<Node>(
							 entry.path().filename().string(),
							 "Folder_" + entry.path().filename().string(),
							 iconFolder,
							 0
						 );
			child->fullPath = entry.path();

			// Recursively fill subfolder
			populateTree(*child, entry.path());

			parent.children.emplace_back(std::move(child));
		}
	}

	// --- Second: Files ---
	for (const auto& entry : fs::directory_iterator(fullPath))
	{
		if (entry.is_regular_file())
		{
			auto ext = entry.path().extension().string();
			ImTextureRef icon;

			if (ext == ".txt" || ext == ".ini" || ext == ".xml" || ext == ".json")
				icon = iconText;
			else if (ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".png" || ext == ".gif" || ext == ".tga")
				icon = iconImage;
			else if (ext == ".as")
				icon = iconScript;
			else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg")
				icon = iconAudio;
			else if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".webm")
				icon = iconVideo;
			else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae" || ext == ".stl")
				icon = iconModel;
			else if (ext == ".prefab" || ext == ".pfb")
				icon = iconPrefab;
			else if (ext == ".particle")
				icon = iconParticle;
			else if (ext == ".world")
				icon = iconWorld;
			else if (ext == ".mat")
				icon = iconMaterial;
			else if (ext == ".logic")
				icon = iconLogic;
			else if (ext == ".glsl" || ext == ".hlsl")
				icon = iconShaderScript;
			else if (ext == ".shader")
				icon = iconShader;
			else if (ext == ".ui")
				icon = iconGui;
			else if (ext == ".animator")
				icon = iconLogic;    // Graph-editor visual, use logic icon
			else if (ext == ".animation")
				icon = iconAnimation;    // Character animation data
			else if (ext == ".cinematic")
				icon = iconLogic;    // Cinematic curve/graph visual
			else
				icon = iconOther;

            kString relativePath = fs::relative(entry.path(), assetsPath).generic_string();

            kString uuid = manager->uuidMap[relativePath];
            //std::cout << relativePath << " -> " << uuid << std::endl;

			icon = getThumbnailIcon(uuid, icon);

			auto fileNode = std::make_unique<Node>(
											 entry.path().filename().string(),
											 uuid,
											 icon,
											 1
										 );
			fileNode->fullPath = entry.path();
			parent.children.emplace_back(std::move(fileNode));
		}
	}
}

void PanelProject::refreshThumbnailList()
{
	if (manager->projectOpened)
	{
		fs::path fullPath = manager->projectPath;
		fs::path assetsPath = manager->projectPath / "Assets";

		if (!manager->currentDir.empty())
		{
			for (const auto& dir : manager->currentDir)
            {
				fullPath /= dir;  // appends with correct separator for the platform
            }
		}

		// Navigate up if the current directory no longer exists
		while ((!fs::exists(fullPath) || !fs::is_directory(fullPath)) && manager->currentDir.size() > 1)
		{
			manager->currentDir.pop_back();
			fullPath = manager->projectPath;
			for (const auto& dir : manager->currentDir)
				fullPath /= dir;
		}

		if (!fs::exists(fullPath) || !fs::is_directory(fullPath))
		{
			std::cerr << "Path does not exist or is not a directory: " << fullPath << "\n";
			return;
		}

		if (fs::is_directory(fullPath))
		{
			kString folderName = fullPath.filename().string();
			//std::cout << "Last folder: " << folderName << "\n";
			//std::cout << "Last folder: " << fullPath << "\n";

			// Check whether uuid already exist for this folder
			// Generate a uuid if haven't, then save to the list

			// Reset root safely
			rootThumbnail.name = folderName;
			rootThumbnail.uuid = "Assets";
			rootThumbnail.icon = iconFolder;
			rootThumbnail.type = 0;
			rootThumbnail.isSelected = false;
			rootThumbnail.children.clear();
		}

		// Loop through all the folders and files in fullPath
		if (!fs::exists(fullPath))
		{
			std::cerr << "Path does not exist: " << fullPath << "\n";
			return;
		}

		// Do directory first
		{
			if (!fs::is_empty(fullPath))
			{
				// Do for folders
				{
					for (const auto& entry : fs::directory_iterator(fullPath))
					{
						if (entry.is_directory())
						{
							auto folderNode = std::make_unique<Node>(
																	entry.path().filename().string(),
																	"Folder_" + entry.path().filename().string(),
																	iconFolder,
																	0
																);
							folderNode->fullPath = entry.path();
							rootThumbnail.children.emplace_back(std::move(folderNode));
						}
					}
				}

				// Do for files
				{
					for (const auto& entry : fs::directory_iterator(fullPath))
					{
						if (entry.is_regular_file())
						{
							auto ext = entry.path().extension().string();
							ImTextureRef icon;

							if (ext == ".txt" || ext == ".ini" || ext == ".xml" || ext == ".json")
								icon = iconText;
							else if (ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".png" || ext == ".gif" || ext == ".tga")
								icon = iconImage;
							else if (ext == ".as")
								icon = iconScript;
							else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg")
								icon = iconAudio;
							else if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".webm")
								icon = iconVideo;
							else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae" || ext == ".stl")
								icon = iconModel;
							else if (ext == ".prefab" || ext == ".pfb")
								icon = iconPrefab;
							else if (ext == ".world")
								icon = iconWorld;
							else if (ext == ".mat")
								icon = iconMaterial;
							else if (ext == ".logic")
								icon = iconLogic;
							else if (ext == ".particle")
								icon = iconParticle;
							else if (ext == ".glsl" || ext == ".hlsl")
								icon = iconShaderScript;
							else if (ext == ".shader")
								icon = iconShader;
							else if (ext == ".ui")
								icon = iconGui;
							else if (ext == ".animator")
								icon = iconLogic;
							else if (ext == ".animation")
								icon = iconAnimation;
							else if (ext == ".cinematic")
								icon = iconLogic;
							else
								icon = iconOther;

                            kString relativePath = fs::relative(entry.path(), assetsPath).generic_string();
                            kString uuid = manager->uuidMap[relativePath];
                            //std::cout << relativePath << " -> " << uuid << std::endl;

							icon = getThumbnailIcon(uuid, icon);

							auto thumbNode = std::make_unique<Node>(
																	entry.path().filename().string(),
																	uuid,
																	icon,
																	1
																);
							thumbNode->fullPath = entry.path();
							rootThumbnail.children.emplace_back(std::move(thumbNode));
						}
						else
						{
							// Don't display anything if it's not a folder or file (could be symlink, drive, etc.)
						}
					}
				}
			}
		}
	}
}

void PanelProject::drawThumbnailNode(const Node& currentDir)
{
	if (manager->projectOpened)
	{
		float thumbSize   = 48.0f;   // icon size
		float cellPadding = 12.0f;   // spacing between columns
		float cellWidth   = thumbSize + cellPadding;

		float panelWidth  = gui->getContentRegionAvail().x;
		int columns = (int)(panelWidth / cellWidth);
		if (columns < 1) columns = 1;

		if (currentDir.children.size() > 0)
		{
			gui->columnsStart(columns, "", false);

			for (auto& child : currentDir.children)
			{
				gui->groupStart();
				gui->pushId(child.get());

				float columnWidth = gui->getColumnWidth();
				float topMargin   = 4.0f;
				float cellHeight  = topMargin + thumbSize + gui->getTextLineHeight() + 4.0f;

				// Full-cell selectable
				bool selected = child->isSelected;
				if (gui->selectable("##thumb", selected, 0, kVec2(columnWidth, cellHeight)))
					handleSelectionClick(rootThumbnail, *child);

				// Drag source — file items only (folders aren't draggable assets).
				// Payload carries every selected file's UUID (multi-file move).
				if (child->type == 1 && !child->uuid.empty() &&
					ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
				{
					beginAssetDragSource(*child);
					ImGui::EndDragDropSource();
				}

				// Drop target — dropping asset(s) onto a folder moves them into it.
				if (child->type == 0)
					acceptAssetDropInto(child->fullPath);

				// Handle double-click
				if (gui->isItemHovered() && gui->isMouseDoubleClicked(ImGuiMouseButton_Left))
				{
				    if (child->type == 0)
                    {
                        manager->openFolder(child->name);
                        needRefreshList = true;
                        gui->columnsEnd();
                        gui->popId();
                        gui->groupEnd();
                        return;
                    }
                    else
                    {
                        if (onFileDoubleClicked)
                            onFileDoubleClicked(child->fullPath.string());
                    }
				}

				// Per-item right-click context menu
				if (ImGui::BeginPopupContextItem("##ItemCtx"))
				{
					if (!child->isSelected)
					{
						deselectAll(rootThumbnail);
						child->isSelected = true;
					}
					if (ImGui::MenuItem("Rename"))
						beginRename(child->fullPath, child->name);
					if (ImGui::MenuItem("Duplicate"))
						{ manager->duplicateAsset(child->fullPath); needRefreshList = true; }
					if (ImGui::MenuItem("Delete"))
						executeDeleteSelected();
					ImGui::Separator();
					if (ImGui::MenuItem("Reimport"))
						{ manager->reimportAsset(child->uuid); needRefreshList = true; }
					ImGui::Separator();
					// "Create Animation" for mesh files
					if (child->type == 1 && !child->uuid.empty())
					{
						auto it = manager->fileMap.find(child->uuid);
						if (it != manager->fileMap.end() && it->second.type == "mesh")
						{
							if (ImGui::MenuItem("Create Animation"))
							{
								manager->createNewAnimationFromMesh(child->uuid, child->fullPath);
								needRefreshList = true;
							}
						}
					}
					ImGui::EndPopup();
				}

				kString displayName = fitTextWithEllipsisUtf8(gui, child->name, columnWidth - 4.0f);

				if (displayName != child->name && gui->isItemHovered())
				{
					gui->beginTooltip();
					gui->textUnformatted(child->name);
					gui->endTooltip();
				}

				kVec2 cellPos  = gui->getItemRectMin();
				float iconX = cellPos.x + (columnWidth - thumbSize) * 0.5f;
				float iconY = cellPos.y + topMargin;
				gui->setCursorScreenPos(kVec2(iconX, iconY));
				ImGui::Image(child->icon, ImVec2(thumbSize, thumbSize));

				float textWidth = gui->calcTextSize(displayName).x;
				float textX = cellPos.x + (columnWidth - textWidth) * 0.5f;
				float textY = iconY + thumbSize + 2.0f;
				gui->setCursorScreenPos(kVec2(textX, textY));
				gui->textUnformatted(displayName);

				gui->popId();
				gui->groupEnd();
				gui->nextColumn();
			}

			gui->columnsEnd();
		}
		else
		{
			kString text = "Empty folder";
			float textWidth = gui->calcTextSize(text).x;
			float columnWidth = gui->getColumnWidth();
			float textX = gui->getCursorPosX() + (columnWidth - textWidth) * 0.5f;
			gui->setCursorPosX(textX);
			gui->text(text);
		}

		// Right-click context menu on empty space
		if (ImGui::BeginPopupContextWindow("##ProjectContextMenu",
		        ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::BeginMenu("Create"))
			{
				if (ImGui::MenuItem("Folder"))
					{ manager->createNewFolder();     needRefreshList = true; }
				if (ImGui::MenuItem("Shader"))
					{ manager->createNewShader();     needRefreshList = true; }
				if (ImGui::MenuItem("Raw Shader"))
					{ manager->createNewRawShader();  needRefreshList = true; }
				if (ImGui::MenuItem("Material"))
					{ manager->createNewMaterial();   needRefreshList = true; }
				if (ImGui::MenuItem("Script"))
					{ manager->createNewScript();     needRefreshList = true; }
				if (ImGui::MenuItem("Logic Graph"))
					{ manager->createNewLogicGraph(); needRefreshList = true; }
				ImGui::Separator();
				if (ImGui::MenuItem("Animator"))
					{ manager->createNewAnimator();   needRefreshList = true; }
				if (ImGui::MenuItem("Cinematic Clip"))
					{ manager->createNewAnimation();  needRefreshList = true; }
				if (ImGui::MenuItem("UI"))
					{ manager->createNewGui();        needRefreshList = true; }
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Show in Explorer"))
			{
				fs::path dir = manager->projectPath / "Assets";
				for (size_t i = 1; i < manager->currentDir.size(); ++i)
					dir /= manager->currentDir[i];
	#ifdef _WIN32
				ShellExecuteA(nullptr, "open", "explorer", ("/select," + dir.string()).c_str(), nullptr, SW_SHOW);
	#elif __APPLE__
				system(("open \"" + dir.string() + "\"").c_str());
	#else
				system(("xdg-open \"" + dir.string() + "\"").c_str());
	#endif
			}
			ImGui::EndPopup();
		}
	}
	else
	{
		kString text = "No project found";
		float textWidth = gui->calcTextSize(text).x;
		float columnWidth = gui->getColumnWidth();
		float textX = gui->getCursorPosX() + (columnWidth - textWidth) * 0.5f;
		gui->setCursorPosX(textX);
		gui->text(text);
	}
}

void PanelProject::executeDeleteSelected()
{
	auto sel = getProjectSelection();
	if (sel.count == 0) return;

	std::string msg = (sel.count == 1)
		? "Delete \"" + sel.name + "\"?\nThis action cannot be undone."
		: "Delete " + std::to_string(sel.count) + " selected asset(s)?\nThis action cannot be undone.";

	auto result = pfd::message("Confirm Delete", msg, pfd::choice::yes_no, pfd::icon::warning).result();
	if (result != pfd::button::yes) return;

	std::vector<fs::path> paths;

	std::function<void(const Node&)> collect = [&](const Node& n) {
		for (auto& child : n.children) {
			if (child->isSelected) {
				if (!child->fullPath.empty())
					paths.push_back(child->fullPath);
			} else {
				collect(*child);
			}
		}
	};

	collect(displayThumbnail ? rootThumbnail : rootTree);

	if (!paths.empty())
	{
		manager->deleteAssets(paths);
		clearSelection();
		needRefreshList = true;
	}
}

void PanelProject::beginRename(const fs::path& path, const kString& name)
{
	renameTargetPath = path;
	strncpy_s(renameBuffer, sizeof(renameBuffer), name.c_str(), _TRUNCATE);
	renameBuffer[sizeof(renameBuffer) - 1] = '\0';
	openRenamePopup = true;
}

void PanelProject::drawRenameModal()
{
	if (openRenamePopup)
	{
		ImGui::OpenPopup("Rename Asset");
		openRenamePopup = false;
	}

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("New name:");

		// Focus the field when the popup opens and, on that first focused frame,
		// pre-select just the file stem (the name before the last '.') so typing
		// replaces the name but keeps the extension instead of selecting it too.
		static bool selectStem = false;
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
			selectStem = true;
		}

		struct RenameSelect
		{
			static int cb(ImGuiInputTextCallbackData *data)
			{
				bool *want = static_cast<bool *>(data->UserData);
				if (want && *want)
				{
					int stem = data->BufTextLen;
					for (int i = data->BufTextLen - 1; i > 0; --i)
						if (data->Buf[i] == '.') { stem = i; break; }
					data->CursorPos      = stem;
					data->SelectionStart = 0;
					data->SelectionEnd   = stem;
					*want = false;
				}
				return 0;
			}
		};

		ImGui::SetNextItemWidth(320.0f);
		bool commit = ImGui::InputText("##renameinput", renameBuffer, sizeof(renameBuffer),
		                               ImGuiInputTextFlags_EnterReturnsTrue |
		                               ImGuiInputTextFlags_CallbackAlways,
		                               RenameSelect::cb, &selectStem);

		ImGui::Spacing();
		bool doRename = ImGui::Button("Rename", ImVec2(120, 0)) || commit;
		ImGui::SameLine();
		bool doCancel = ImGui::Button("Cancel", ImVec2(120, 0));

		if (doRename)
		{
			kString newName = renameBuffer;
			if (!newName.empty() && !renameTargetPath.empty() &&
			    manager->renameAsset(renameTargetPath, newName))
			{
				clearSelection();
				needRefreshList = true;
			}
			ImGui::CloseCurrentPopup();
		}
		else if (doCancel)
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

void PanelProject::drawBreadcrumb()
{
	if (manager->currentDir.size() > 0)
	{
		// Start from project root
		fs::path basePath = manager->projectPath;

		// Make the breadcrumb button just submitted accept dragged file assets
		// and move them into `targetDir` on drop. The file's UUID lives in its
		// JSON content, so moving the file preserves it.
		auto acceptDropInto = [&](const fs::path& targetDir) { acceptAssetDropInto(targetDir); };

		// "Assets" — root of the project's asset tree.
		if (gui->button("Assets"))
		{
			manager->currentDir.erase(manager->currentDir.begin() + 1, manager->currentDir.end());
			needRefreshList = true;
		}
		acceptDropInto(basePath / manager->currentDir[0]);

		fs::path segPath = basePath / manager->currentDir[0];
		for (size_t i = 1; i < manager->currentDir.size(); ++i)
		{
			gui->sameLine();
			gui->setCursorPosX(gui->getCursorPosX() - 3.0f);
			gui->text(">");
			gui->sameLine();
			gui->setCursorPosX(gui->getCursorPosX() - 4.0f);

			segPath /= manager->currentDir[i];

			if (gui->button(manager->currentDir[i]))
			{
				manager->currentDir.erase(manager->currentDir.begin() + i + 1, manager->currentDir.end());
				needRefreshList = true;
			}
			acceptDropInto(segPath);
		}

		//gui->dummy(kVec2(0.0f, 0.0f));
	}
}

