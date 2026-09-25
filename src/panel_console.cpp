#include "panel_console.h"

PanelConsole::PanelConsole(kGuiManager *setGuiManager, Manager *setManager)
{
    addLog(LogLevel::Info, "Welcome to Kemena3D. Create or open a project to get started.", inputBuf);

    gui = setGuiManager;
    manager = setManager;
    if (manager) manager->panelConsole = this; // let Manager log build/script messages here

    // Route AngelScript compiler messages (errors/warnings/info) to this panel.
    // Errors render red, warnings orange — matching addLog's color mapping.
    kScriptManager::setMessageHandler(
        [m = manager](int severity, const char *section, int row, int col, const char *message)
        {
            if (!m || !m->panelConsole) return;
            LogLevel lvl = (severity == kScriptManager::MSG_WARNING) ? LogLevel::Warning
                         : (severity == kScriptManager::MSG_INFO)    ? LogLevel::Info
                         :                                             LogLevel::Error;
            if (row > 0)
                m->panelConsole->addLog(lvl, "[Script] %s (%d,%d): %s",
                                        section ? section : "", row, col, message ? message : "");
            else
                m->panelConsole->addLog(lvl, "[Script] %s", message ? message : "");
        });

    // Route runtime script print()/log()/printConsole() output to this panel.
    kScriptManager::setScriptPrintHandler(
        [m = manager](const char *message)
        {
            if (m && m->panelConsole)
                m->panelConsole->addLog(LogLevel::Info, "[Script] %s", message ? message : "");
        });
}

void PanelConsole::addLog(LogLevel level, const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
    va_end(args);

    ConsoleItem item;
    item.text = kString(buf);
    item.level = level;

    consoleItems.push_back(item);
    scrollToBottom = true;
}

int PanelConsole::textEditCallback(ImGuiInputTextCallbackData *data)
{
    // For history browsing or autocompletion if needed
    return 0;
}

void PanelConsole::draw(bool &opened)
{
    if (opened)
    {
        if (!manager->projectOpened)
            gui->beginDisabled(true);

        gui->windowStart("Console", &opened);

        // Main log display
        gui->childStart("ScrollingRegion", kVec2(0, -gui->getFrameHeightWithSpacing()), 0, ImGuiWindowFlags_HorizontalScrollbar);
        if (consoleItems.size() > 0)
        {
            for (size_t i = 0; i < consoleItems.size(); ++i)
            {
                auto &item = consoleItems[i];
                kVec4 color;
                switch (item.level)
                {
                case LogLevel::Info:
                    color = kVec4(1.0f, 1.0f, 1.0f, 1.0f);
                    break; // white
                case LogLevel::Warning:
                    color = kVec4(1.0f, 0.6f, 0.1f, 1.0f);
                    break; // orange
                case LogLevel::Error:
                    color = kVec4(1.0f, 0.2f, 0.2f, 1.0f);
                    break; // red
                }

                gui->pushStyleColor(ImGuiCol_Text, color);

                bool selected = selectedIndices.count(i) > 0;

                if (gui->selectable(item.text + "##ConsoleMessage" + std::to_string(i), selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap))
                {
                    bool shift = gui->isKeyShift();
                    bool ctrl = gui->isKeyCtrl();

                    if (shift && lastClickedIndex >= 0)
                    {
                        // Shift: select range from last clicked to current
                        size_t start = std::min(lastClickedIndex, i);
                        size_t end = std::max(lastClickedIndex, i);
                        for (size_t j = start; j <= end; ++j)
                            selectedIndices.insert(j);
                    }
                    else if (ctrl)
                    {
                        // Ctrl: toggle selection
                        if (selected)
                            selectedIndices.erase(i);
                        else
                            selectedIndices.insert(i);
                    }
                    else
                    {
                        // Normal click: select only this
                        selectedIndices.clear();
                        selectedIndices.insert(i);
                    }

                    lastClickedIndex = i;

                    // Copy to clipboard on double click
                    if (gui->isMouseDoubleClicked(0))
                    {
                        gui->setClipboardText(item.text);

                        showCopiedTooltip = true;
                        copiedTooltipTime = 0.0f; // reset timer
                    }
                }

                // Right-click context menu for this line
                if (gui->popupContextItemStart("##ConsoleMessage" + std::to_string(i)))
                {
                    gui->pushStyleColor(ImGuiCol_Text, kVec4(1.0f, 1.0f, 1.0f, 1.0f));

                    if (gui->menuItem("Select This"))
                        selectedIndices.insert(i);

                    if (gui->menuItem("Select All"))
                        for (size_t j = 0; j < consoleItems.size(); ++j)
                            selectedIndices.insert(j);

                    // Deselect only the current line
                    if (gui->menuItem("Deselect This"))
                        selectedIndices.erase(i);

                    if (gui->menuItem("Deselect All"))
                        selectedIndices.clear();

                    if (gui->menuItem("Copy Text"))
                    {
                        kString textToCopy;
                        if (selectedIndices.empty())
                            textToCopy = item.text;
                        else
                            for (size_t idx : selectedIndices)
                                textToCopy += consoleItems[idx].text + "\n";

                        gui->setClipboardText(textToCopy);
                        showCopiedTooltip = true;
                        copiedTooltipTime = 0.0f; // reset timer
                    }

                    gui->separator();

                    if (gui->menuItem("Clear Console"))
                        consoleItems.clear();

                    gui->popStyleColor();
                    gui->popupEnd();
                }

                gui->popStyleColor();
            }
        }
        if (scrollToBottom)
            gui->setScrollHereY(1.0f);
        scrollToBottom = false;
        gui->childEnd();

        // Command input
        gui->separator();
        gui->pushItemWidth(-1);
        if (ImGui::InputText("##ConsoleInput", inputBuf, IM_ARRAYSIZE(inputBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
                             &textEditCallback))
        {
            if (inputBuf[0])
            {
                addLog(LogLevel::Info, "> %s", inputBuf);
                // Handle command here
                if (strcmp(inputBuf, "clear") == 0)
                    consoleItems.clear();
                else if (strcmp(inputBuf, "help") == 0)
                    addLog(LogLevel::Info, "Available commands: help, clear");
                else
                    addLog(LogLevel::Warning, "Unknown command: '%s'", inputBuf);

                inputBuf[0] = '\0';
            }
        }
        gui->popItemWidth();

        gui->windowEnd();

        if (!manager->projectOpened)
            gui->endDisabled();

        // Show tooltip
        if (showCopiedTooltip)
        {
            copiedTooltipTime += gui->getDeltaTime();
            gui->beginTooltip();
            gui->text("Text copied to clipboard");
            gui->endTooltip();

            if (copiedTooltipTime >= copiedTooltipDuration)
            {
                showCopiedTooltip = false;
            }
        }
    }
}
