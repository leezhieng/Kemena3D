#ifndef PANEL_PROJECT_H
#define PANEL_PROJECT_H

#include "kemena/kemena.h"

#include "manager.h"
#include "util.h"

#include <GL/glew.h>
#include <imgui.h>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <unordered_map>

using namespace kemena;
namespace fs = std::filesystem;

class Manager;

/**
 * @brief Editor panel that displays and manages the project's asset folder.
 *
 * Presents the on-disk asset hierarchy as a navigable tree and/or thumbnail
 * grid, supporting selection, renaming, deletion, search filtering and
 * double-click activation of files. Thumbnails are generated and cached per
 * asset UUID through the associated kAssetManager.
 */
class PanelProject
{
	private:
		ImTextureRef iconUp;   ///< Toolbar icon for navigating up one folder.
		uint32_t iconAdd;  ///< Toolbar icon for the create/add menu.
		ImTextureRef iconMag;  ///< Search (magnifier) icon.

		ImTextureRef iconFolder;   ///< Default icon for folders.
		ImTextureRef iconText;     ///< Icon for text/document assets.
		ImTextureRef iconImage;    ///< Icon for image assets.
		ImTextureRef iconScript;   ///< Icon for script assets.
		ImTextureRef iconAudio;    ///< Icon for audio assets.
		ImTextureRef iconVideo;    ///< Icon for video assets.
		ImTextureRef iconAnimation; ///< Icon for .animation clip assets.
		ImTextureRef iconModel;    ///< Icon for 3D model/mesh assets.
		ImTextureRef iconPrefab;   ///< Icon for prefab assets.
		ImTextureRef iconWorld;    ///< Icon for world/scene assets.
		ImTextureRef iconMaterial; ///< Icon for material assets.
		ImTextureRef iconLogic;         ///< Icon for logic/node-graph assets.
		ImTextureRef iconShader;        ///< Icon for shader assets (.shader).
		ImTextureRef iconShaderScript;  ///< Icon for shader source files (.glsl, .hlsl).
		ImTextureRef iconParticle;      ///< Icon for particle system assets (.particle).
		ImTextureRef iconGui;           ///< Icon for GUI/UI assets.
		ImTextureRef iconOther;         ///< Fallback icon for unrecognised asset types.

		char searchBuffer[128] = ""; ///< Backing buffer for the search/filter text field.

		ImVec4 upTint = ImVec4(1, 1, 1, 1);  ///< Tint applied to the "up" toolbar button.
		kVec4 addTint = kVec4(1, 1, 1, 1); ///< Tint applied to the "add" toolbar button.

		ImTextureRef iconList;      ///< Icon for the list-view toggle.
		ImTextureRef iconThumbnail; ///< Icon for the thumbnail-view toggle.

		bool displayThumbnail = true; ///< When true the thumbnail grid is shown; otherwise the list view.

		/**
		 * @brief A single entry (folder or file) in the project asset hierarchy.
		 *
		 * Used both for the side tree (folders only) and the thumbnail grid
		 * (current folder contents). Owns its children via unique_ptr.
		 */
		struct Node
		{
			kString name;                              ///< Display name of the entry.
			bool isSelected = false;                   ///< Whether this entry is currently selected.
			std::vector<std::unique_ptr<Node>> children; ///< Owned child entries.

			kString uuid;                ///< Asset UUID (empty for plain folders).
			ImTextureRef icon = nullptr; ///< Icon used to render this entry.
			int type = 0;                ///< Entry kind: 0 = Folder, 1 = File.
			fs::path fullPath;           ///< Absolute filesystem path of the entry.

			/**
			 * @brief Constructs a node.
			 * @param n Display name.
			 * @param g Asset UUID (empty for folders).
			 * @param i Icon texture reference.
			 * @param t Entry kind (0 = Folder, 1 = File).
			 */
			Node(const kString& n, const kString& g, ImTextureRef i = nullptr, int t = 0) : name(n), uuid(g), icon(i), type(t) {}
		};

		Node rootTree;             ///< Root of the folder tree shown in the side panel.
		Node rootThumbnail;        ///< Root for the currently browsed folder's thumbnail contents.
		bool needRefreshList = false; ///< Set when the on-disk view must be rebuilt on the next frame.

		// Rename modal state.
		bool     openRenamePopup = false; ///< Whether the rename modal should open this frame.
		fs::path renameTargetPath;        ///< Filesystem path of the asset being renamed.
		char     renameBuffer[256] = "";  ///< Backing buffer for the rename text field.

		// Prefab-from-instance confirmation popup state.
		kString  pendingPrefabObjUuid;     ///< UUID of the prefab instance being dragged for new prefab creation.
		fs::path pendingPrefabTargetDir;   ///< Target directory where the new prefab should be saved.

		kAssetManager* assetManager = nullptr; ///< Asset manager used to resolve and generate thumbnails.
		std::unordered_map<kString, std::pair<uint32_t, ImTextureRef>> thumbnailCache; ///< Cache of generated thumbnails keyed by asset UUID (texture handle + ImGui ref).

	public:
		/**
		 * @brief Returns the thumbnail icon for an asset, generating and caching it if needed.
		 * @param uuid UUID of the asset whose thumbnail is requested.
		 * @param defaultIcon Icon to fall back to when no thumbnail can be produced.
		 * @return Texture reference to the asset's thumbnail or the supplied default.
		 */
		ImTextureRef getThumbnailIcon(const kString& uuid, ImTextureRef defaultIcon);

	private:
		/** @brief Releases all cached thumbnail GL textures and empties the cache. */
		void clearThumbnailCache();

		/**
		 * @brief Invalidates the cached thumbnail for a single asset so it is regenerated.
		 * @param uuid UUID of the asset whose thumbnail should be discarded.
		 */
		void invalidateThumbnail(const kString& uuid);

	public:
		/**
		 * @brief Constructs the project panel.
		 * @param setGuiManager GUI manager used for rendering and icon loading.
		 * @param setManager Owning editor manager providing shared editor state.
		 * @param assetManager Asset manager backing asset resolution and thumbnails.
		 */
	    PanelProject(kGuiManager* setGuiManager, Manager* setManager, kAssetManager* assetManager);

	    /** @brief Requests a rebuild of the asset views on the next frame. */
	    void triggerRefresh() { needRefreshList = true; }

	    /**
	     * @brief Callback invoked when a file is double-clicked.
	     *
	     * Receives the full filesystem path of the activated file.
	     */
	    std::function<void(const std::string&)> onFileDoubleClicked;

	    kString pendingSelectUuid; ///< UUID of an asset to select once the view is next refreshed.

		/**
		 * @brief Snapshot of the panel's current asset selection.
		 *
		 * Describes how many assets are selected and, for a single selection,
		 * the details needed by other panels (e.g. the inspector).
		 */
		struct SelectedProjectAsset
		{
			int          count    = 0;      ///< Number of selected assets: 0 = none, 1 = single, >1 = multi.
			kString      name;              ///< Name of the selected asset (single selection).
			kString      uuid;              ///< UUID of the selected asset (single selection).
			kString      fileType;          ///< Asset type string ("mesh", "image", ...); empty for folders.
			bool         isFolder = false;  ///< Whether the single selection is a folder.
			ImTextureRef thumbnail = nullptr; ///< Thumbnail of the selected asset.
			fs::path     metaPath;          ///< Path to the asset's metadata file.
		};

		/**
		 * @brief Returns a snapshot of the current selection.
		 * @return Populated SelectedProjectAsset describing the active selection.
		 */
		SelectedProjectAsset getProjectSelection() const;

		/**
		 * @brief Recursively clears the selection flag on a node and its descendants.
		 * @param root Subtree root to deselect.
		 */
		void deselectAll(Node& root);

		/** @brief Clears the current asset selection across all views. */
		void clearSelection();

		/**
		 * @brief Applies a click to the selection honouring Ctrl/Shift modifiers.
		 *
		 * Plain click selects only @p clicked; Ctrl toggles it within the current
		 * selection; Shift selects the contiguous range (in depth-first display
		 * order) between the anchor and @p clicked. The anchor is the last item
		 * single- or Ctrl-clicked.
		 * @param root    Active view root (rootThumbnail or rootTree).
		 * @param clicked The node that was clicked.
		 */
		void handleSelectionClick(Node& root, Node& clicked);

		/**
		 * @brief Begins an ImGui drag carrying every selected file asset's UUID.
		 *
		 * If @p node isn't part of the current selection it becomes the sole
		 * selection first. The payload ("PROJECT_ASSET") is a newline-separated
		 * list of UUIDs so multiple files can be moved at once.
		 * @param node The node the drag started on.
		 */
		void beginAssetDragSource(Node& node);

		/**
		 * @brief Drop target that moves every dragged asset UUID into @p targetDir.
		 * @param targetDir Destination folder for the dropped assets.
		 */
		void acceptAssetDropInto(const fs::path& targetDir);

		/// UUID of the selection anchor used for Shift range-selection.
		kString selectionAnchorUuid;

		/// List-view only: a plain click on an already-selected row is deferred to
		/// mouse-release (so pressing to start a drag keeps the whole selection).
		/// Holds that row's UUID between press and release; cleared if a drag starts.
		kString clickPendingUuid;

		/**
		 * @brief Draws the full project panel contents (tree and thumbnail views).
		 * @param rootTree Folder tree root to render.
		 * @param rootThumbnail Current folder's thumbnail-grid root to render.
		 * @param opened Pointer to the window's open state, toggled by the close button.
		 */
		void drawProjectPanel(Node& rootTree, Node& rootThumbnail, bool* opened);

		/**
		 * @brief Renders the project panel window.
		 * @param opened Reference to the window's open state.
		 */
		void draw(bool& opened);

		/** @brief Rebuilds the folder tree from the project's asset directory. */
		void refreshTreeList();

		/**
		 * @brief Recursively draws a folder tree node and its children.
		 * @param node Node to render.
		 * @param rootTree Tree root, used for selection bookkeeping.
		 * @param level Current indentation depth (0 for the root level).
		 */
		void drawTreeNode(Node& node, Node& rootTree, int level = 0);

		/**
		 * @brief Recursively populates a node's children from a filesystem path.
		 * @param parent Node to populate.
		 * @param fullPath Directory on disk to scan.
		 */
		void populateTree(Node& parent, const fs::path& fullPath);

		/** @brief Rebuilds the thumbnail grid for the currently browsed folder. */
		void refreshThumbnailList();

		/**
		 * @brief Draws the thumbnail grid for the contents of a directory node.
		 * @param currentDir Directory node whose contents are rendered.
		 */
		void drawThumbnailNode(const Node& currentDir);

		/** @brief Draws the breadcrumb navigation bar for the current folder path. */
		void drawBreadcrumb();

		/** @brief Deletes the currently selected assets from disk and refreshes the views. */
		void executeDeleteSelected();

		/**
		 * @brief Begins a rename operation for the given asset and opens the rename modal.
		 * @param path Filesystem path of the asset to rename.
		 * @param name Current name pre-filled into the rename field.
		 */
		void beginRename(const fs::path& path, const kString& name);

		/** @brief Renders the rename modal dialog and applies the rename on confirmation. */
		void drawRenameModal();

		bool focused = false; ///< True while the project window owns ImGui focus (set each draw()).

		Manager* manager;  ///< Owning editor manager providing shared editor state.
		kGuiManager* gui;  ///< GUI manager used for rendering and icon loading.
};

#endif

