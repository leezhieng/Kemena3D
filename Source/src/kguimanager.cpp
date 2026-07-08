#include "kguimanager.h"

#include <fstream>
#include <cstdlib>

namespace kemena
{
	kGuiManager::kGuiManager()
	{
		// ctor
	}

	kGuiManager::~kGuiManager()
	{
		// dtor
	}

	void kGuiManager::init(kRenderer *newRenderer)
	{
		renderer = newRenderer;

		float mainScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO &io = ImGui::GetIO();
		(void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		io.IniFilename = nullptr; // Disable imgui.ini

		ImGui::GetStyle().WindowMinSize = ImVec2(200, 200); // Minimum window size

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();
		// ImGui::StyleColorsLight();

		// Disable hide tab bar button
		ImGui::GetStyle().WindowMenuButtonPosition = ImGuiDir_None;

		// Setup scaling
		ImGuiStyle &style = ImGui::GetStyle();
		style.ScaleAllSizes(mainScale); // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
		style.FontScaleDpi = mainScale; // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

		// Setup Platform/Renderer backends
		ImGui_ImplSDL3_InitForOpenGL(renderer->getWindow()->getSdlWindow(), (SDL_GLContext)renderer->getDriver()->getNativeContext());
		const char *glsl_version = "#version 150";
		ImGui_ImplOpenGL3_Init(glsl_version);
	}

	void kGuiManager::processEvent(kSystemEvent event)
	{
		ImGui_ImplSDL3_ProcessEvent(event.getSdlEvent());
	}

	void kGuiManager::loadDefaultFontFromResource(kString resourceName)
	{
		void* fontData = nullptr;
		size_t dataSize = 0;

#ifdef _WIN32
		HRSRC hRes = FindResource(NULL, resourceName.c_str(), RT_RCDATA);
		if (!hRes) return;
		HGLOBAL hData = LoadResource(NULL, hRes);
		if (!hData) return;
		dataSize = SizeofResource(NULL, hRes);
		void* pData = LockResource(hData);
		if (pData && dataSize > 0)
		{
			fontData = malloc(dataSize);
			memcpy(fontData, pData, dataSize);
		}
#else
		// Read from Resources/ directory next to the executable
		const char *base = SDL_GetBasePath();
		if (!base) return;
		kString path = kString(base) + "Resources/" + resourceName;
		SDL_free(const_cast<char *>(base));

		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f) return;
		dataSize = static_cast<size_t>(f.tellg());
		f.seekg(0);
		fontData = malloc(dataSize);
		f.read(static_cast<char*>(fontData), dataSize);
		if (!f.good()) { free(fontData); return; }
#endif

		if (fontData && dataSize > 0)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.Fonts->AddFontFromMemoryTTF(fontData, dataSize, 16.0f);
			// fontData must remain alive until ImGui::DestroyContext()
		}
	}

	void kGuiManager::canvasStart()
	{
		renderer->getDriver()->setDepthTest(false);
		renderer->getDriver()->setCullFace(false);

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
	}

	void kGuiManager::canvasEnd()
	{
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// IMPORTANT: multi-viewport handling
		ImGuiIO &io = ImGui::GetIO();
		(void)io;
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			SDL_Window *backup_current_window = SDL_GL_GetCurrentWindow();
			SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();

			SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
		}
	}

	// ---- Window ----

	void kGuiManager::windowStart(kString title, bool *open, ImGuiWindowFlags flags)
	{
		ImGui::Begin(title.c_str(), open, flags);

		// ImGui only brings a window forward on left click. Mirror that for the
		// middle and right buttons so a click with any mouse button inside a
		// panel focuses it (and, when docked, raises its tab).
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
			(ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
			 ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
		{
			ImGui::SetWindowFocus();
		}
	}

	void kGuiManager::windowEnd()
	{
		ImGui::End();
	}

	kVec2 kGuiManager::getWindowSize()
	{
		ImVec2 s = ImGui::GetWindowSize();
		return kVec2(s.x, s.y);
	}

	kVec2 kGuiManager::getWindowPos()
	{
		ImVec2 p = ImGui::GetWindowPos();
		return kVec2(p.x, p.y);
	}

	void kGuiManager::setNextWindowSize(kVec2 size, ImGuiCond cond)
	{
		ImGui::SetNextWindowSize(ImVec2(size.x, size.y), cond);
	}

	void kGuiManager::setNextWindowPos(kVec2 pos, ImGuiCond cond, kVec2 pivot)
	{
		ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y), cond, ImVec2(pivot.x, pivot.y));
	}

	void kGuiManager::setNextWindowContentSize(kVec2 size)
	{
		ImGui::SetNextWindowContentSize(ImVec2(size.x, size.y));
	}

	void kGuiManager::setNextWindowCollapsed(bool collapsed, ImGuiCond cond)
	{
		ImGui::SetNextWindowCollapsed(collapsed, cond);
	}

	void kGuiManager::setNextWindowFocus()
	{
		ImGui::SetNextWindowFocus();
	}

	void kGuiManager::setNextWindowBgAlpha(float alpha)
	{
		ImGui::SetNextWindowBgAlpha(alpha);
	}

	bool kGuiManager::isWindowFocused(ImGuiFocusedFlags flags)
	{
		return ImGui::IsWindowFocused(flags);
	}

	bool kGuiManager::isWindowHovered(ImGuiHoveredFlags flags)
	{
		return ImGui::IsWindowHovered(flags);
	}

	// ---- Dockspace ----

	void kGuiManager::dockSpaceStart(kString name)
	{
		bool show_main_dockspace = true;
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

		// Get main viewport
		const ImGuiViewport *viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);

		// Remove window decorations and make it fixed
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		// Remove padding
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin(name.c_str(), &show_main_dockspace, window_flags);
		ImGui::PopStyleVar();

		ImGuiID dockspaceID = ImGui::GetID(name.c_str());
		ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

		ImGui::DockSpaceOverViewport();
	}

	void kGuiManager::dockSpaceEnd()
	{
		ImGui::End();
	}

	// ---- Menu ----

	bool kGuiManager::menuBar()
	{
		return ImGui::BeginMainMenuBar();
	}

	void kGuiManager::menuBarEnd()
	{
		ImGui::EndMainMenuBar();
	}

	bool kGuiManager::menu(kString text)
	{
		return ImGui::BeginMenu(text.c_str());
	}

	void kGuiManager::menuEnd()
	{
		ImGui::EndMenu();
	}

	bool kGuiManager::menuItem(kString text, kString shortcut, bool selected, bool enabled)
	{
		return ImGui::MenuItem(text.c_str(), shortcut.c_str(), selected, enabled);
	}

	bool kGuiManager::menuItem(kString text, kString shortcut, bool *selected, bool enabled)
	{
		return ImGui::MenuItem(text.c_str(), shortcut.c_str(), selected, enabled);
	}

	// ---- Layout ----

	void kGuiManager::groupStart()
	{
		ImGui::BeginGroup();
	}

	void kGuiManager::groupEnd()
	{
		ImGui::EndGroup();
	}

	void kGuiManager::sameLine(float offsetFromStartX, float spacing)
	{
		ImGui::SameLine(offsetFromStartX, spacing);
	}

	void kGuiManager::spacing()
	{
		ImGui::Spacing();
	}

	void kGuiManager::separator()
	{
		ImGui::Separator();
	}

	void kGuiManager::separatorText(kString text)
	{
		ImGui::SeparatorText(text.c_str());
	}

	void kGuiManager::newLine()
	{
		ImGui::NewLine();
	}

	void kGuiManager::indent(float indentW)
	{
		ImGui::Indent(indentW);
	}

	void kGuiManager::unindent(float indentW)
	{
		ImGui::Unindent(indentW);
	}

	void kGuiManager::dummy(kVec2 size)
	{
		ImGui::Dummy(ImVec2(size.x, size.y));
	}

	void kGuiManager::setCursorPos(kVec2 pos)
	{
		ImGui::SetCursorPos(ImVec2(pos.x, pos.y));
	}

	void kGuiManager::setCursorPosX(float x)
	{
		ImGui::SetCursorPosX(x);
	}

	void kGuiManager::setCursorPosY(float y)
	{
		ImGui::SetCursorPosY(y);
	}

	kVec2 kGuiManager::getCursorPos()
	{
		ImVec2 p = ImGui::GetCursorPos();
		return kVec2(p.x, p.y);
	}

	float kGuiManager::getCursorPosX()
	{
		return ImGui::GetCursorPosX();
	}

	float kGuiManager::getCursorPosY()
	{
		return ImGui::GetCursorPosY();
	}

	kVec2 kGuiManager::getCursorScreenPos()
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		return kVec2(p.x, p.y);
	}

	kVec2 kGuiManager::getContentRegionAvail()
	{
		ImVec2 s = ImGui::GetContentRegionAvail();
		return kVec2(s.x, s.y);
	}

	void kGuiManager::setNextItemWidth(float itemWidth)
	{
		ImGui::SetNextItemWidth(itemWidth);
	}

	void kGuiManager::pushItemWidth(float itemWidth)
	{
		ImGui::PushItemWidth(itemWidth);
	}

	void kGuiManager::popItemWidth()
	{
		ImGui::PopItemWidth();
	}

	float kGuiManager::calcItemWidth()
	{
		return ImGui::CalcItemWidth();
	}

	void kGuiManager::pushTextWrapPos(float wrapLocalPosX)
	{
		ImGui::PushTextWrapPos(wrapLocalPosX);
	}

	void kGuiManager::popTextWrapPos()
	{
		ImGui::PopTextWrapPos();
	}

	// ---- ID Stack ----

	void kGuiManager::pushId(kString id)
	{
		ImGui::PushID(id.c_str());
	}

	void kGuiManager::pushId(int id)
	{
		ImGui::PushID(id);
	}

	void kGuiManager::pushId(const void *ptr)
	{
		ImGui::PushID(ptr);
	}

	void kGuiManager::popId()
	{
		ImGui::PopID();
	}

	// ---- Style ----

	void kGuiManager::pushStyleColor(ImGuiCol idx, kVec4 color)
	{
		ImGui::PushStyleColor(idx, ImVec4(color.r, color.g, color.b, color.a));
	}

	void kGuiManager::pushStyleColor(ImGuiCol idx, ImU32 color)
	{
		ImGui::PushStyleColor(idx, color);
	}

	void kGuiManager::popStyleColor(int count)
	{
		ImGui::PopStyleColor(count);
	}

	void kGuiManager::pushStyleVar(ImGuiStyleVar idx, float val)
	{
		ImGui::PushStyleVar(idx, val);
	}

	void kGuiManager::pushStyleVar(ImGuiStyleVar idx, kVec2 val)
	{
		ImGui::PushStyleVar(idx, ImVec2(val.x, val.y));
	}

	void kGuiManager::popStyleVar(int count)
	{
		ImGui::PopStyleVar(count);
	}

	void kGuiManager::pushFont(ImFont *font)
	{
		ImGui::PushFont(font);
	}

	void kGuiManager::popFont()
	{
		ImGui::PopFont();
	}

	// ---- Text ----

	void kGuiManager::text(kString text)
	{
		ImGui::Text(text.c_str());
	}

	void kGuiManager::textColored(kVec4 color, kString text)
	{
		ImGui::TextColored(ImVec4(color.r, color.g, color.b, color.a), text.c_str());
	}

	void kGuiManager::textDisabled(kString text)
	{
		ImGui::TextDisabled(text.c_str());
	}

	void kGuiManager::alignTextToFramePadding()
	{
		ImGui::AlignTextToFramePadding();
	}

	void kGuiManager::textWrapped(kString text)
	{
		ImGui::TextWrapped(text.c_str());
	}

	void kGuiManager::labelText(kString label, kString text)
	{
		ImGui::LabelText(label.c_str(), text.c_str());
	}

	void kGuiManager::bulletText(kString text)
	{
		ImGui::BulletText(text.c_str());
	}

	void kGuiManager::bullet()
	{
		ImGui::Bullet();
	}

	// ---- Buttons ----

	bool kGuiManager::button(kString text, kIvec2 size)
	{
		if (size == kIvec2(0, 0))
			return ImGui::Button(text.c_str());
		else
			return ImGui::Button(text.c_str(), ImVec2((float)size.x, (float)size.y));
	}

	bool kGuiManager::smallButton(kString text)
	{
		return ImGui::SmallButton(text.c_str());
	}

	bool kGuiManager::invisibleButton(kString id, kVec2 size, ImGuiButtonFlags flags)
	{
		return ImGui::InvisibleButton(id.c_str(), ImVec2(size.x, size.y), flags);
	}

	bool kGuiManager::arrowButton(kString id, ImGuiDir dir)
	{
		return ImGui::ArrowButton(id.c_str(), dir);
	}

	bool kGuiManager::radioButton(kString label, bool active)
	{
		return ImGui::RadioButton(label.c_str(), active);
	}

	bool kGuiManager::radioButton(kString label, int *v, int vButton)
	{
		return ImGui::RadioButton(label.c_str(), v, vButton);
	}

	// ---- Checkbox ----

	bool kGuiManager::checkbox(kString text, bool *output)
	{
		return ImGui::Checkbox(text.c_str(), output);
	}

	bool kGuiManager::checkboxFlags(kString label, int *flags, int flagsValue)
	{
		return ImGui::CheckboxFlags(label.c_str(), flags, flagsValue);
	}

	bool kGuiManager::checkboxFlags(kString label, unsigned int *flags, unsigned int flagsValue)
	{
		return ImGui::CheckboxFlags(label.c_str(), flags, flagsValue);
	}

	// ---- Input Text ----

	bool kGuiManager::inputText(kString label, kString &value, size_t maxLength, ImGuiInputTextFlags flags)
	{
		std::vector<char> buf(maxLength + 1, 0);
		strncpy(buf.data(), value.c_str(), maxLength);
		buf[maxLength] = '\0';
		if (ImGui::InputText(label.c_str(), buf.data(), maxLength + 1, flags))
		{
			value = buf.data();
			return true;
		}
		return false;
	}

	bool kGuiManager::inputTextMultiline(kString label, kString &value, size_t maxLength, kVec2 size, ImGuiInputTextFlags flags)
	{
		std::vector<char> buf(maxLength + 1, 0);
		strncpy(buf.data(), value.c_str(), maxLength);
		buf[maxLength] = '\0';
		if (ImGui::InputTextMultiline(label.c_str(), buf.data(), maxLength + 1, ImVec2(size.x, size.y), flags))
		{
			value = buf.data();
			return true;
		}
		return false;
	}

	bool kGuiManager::inputTextWithHint(kString label, kString hint, kString &value, size_t maxLength, ImGuiInputTextFlags flags)
	{
		std::vector<char> buf(maxLength + 1, 0);
		strncpy(buf.data(), value.c_str(), maxLength);
		buf[maxLength] = '\0';
		if (ImGui::InputTextWithHint(label.c_str(), hint.c_str(), buf.data(), maxLength + 1, flags))
		{
			value = buf.data();
			return true;
		}
		return false;
	}

	// ---- Input Scalar ----

	bool kGuiManager::inputFloat(kString label, float *v, float step, float stepFast, kString format, ImGuiInputTextFlags flags)
	{
		return ImGui::InputFloat(label.c_str(), v, step, stepFast, format.c_str(), flags);
	}

	bool kGuiManager::inputFloat2(kString label, float v[2], kString format, ImGuiInputTextFlags flags)
	{
		return ImGui::InputFloat2(label.c_str(), v, format.c_str(), flags);
	}

	bool kGuiManager::inputFloat3(kString label, float v[3], kString format, ImGuiInputTextFlags flags)
	{
		return ImGui::InputFloat3(label.c_str(), v, format.c_str(), flags);
	}

	bool kGuiManager::inputFloat4(kString label, float v[4], kString format, ImGuiInputTextFlags flags)
	{
		return ImGui::InputFloat4(label.c_str(), v, format.c_str(), flags);
	}

	bool kGuiManager::inputInt(kString label, int *v, int step, int stepFast, ImGuiInputTextFlags flags)
	{
		return ImGui::InputInt(label.c_str(), v, step, stepFast, flags);
	}

	bool kGuiManager::inputInt2(kString label, int v[2], ImGuiInputTextFlags flags)
	{
		return ImGui::InputInt2(label.c_str(), v, flags);
	}

	bool kGuiManager::inputInt3(kString label, int v[3], ImGuiInputTextFlags flags)
	{
		return ImGui::InputInt3(label.c_str(), v, flags);
	}

	bool kGuiManager::inputInt4(kString label, int v[4], ImGuiInputTextFlags flags)
	{
		return ImGui::InputInt4(label.c_str(), v, flags);
	}

	bool kGuiManager::inputDouble(kString label, double *v, double step, double stepFast, kString format, ImGuiInputTextFlags flags)
	{
		return ImGui::InputDouble(label.c_str(), v, step, stepFast, format.c_str(), flags);
	}

	// ---- Drag ----

	bool kGuiManager::dragFloat(kString label, float *v, float speed, float min, float max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::DragFloat(label.c_str(), v, speed, min, max, format.c_str(), flags);
	}

	bool kGuiManager::dragFloat2(kString label, float v[2], float speed, float min, float max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::DragFloat2(label.c_str(), v, speed, min, max, format.c_str(), flags);
	}

	bool kGuiManager::dragFloat3(kString label, float v[3], float speed, float min, float max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::DragFloat3(label.c_str(), v, speed, min, max, format.c_str(), flags);
	}

	bool kGuiManager::dragFloat4(kString label, float v[4], float speed, float min, float max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::DragFloat4(label.c_str(), v, speed, min, max, format.c_str(), flags);
	}

	bool kGuiManager::dragInt(kString label, int *v, float speed, int min, int max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::DragInt(label.c_str(), v, speed, min, max, format.c_str(), flags);
	}

	bool kGuiManager::dragInt2(kString label, int v[2], float speed, int min, int max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::DragInt2(label.c_str(), v, speed, min, max, format.c_str(), flags);
	}

	bool kGuiManager::dragInt3(kString label, int v[3], float speed, int min, int max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::DragInt3(label.c_str(), v, speed, min, max, format.c_str(), flags);
	}

	bool kGuiManager::dragInt4(kString label, int v[4], float speed, int min, int max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::DragInt4(label.c_str(), v, speed, min, max, format.c_str(), flags);
	}

	bool kGuiManager::dragFloatRange2(kString label, float *vCurrentMin, float *vCurrentMax, float speed, float min, float max, kString format, kString formatMax, ImGuiSliderFlags flags)
	{
		return ImGui::DragFloatRange2(label.c_str(), vCurrentMin, vCurrentMax, speed, min, max, format.c_str(), formatMax.empty() ? nullptr : formatMax.c_str(), flags);
	}

	bool kGuiManager::dragIntRange2(kString label, int *vCurrentMin, int *vCurrentMax, float speed, int min, int max, kString format, kString formatMax, ImGuiSliderFlags flags)
	{
		return ImGui::DragIntRange2(label.c_str(), vCurrentMin, vCurrentMax, speed, min, max, format.c_str(), formatMax.empty() ? nullptr : formatMax.c_str(), flags);
	}

	// ---- Slider ----

	bool kGuiManager::sliderFloat(kString label, float *v, float min, float max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::SliderFloat(label.c_str(), v, min, max, format.c_str(), flags);
	}

	bool kGuiManager::sliderFloat2(kString label, float v[2], float min, float max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::SliderFloat2(label.c_str(), v, min, max, format.c_str(), flags);
	}

	bool kGuiManager::sliderFloat3(kString label, float v[3], float min, float max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::SliderFloat3(label.c_str(), v, min, max, format.c_str(), flags);
	}

	bool kGuiManager::sliderFloat4(kString label, float v[4], float min, float max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::SliderFloat4(label.c_str(), v, min, max, format.c_str(), flags);
	}

	bool kGuiManager::sliderAngle(kString label, float *vRad, float vDegreesMin, float vDegreesMax, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::SliderAngle(label.c_str(), vRad, vDegreesMin, vDegreesMax, format.c_str(), flags);
	}

	bool kGuiManager::sliderInt(kString label, int *v, int min, int max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::SliderInt(label.c_str(), v, min, max, format.c_str(), flags);
	}

	bool kGuiManager::sliderInt2(kString label, int v[2], int min, int max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::SliderInt2(label.c_str(), v, min, max, format.c_str(), flags);
	}

	bool kGuiManager::sliderInt3(kString label, int v[3], int min, int max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::SliderInt3(label.c_str(), v, min, max, format.c_str(), flags);
	}

	bool kGuiManager::sliderInt4(kString label, int v[4], int min, int max, kString format, ImGuiSliderFlags flags)
	{
		return ImGui::SliderInt4(label.c_str(), v, min, max, format.c_str(), flags);
	}

	// ---- Color ----

	bool kGuiManager::colorEdit3(kString label, float col[3], ImGuiColorEditFlags flags)
	{
		return ImGui::ColorEdit3(label.c_str(), col, flags);
	}

	bool kGuiManager::colorEdit4(kString label, float col[4], ImGuiColorEditFlags flags)
	{
		return ImGui::ColorEdit4(label.c_str(), col, flags);
	}

	bool kGuiManager::colorPicker3(kString label, float col[3], ImGuiColorEditFlags flags)
	{
		return ImGui::ColorPicker3(label.c_str(), col, flags);
	}

	bool kGuiManager::colorPicker4(kString label, float col[4], ImGuiColorEditFlags flags)
	{
		return ImGui::ColorPicker4(label.c_str(), col, flags);
	}

	bool kGuiManager::colorButton(kString descId, kVec4 col, ImGuiColorEditFlags flags, kVec2 size)
	{
		return ImGui::ColorButton(descId.c_str(), ImVec4(col.r, col.g, col.b, col.a), flags, ImVec2(size.x, size.y));
	}

	void kGuiManager::setColorEditOptions(ImGuiColorEditFlags flags)
	{
		ImGui::SetColorEditOptions(flags);
	}

	// ---- Combo / List / Selectable ----

	bool kGuiManager::combo(kString label, int *currentItem, std::vector<kString> items, int popupMaxHeightInItems)
	{
		std::vector<const char *> cItems;
		cItems.reserve(items.size());
		for (const auto &s : items)
			cItems.push_back(s.c_str());
		return ImGui::Combo(label.c_str(), currentItem, cItems.data(), (int)cItems.size(), popupMaxHeightInItems);
	}

	bool kGuiManager::listBox(kString label, int *currentItem, std::vector<kString> items, int heightInItems)
	{
		std::vector<const char *> cItems;
		cItems.reserve(items.size());
		for (const auto &s : items)
			cItems.push_back(s.c_str());
		return ImGui::ListBox(label.c_str(), currentItem, cItems.data(), (int)cItems.size(), heightInItems);
	}

	bool kGuiManager::selectable(kString label, bool selected, ImGuiSelectableFlags flags, kVec2 size)
	{
		return ImGui::Selectable(label.c_str(), selected, flags, ImVec2(size.x, size.y));
	}

	bool kGuiManager::selectable(kString label, bool *pSelected, ImGuiSelectableFlags flags, kVec2 size)
	{
		return ImGui::Selectable(label.c_str(), pSelected, flags, ImVec2(size.x, size.y));
	}

	// ---- Tree / Collapsing ----

	bool kGuiManager::treeStart(kString label)
	{
		return ImGui::TreeNode(label.c_str());
	}

	bool kGuiManager::treeStartEx(kString id, kString label, ImGuiTreeNodeFlags flags)
	{
		if (label.empty())
			return ImGui::TreeNodeEx(id.c_str(), flags);
		return ImGui::TreeNodeEx(id.c_str(), flags, label.c_str());
	}

	void kGuiManager::treeEnd()
	{
		ImGui::TreePop();
	}

	void kGuiManager::treePop()
	{
		ImGui::TreePop();
	}

	bool kGuiManager::collapsingHeader(kString label, ImGuiTreeNodeFlags flags)
	{
		return ImGui::CollapsingHeader(label.c_str(), flags);
	}

	bool kGuiManager::collapsingHeader(kString label, bool *visible, ImGuiTreeNodeFlags flags)
	{
		return ImGui::CollapsingHeader(label.c_str(), visible, flags);
	}

	void kGuiManager::setNextItemOpen(bool isOpen, ImGuiCond cond)
	{
		ImGui::SetNextItemOpen(isOpen, cond);
	}

	// ---- Image ----

	void kGuiManager::image(GLuint textureId, kVec2 size, kVec2 uv0, kVec2 uv1)
	{
		ImGui::Image((ImTextureID)(intptr_t)textureId, ImVec2(size.x, size.y), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));
	}

	bool kGuiManager::imageButton(kString id, GLuint textureId, kVec2 size, kVec2 uv0, kVec2 uv1, kVec4 tint)
	{
		return ImGui::ImageButton(id.c_str(), (ImTextureID)(intptr_t)textureId, ImVec2(size.x, size.y), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y), ImVec4(0, 0, 0, 0), ImVec4(tint.x, tint.y, tint.z, tint.w));
	}

	// ---- Progress ----

	void kGuiManager::progressBar(float fraction, kVec2 size, kString overlay)
	{
		ImGui::ProgressBar(fraction, ImVec2(size.x, size.y), overlay.empty() ? nullptr : overlay.c_str());
	}

	// ---- Tooltip ----

	void kGuiManager::setItemTooltip(kString text)
	{
		ImGui::SetItemTooltip(text.c_str());
	}

	void kGuiManager::beginTooltip()
	{
		ImGui::BeginTooltip();
	}

	void kGuiManager::endTooltip()
	{
		ImGui::EndTooltip();
	}

	// ---- Popup ----

	void kGuiManager::openPopup(kString id, ImGuiPopupFlags flags)
	{
		ImGui::OpenPopup(id.c_str(), flags);
	}

	bool kGuiManager::popupStart(kString id, ImGuiWindowFlags flags)
	{
		return ImGui::BeginPopup(id.c_str(), flags);
	}

	void kGuiManager::popupEnd()
	{
		ImGui::EndPopup();
	}

	bool kGuiManager::popupModal(kString name, bool *open, ImGuiWindowFlags flags)
	{
		return ImGui::BeginPopupModal(name.c_str(), open, flags);
	}

	bool kGuiManager::popupContextItemStart(kString id, ImGuiPopupFlags flags)
	{
		return ImGui::BeginPopupContextItem(id.empty() ? nullptr : id.c_str(), flags);
	}

	bool kGuiManager::popupContextWindowStart(kString id, ImGuiPopupFlags flags)
	{
		return ImGui::BeginPopupContextWindow(id.empty() ? nullptr : id.c_str(), flags);
	}

	bool kGuiManager::isPopupOpen(kString id, ImGuiPopupFlags flags)
	{
		return ImGui::IsPopupOpen(id.c_str(), flags);
	}

	void kGuiManager::closeCurrentPopup()
	{
		ImGui::CloseCurrentPopup();
	}

	// ---- Tab Bar ----

	bool kGuiManager::tabBarStart(kString id, ImGuiTabBarFlags flags)
	{
		return ImGui::BeginTabBar(id.c_str(), flags);
	}

	void kGuiManager::tabBarEnd()
	{
		ImGui::EndTabBar();
	}

	bool kGuiManager::tabItemStart(kString label, bool *open, ImGuiTabItemFlags flags)
	{
		return ImGui::BeginTabItem(label.c_str(), open, flags);
	}

	void kGuiManager::tabItemEnd()
	{
		ImGui::EndTabItem();
	}

	void kGuiManager::setTabItemClosed(kString tabOrDockedWindowLabel)
	{
		ImGui::SetTabItemClosed(tabOrDockedWindowLabel.c_str());
	}

	// ---- Tables ----

	bool kGuiManager::tableStart(kString id, int columns, ImGuiTableFlags flags, kVec2 outerSize, float innerWidth)
	{
		return ImGui::BeginTable(id.c_str(), columns, flags, ImVec2(outerSize.x, outerSize.y), innerWidth);
	}

	void kGuiManager::tableEnd()
	{
		ImGui::EndTable();
	}

	bool kGuiManager::tableNextColumn()
	{
		return ImGui::TableNextColumn();
	}

	bool kGuiManager::tableSetColumnIndex(int columnN)
	{
		return ImGui::TableSetColumnIndex(columnN);
	}

	void kGuiManager::tableNextRow(ImGuiTableRowFlags rowFlags, float minRowHeight)
	{
		ImGui::TableNextRow(rowFlags, minRowHeight);
	}

	void kGuiManager::tableSetupColumn(kString label, ImGuiTableColumnFlags flags, float initWidthOrWeight)
	{
		ImGui::TableSetupColumn(label.c_str(), flags, initWidthOrWeight);
	}

	void kGuiManager::tableSetupScrollFreeze(int cols, int rows)
	{
		ImGui::TableSetupScrollFreeze(cols, rows);
	}

	void kGuiManager::tableHeadersRow()
	{
		ImGui::TableHeadersRow();
	}

	void kGuiManager::tableHeader(kString label)
	{
		ImGui::TableHeader(label.c_str());
	}

	// ---- Scroll ----

	float kGuiManager::getScrollX()
	{
		return ImGui::GetScrollX();
	}

	float kGuiManager::getScrollY()
	{
		return ImGui::GetScrollY();
	}

	void kGuiManager::setScrollX(float scrollX)
	{
		ImGui::SetScrollX(scrollX);
	}

	void kGuiManager::setScrollY(float scrollY)
	{
		ImGui::SetScrollY(scrollY);
	}

	float kGuiManager::getScrollMaxX()
	{
		return ImGui::GetScrollMaxX();
	}

	float kGuiManager::getScrollMaxY()
	{
		return ImGui::GetScrollMaxY();
	}

	void kGuiManager::setScrollHereX(float centerXRatio)
	{
		ImGui::SetScrollHereX(centerXRatio);
	}

	void kGuiManager::setScrollHereY(float centerYRatio)
	{
		ImGui::SetScrollHereY(centerYRatio);
	}

	// ---- Item Queries ----

	bool kGuiManager::isItemHovered(ImGuiHoveredFlags flags)
	{
		return ImGui::IsItemHovered(flags);
	}

	bool kGuiManager::isItemActive()
	{
		return ImGui::IsItemActive();
	}

	bool kGuiManager::isItemFocused()
	{
		return ImGui::IsItemFocused();
	}

	bool kGuiManager::isItemClicked(ImGuiMouseButton mouseButton)
	{
		return ImGui::IsItemClicked(mouseButton);
	}

	bool kGuiManager::isItemVisible()
	{
		return ImGui::IsItemVisible();
	}

	bool kGuiManager::isItemEdited()
	{
		return ImGui::IsItemEdited();
	}

	bool kGuiManager::isItemActivated()
	{
		return ImGui::IsItemActivated();
	}

	bool kGuiManager::isItemDeactivated()
	{
		return ImGui::IsItemDeactivated();
	}

	bool kGuiManager::isItemDeactivatedAfterEdit()
	{
		return ImGui::IsItemDeactivatedAfterEdit();
	}

	bool kGuiManager::isItemToggledOpen()
	{
		return ImGui::IsItemToggledOpen();
	}

	bool kGuiManager::isAnyItemHovered()
	{
		return ImGui::IsAnyItemHovered();
	}

	bool kGuiManager::isAnyItemActive()
	{
		return ImGui::IsAnyItemActive();
	}

	bool kGuiManager::isAnyItemFocused()
	{
		return ImGui::IsAnyItemFocused();
	}

	kVec2 kGuiManager::getItemRectMin()
	{
		ImVec2 v = ImGui::GetItemRectMin();
		return kVec2(v.x, v.y);
	}

	kVec2 kGuiManager::getItemRectMax()
	{
		ImVec2 v = ImGui::GetItemRectMax();
		return kVec2(v.x, v.y);
	}

	kVec2 kGuiManager::getItemRectSize()
	{
		ImVec2 v = ImGui::GetItemRectSize();
		return kVec2(v.x, v.y);
	}

	// ---- Mouse ----

	bool kGuiManager::isMouseDown(ImGuiMouseButton button)
	{
		return ImGui::IsMouseDown(button);
	}

	bool kGuiManager::isMouseClicked(ImGuiMouseButton button, bool repeat)
	{
		return ImGui::IsMouseClicked(button, repeat);
	}

	bool kGuiManager::isMouseReleased(ImGuiMouseButton button)
	{
		return ImGui::IsMouseReleased(button);
	}

	bool kGuiManager::isMouseDoubleClicked(ImGuiMouseButton button)
	{
		return ImGui::IsMouseDoubleClicked(button);
	}

	bool kGuiManager::isMouseHoveringRect(kVec2 rMin, kVec2 rMax, bool clip)
	{
		return ImGui::IsMouseHoveringRect(ImVec2(rMin.x, rMin.y), ImVec2(rMax.x, rMax.y), clip);
	}

	kVec2 kGuiManager::getMousePos()
	{
		ImVec2 p = ImGui::GetMousePos();
		return kVec2(p.x, p.y);
	}

	kVec2 kGuiManager::getMouseDelta()
	{
		ImVec2 d = ImGui::GetIO().MouseDelta;
		return kVec2(d.x, d.y);
	}

	float kGuiManager::getMouseWheel()
	{
		return ImGui::GetIO().MouseWheel;
	}

	// ---- Child Window ----

	kVec2 kGuiManager::getWindowContentRegionMin()
	{
		ImVec2 v = ImGui::GetWindowContentRegionMin();
		return kVec2(v.x, v.y);
	}

	kVec2 kGuiManager::getWindowContentRegionMax()
	{
		ImVec2 v = ImGui::GetWindowContentRegionMax();
		return kVec2(v.x, v.y);
	}

	kVec2 kGuiManager::getMainViewportCenter()
	{
		ImVec2 v = ImGui::GetMainViewport()->GetCenter();
		return kVec2(v.x, v.y);
	}

	bool kGuiManager::childStart(kString id, kVec2 size, ImGuiChildFlags childFlags, ImGuiWindowFlags windowFlags)
	{
		return ImGui::BeginChild(id.c_str(), ImVec2(size.x, size.y), childFlags, windowFlags);
	}

	void kGuiManager::childEnd()
	{
		ImGui::EndChild();
	}

	// ---- Layout (disabled / frame metrics / screen cursor) ----

	void kGuiManager::beginDisabled(bool disabled)
	{
		ImGui::BeginDisabled(disabled);
	}

	void kGuiManager::endDisabled()
	{
		ImGui::EndDisabled();
	}

	void kGuiManager::setCursorScreenPos(kVec2 pos)
	{
		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y));
	}

	float kGuiManager::getFrameHeight()
	{
		return ImGui::GetFrameHeight();
	}

	float kGuiManager::getFrameHeightWithSpacing()
	{
		return ImGui::GetFrameHeightWithSpacing();
	}

	// ---- Style ----

	float kGuiManager::getFontSize()
	{
		return ImGui::GetFontSize();
	}

	// ---- Text ----

	kVec2 kGuiManager::calcTextSize(kString text, bool hideTextAfterDoubleHash, float wrapWidth)
	{
		ImVec2 v = ImGui::CalcTextSize(text.c_str(), nullptr, hideTextAfterDoubleHash, wrapWidth);
		return kVec2(v.x, v.y);
	}

	// ---- Columns ----

	void kGuiManager::columnsStart(int count, kString id, bool borders)
	{
		ImGui::Columns(count, id.empty() ? nullptr : id.c_str(), borders);
	}

	void kGuiManager::columnsEnd()
	{
		ImGui::Columns(1);
	}

	void kGuiManager::nextColumn()
	{
		ImGui::NextColumn();
	}

	float kGuiManager::getColumnWidth(int columnIndex)
	{
		return ImGui::GetColumnWidth(columnIndex);
	}

	// ---- Item Queries ----

	void kGuiManager::setNextItemAllowOverlap()
	{
		ImGui::SetNextItemAllowOverlap();
	}

	// ---- Keyboard ----

	bool kGuiManager::isKeyShift()
	{
		return ImGui::GetIO().KeyShift;
	}

	bool kGuiManager::isKeyCtrl()
	{
		return ImGui::GetIO().KeyCtrl;
	}

	bool kGuiManager::getWantTextInput()
	{
		return ImGui::GetIO().WantTextInput;
	}

	float kGuiManager::getDeltaTime()
	{
		return ImGui::GetIO().DeltaTime;
	}

	// ---- Draw ----

	void kGuiManager::drawListAddImage(GLuint textureId, kVec2 pMin, kVec2 pMax, kVec2 uvMin, kVec2 uvMax, kVec4 tint)
	{
		ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(tint.x, tint.y, tint.z, tint.w));
		ImGui::GetWindowDrawList()->AddImage(
			(ImTextureID)(intptr_t)textureId,
			ImVec2(pMin.x, pMin.y), ImVec2(pMax.x, pMax.y),
			ImVec2(uvMin.x, uvMin.y), ImVec2(uvMax.x, uvMax.y),
			col);
	}

	float kGuiManager::getTextLineHeight()
	{
		return ImGui::GetTextLineHeight();
	}

	void kGuiManager::textUnformatted(kString text)
	{
		ImGui::TextUnformatted(text.c_str());
	}

	// ---- Utility ----

	void kGuiManager::setClipboardText(kString text)
	{
		ImGui::SetClipboardText(text.c_str());
	}

	void kGuiManager::saveIniSettingsToDisk(kString filename)
	{
		ImGui::SaveIniSettingsToDisk(filename.c_str());
	}

	void kGuiManager::loadIniSettingsFromDisk(kString filename)
	{
		ImGui::LoadIniSettingsFromDisk(filename.c_str());
	}

	void kGuiManager::loadIniSettingsFromMemory(const char *data, size_t size)
	{
		ImGui::LoadIniSettingsFromMemory(data, size);
	}

	void kGuiManager::addSettingsHandler(ImGuiSettingsHandler handler)
	{
		ImGui::AddSettingsHandler(&handler);
	}

	void kGuiManager::destroy()
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
	}
}
