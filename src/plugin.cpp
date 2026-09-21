#include <D2RLPlugin/api.h>
#include <itemdb/prototype.hpp>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

namespace {
constexpr D2RL::PluginInfo PluginInfo {
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "item-database",
    .name = "D2RR Item Database",
    .version = "0.1.0",
    .author = "D2RR Item Database contributors",
    .description = "Offline D2R Reimagined item database UI prototype.",
    .flags = D2RL::PluginFlags::Client,
};

const D2RL::PluginContext* pluginContext = nullptr;
const D2RL::ResourceService* resources = nullptr;
const D2RL::PanelService* panels = nullptr;
const D2RL::WidgetService* widgets = nullptr;
const D2RL::SharedEventService* events = nullptr;
const D2RL::InputService* input = nullptr;
const D2RL::ThreadService* threads = nullptr;
D2RL::SharedEvents::ListenerHandle messageListener = D2RL::SharedEvents::InvalidHandle;
D2RL::Input::ActionHandle openAction = D2RL::Input::InvalidHandle;
std::unique_ptr<itemdb::Database> database;
std::unique_ptr<itemdb::PrototypeViewModel> model;
uint64_t viewGeneration = 0;
size_t viewApplyAttempts = 0;
constexpr size_t MaxViewApplyAttempts = 16;

struct PanelChunk {
    size_t tab = 0;
    size_t firstPage = 0;
    size_t pageCount = 0;
    std::string localId;
    D2RL::Resources::RegistrationHandle resource = D2RL::Resources::InvalidHandle;
    D2RL::Panels::RegistrationHandle panel = D2RL::Panels::InvalidHandle;
};

std::vector<PanelChunk> panelChunks;

PanelChunk* chunkFor(size_t tab, size_t page) noexcept {
    for (auto& chunk : panelChunks) if (chunk.tab == tab && page >= chunk.firstPage && page < chunk.firstPage + chunk.pageCount) return &chunk;
    return nullptr;
}

PanelChunk* currentChunk() noexcept {
    return model == nullptr ? nullptr : chunkFor(model->activeTab(), model->page(model->activeTab()));
}

void logResult(const char* prefix, uint32_t result) noexcept {
    if (pluginContext == nullptr) return;
    char message[160]{};
    std::snprintf(message, sizeof(message), "%s (result=%u)", prefix, result);
    pluginContext->LogError(message);
}

bool setVisible(D2RL::Widgets::WidgetHandle root, const std::string& name, bool visible) {
    D2RL::Widgets::WidgetHandle handle = D2RL::Widgets::InvalidHandle;
    auto result = widgets->findWidget(pluginContext, root, name.c_str(), &handle);
    if (result != D2RL::Widgets::Result::Success) {
        logResult(("Item Database widget not found: " + name).c_str(), static_cast<uint32_t>(result));
        return false;
    }
    result = widgets->setWidgetVisible(pluginContext, handle, visible);
    if (result != D2RL::Widgets::Result::Success) {
        logResult(("Item Database widget visibility failed: " + name).c_str(), static_cast<uint32_t>(result));
        return false;
    }
    return true;
}

bool setEnabled(D2RL::Widgets::WidgetHandle root, const std::string& name, bool enabled) {
    D2RL::Widgets::WidgetHandle handle = D2RL::Widgets::InvalidHandle;
    auto result = widgets->findWidget(pluginContext, root, name.c_str(), &handle);
    if (result != D2RL::Widgets::Result::Success) {
        logResult(("Item Database widget not found: " + name).c_str(), static_cast<uint32_t>(result));
        return false;
    }
    result = widgets->setWidgetEnabled(pluginContext, handle, enabled);
    if (result != D2RL::Widgets::Result::Success) {
        logResult(("Item Database widget enabled state failed: " + name).c_str(), static_cast<uint32_t>(result));
        return false;
    }
    return true;
}

bool applyView() {
    if (pluginContext == nullptr || widgets == nullptr || model == nullptr) return false;
    auto* chunk = currentChunk();
    if (chunk == nullptr) return false;
    D2RL::Widgets::WidgetHandle root = D2RL::Widgets::InvalidHandle;
    const std::string panelName = "item-database/" + chunk->localId;
    auto result = widgets->findPanel(pluginContext, panelName.c_str(), &root);
    if (result != D2RL::Widgets::Result::Success) {
        logResult("Item Database panel widget lookup failed", static_cast<uint32_t>(result));
        return false;
    }
    bool ok = true;
    const size_t tab = model->activeTab();
    const std::string pane = "Pane" + std::to_string(tab);
    ok = setVisible(root, pane, true) && ok;
    ok = setEnabled(root, pane, true) && ok;
    for (size_t page = chunk->firstPage; page < chunk->firstPage + chunk->pageCount; ++page) {
        const bool current = page == model->page(tab);
        const std::string pageName = "Page" + std::to_string(tab) + "_" + std::to_string(page);
        ok = setVisible(root, pageName, current) && ok;
        ok = setEnabled(root, pageName, current) && ok;
    }
    for (size_t row = 0; row < model->visibleCount(tab); ++row) {
        const std::string suffix = std::to_string(tab) + "_" + std::to_string(model->page(tab)) + "_" + std::to_string(row);
        const bool selected = model->selectedRow(tab) == row;
        ok = setEnabled(root, "Row" + suffix, true) && ok;
        ok = setVisible(root, "Detail" + suffix, selected) && ok;
        ok = setEnabled(root, "Detail" + suffix, selected) && ok;
    }
    const std::string pageSuffix = std::to_string(tab) + "_" + std::to_string(model->page(tab));
    ok = setEnabled(root, "Previous" + pageSuffix, model->page(tab) > 0) && ok;
    ok = setEnabled(root, "Next" + pageSuffix, model->page(tab) + 1 < model->pageCount(tab)) && ok;
    return ok;
}

bool showPage(size_t tab, size_t oldPage, size_t newPage) {
    if (pluginContext == nullptr || widgets == nullptr) return false;
    D2RL::Widgets::WidgetHandle root = D2RL::Widgets::InvalidHandle;
    auto* chunk = chunkFor(tab, oldPage);
    if (chunk == nullptr || newPage < chunk->firstPage || newPage >= chunk->firstPage + chunk->pageCount) return false;
    const std::string panelName = "item-database/" + chunk->localId;
    if (widgets->findPanel(pluginContext, panelName.c_str(), &root) != D2RL::Widgets::Result::Success) return false;
    const std::string oldName = "Page" + std::to_string(tab) + "_" + std::to_string(oldPage);
    const std::string newName = "Page" + std::to_string(tab) + "_" + std::to_string(newPage);
    bool ok = setVisible(root, oldName, false);
    ok = setEnabled(root, oldName, false) && ok;
    ok = setVisible(root, newName, true) && ok;
    ok = setEnabled(root, newName, true) && ok;
    return ok;
}

void logSelectedDetail() {
    const auto* selected = model ? model->selectedRecord() : nullptr;
    if (selected == nullptr || pluginContext == nullptr) return;
    const auto detail = model->detailFor(model->activeTab(), *model->selectedRow());
    const std::string message = "Item Database detail updated: " + selected->name + " (" + std::to_string(detail.lines.size()) + " lines)";
    pluginContext->LogInfo(message.c_str());
}

bool viewWidgetsReady() {
    if (pluginContext == nullptr || widgets == nullptr || model == nullptr) return false;
    auto* chunk = currentChunk();
    if (chunk == nullptr) return false;
    D2RL::Widgets::WidgetHandle root = D2RL::Widgets::InvalidHandle;
    const std::string panelName = "item-database/" + chunk->localId;
    if (widgets->findPanel(pluginContext, panelName.c_str(), &root) != D2RL::Widgets::Result::Success) return false;
    D2RL::Widgets::WidgetHandle sentinel = D2RL::Widgets::InvalidHandle;
    const std::string suffix = std::to_string(model->activeTab()) + "_" + std::to_string(model->page(model->activeTab()));
    return widgets->findWidget(pluginContext, root, ("PageNumber" + suffix).c_str(), &sentinel) == D2RL::Widgets::Result::Success;
}

void applyOpenedViewOnUiThread(const D2RL::PluginContext* plugin, void* userData) noexcept;

bool queueOpenedViewApply(const D2RL::PluginContext* plugin, uint64_t generation) {
    if (plugin == nullptr || threads == nullptr) return false;
    return threads->runOnUiThread(plugin, applyOpenedViewOnUiThread,
        reinterpret_cast<void*>(static_cast<uintptr_t>(generation))) == D2RL::Threads::Result::Success;
}

void applyOpenedViewOnUiThread(const D2RL::PluginContext* plugin, void* userData) noexcept {
    try {
        const auto generation = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(userData));
        if (plugin == nullptr || generation == 0 || generation != viewGeneration) return;
        if (!viewWidgetsReady()) {
            if (++viewApplyAttempts < MaxViewApplyAttempts && queueOpenedViewApply(plugin, generation)) return;
            plugin->LogError("Item Database UI did not become ready after opening");
            return;
        }
        if (!applyView()) plugin->LogError("Item Database UI initialization failed while applying initial visibility");
        else logSelectedDetail();
    } catch (const std::exception& error) {
        if (plugin != nullptr) {
            const std::string message = std::string("Item Database deferred UI initialization failure: ") + error.what();
            plugin->LogError(message.c_str());
        }
    } catch (...) {
        if (plugin != nullptr) plugin->LogError("Item Database deferred UI initialization failure: unknown exception");
    }
}

void openPanelOnUiThread(const D2RL::PluginContext* plugin, void*) noexcept {
    try {
        if (plugin == nullptr || panels == nullptr) return;
        auto* chunk = currentChunk();
        if (chunk == nullptr) {
            plugin->LogError("Item Database panel chunk unavailable");
            return;
        }
        const auto result = panels->openPanel(plugin, chunk->panel);
        if (result != D2RL::Panels::Result::Success) {
            logResult("Item Database panel open failed", static_cast<uint32_t>(result));
            return;
        }
        plugin->LogInfo("Item Database panel opened");
        ++viewGeneration;
        if (viewGeneration == 0) ++viewGeneration;
        viewApplyAttempts = 0;
        if (!queueOpenedViewApply(plugin, viewGeneration)) plugin->LogError("Item Database could not queue UI initialization");
    } catch (const std::exception& error) {
        if (plugin != nullptr) {
            const std::string message = std::string("Item Database UI initialization failure while opening: ") + error.what();
            plugin->LogError(message.c_str());
        }
    } catch (...) {
        if (plugin != nullptr) plugin->LogError("Item Database UI initialization failure while opening: unknown exception");
    }
}

void closePanel() {
    if (pluginContext == nullptr || panels == nullptr) return;
    auto* chunk = currentChunk();
    if (chunk == nullptr) return;
    ++viewGeneration;
    const auto result = panels->closePanel(pluginContext, chunk->panel);
    if (result == D2RL::Panels::Result::Success) pluginContext->LogInfo("Item Database panel closed");
    else logResult("Item Database panel close failed", static_cast<uint32_t>(result));
}

void togglePanel() {
    if (pluginContext == nullptr || panels == nullptr) return;
    auto* chunk = currentChunk();
    if (chunk == nullptr) return;
    D2RL::Panels::PanelInfo info{.structSize = D2RL::Panels::PanelInfoSize};
    if (panels->getPanelInfo(pluginContext, chunk->panel, &info) == D2RL::Panels::Result::Success &&
        info.presentationState == D2RL::Panels::PresentationState::Open) {
        closePanel();
        return;
    }
    openPanelOnUiThread(pluginContext, nullptr);
}

void toggleOnUiThread(const D2RL::PluginContext* plugin, void*) noexcept {
    try { togglePanel(); }
    catch (const std::exception& error) {
        if (plugin != nullptr) {
            const std::string message = std::string("Item Database UI toggle failure: ") + error.what();
            plugin->LogError(message.c_str());
        }
    } catch (...) { if (plugin != nullptr) plugin->LogError("Item Database UI toggle failure: unknown exception"); }
}

D2RL::Input::ActionResult __cdecl onOpenAction(const D2RL::PluginContext* plugin, const D2RL::Input::ActionEvent* event, void*) noexcept {
    if (plugin == nullptr || !D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) ||
        event->kind != D2RL::Input::ActionEventKind::Pressed || threads == nullptr) {
        return D2RL::Input::ActionResult::Ignored;
    }
    return threads->runOnUiThread(plugin, toggleOnUiThread, nullptr) == D2RL::Threads::Result::Success
        ? D2RL::Input::ActionResult::Handled : D2RL::Input::ActionResult::Ignored;
}

D2RL::ConsoleCommandResult toggleCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    try {
        togglePanel();
        return D2RL::ConsoleCommandResult::Handled;
    } catch (const std::exception& error) {
        const std::string message = std::string("Item Database UI toggle failure: ") + error.what();
        command->plugin->LogError(message.c_str());
        return D2RL::ConsoleCommandResult::Failed;
    } catch (...) {
        command->plugin->LogError("Item Database UI toggle failure: unknown exception");
        return D2RL::ConsoleCommandResult::Failed;
    }
}

D2RL::SharedEvents::UiMessageAction handleUiMessage(const D2RL::PluginContext* plugin, const D2RL::SharedEvents::UiMessageEvent* event) {
    if (plugin == nullptr || event == nullptr || event->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize ||
        event->target == nullptr || event->command == nullptr || std::strcmp(event->target, "PanelManager") != 0 ||
        std::strcmp(event->command, "ClosePanel") != 0 || event->text == nullptr) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    if (std::strcmp(event->text, "item-database/ItemDatabase") == 0) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    constexpr char ActionPrefix[] = "item-database/action/";
    if (std::strncmp(event->text, ActionPrefix, sizeof(ActionPrefix) - 1) != 0) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    if (model == nullptr) return D2RL::SharedEvents::UiMessageAction::Consume;
    const char* action = event->text + sizeof(ActionPrefix) - 1;
    constexpr char TabPrefix[] = "tab/";
    if (std::strncmp(action, TabPrefix, sizeof(TabPrefix) - 1) == 0) {
        const char* tabName = action + sizeof(TabPrefix) - 1;
        size_t tab = itemdb::Tabs.size();
        for (size_t i = 0; i < itemdb::Tabs.size(); ++i) if (std::strcmp(tabName, itemdb::Tabs[i]) == 0) tab = i;
        auto* oldChunk = currentChunk();
        if (!model->switchTab(tab)) {
            plugin->LogWarn("Item Database ignored an invalid tab message");
            return D2RL::SharedEvents::UiMessageAction::Consume;
        }
        const std::string message = "Item Database tab changed: " + std::string(itemdb::Tabs[tab]);
        plugin->LogInfo(message.c_str());
        if (oldChunk != nullptr) panels->closePanel(plugin, oldChunk->panel);
        openPanelOnUiThread(plugin, nullptr);
        return D2RL::SharedEvents::UiMessageAction::Consume;
    }
    constexpr char SelectPrefix[] = "select/";
    if (std::strncmp(action, SelectPrefix, sizeof(SelectPrefix) - 1) == 0) {
        const char* selection = action + sizeof(SelectPrefix) - 1;
        const char* slash = std::strchr(selection, '/');
        if (slash == nullptr) {
            plugin->LogWarn("Item Database ignored a malformed selection");
            return D2RL::SharedEvents::UiMessageAction::Consume;
        }
        const std::string tabName(selection, slash);
        if (tabName != itemdb::Tabs[model->activeTab()]) {
            plugin->LogWarn("Item Database ignored a selection for an inactive tab");
            return D2RL::SharedEvents::UiMessageAction::Consume;
        }
        const char* pageText = slash + 1;
        const char* pageSlash = std::strchr(pageText, '/');
        if (pageSlash == nullptr) {
            plugin->LogWarn("Item Database ignored a malformed paged selection");
            return D2RL::SharedEvents::UiMessageAction::Consume;
        }
        size_t selectionPage = 0;
        auto pageParsed = std::from_chars(pageText, pageSlash, selectionPage);
        if (pageParsed.ec != std::errc{} || pageParsed.ptr != pageSlash || selectionPage != model->page(model->activeTab())) {
            plugin->LogWarn("Item Database ignored a selection for an inactive page");
            return D2RL::SharedEvents::UiMessageAction::Consume;
        }
        const char* rowText = pageSlash + 1;
        size_t row = 0;
        const char* end = rowText + std::strlen(rowText);
        auto parsed = std::from_chars(rowText, end, row);
        if (parsed.ec != std::errc{} || parsed.ptr != end || !model->select(row)) {
            plugin->LogWarn("Item Database ignored an invalid item selection");
            return D2RL::SharedEvents::UiMessageAction::Consume;
        }
        const std::string message = "Item Database item selected: " + model->selectedRecord()->name;
        plugin->LogInfo(message.c_str());
        if (!applyView()) plugin->LogError("Item Database UI initialization failed while selecting an item");
        else logSelectedDetail();
        return D2RL::SharedEvents::UiMessageAction::Consume;
    }
    constexpr char PagePrefix[] = "page/";
    if (std::strncmp(action, PagePrefix, sizeof(PagePrefix) - 1) == 0) {
        const char* value = action + sizeof(PagePrefix) - 1;
        const char* tabSlash = std::strchr(value, '/');
        const char* pageSlash = tabSlash == nullptr ? nullptr : std::strchr(tabSlash + 1, '/');
        if (tabSlash == nullptr || pageSlash == nullptr) {
            plugin->LogWarn("Item Database ignored a malformed page message");
            return D2RL::SharedEvents::UiMessageAction::Consume;
        }
        const std::string tabName(value, tabSlash);
        if (tabName != itemdb::Tabs[model->activeTab()]) {
            plugin->LogWarn("Item Database ignored paging for an inactive tab");
            return D2RL::SharedEvents::UiMessageAction::Consume;
        }
        size_t sourcePage = 0;
        auto parsed = std::from_chars(tabSlash + 1, pageSlash, sourcePage);
        if (parsed.ec != std::errc{} || parsed.ptr != pageSlash || sourcePage != model->page(model->activeTab())) {
            plugin->LogWarn("Item Database ignored paging from an inactive page");
            return D2RL::SharedEvents::UiMessageAction::Consume;
        }
        const std::string direction(pageSlash + 1);
        auto* oldChunk = currentChunk();
        const bool changed = direction == "previous" ? model->previousPage() : direction == "next" ? model->nextPage() : false;
        if (!changed) {
            plugin->LogWarn("Item Database ignored an invalid page change");
            return D2RL::SharedEvents::UiMessageAction::Consume;
        }
        const size_t targetPage = model->page(model->activeTab());
        const std::string message = "Item Database page changed: " + tabName + " " + std::to_string(targetPage + 1) + "/" +
            std::to_string(model->pageCount(model->activeTab()));
        plugin->LogInfo(message.c_str());
        auto* newChunk = currentChunk();
        if (oldChunk != newChunk) {
            if (oldChunk != nullptr) panels->closePanel(plugin, oldChunk->panel);
            openPanelOnUiThread(plugin, nullptr);
        } else if (!showPage(model->activeTab(), sourcePage, targetPage) || !applyView()) {
            plugin->LogError("Item Database UI failed while changing pages");
        } else logSelectedDetail();
        return D2RL::SharedEvents::UiMessageAction::Consume;
    }
    return D2RL::SharedEvents::UiMessageAction::Continue;
}

D2RL::SharedEvents::UiMessageAction __cdecl onUiMessage(const D2RL::PluginContext* plugin, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept {
    try { return handleUiMessage(plugin, event); }
    catch (const std::exception& error) {
        if (plugin != nullptr) {
            const std::string message = std::string("Item Database UI message failure: ") + error.what();
            plugin->LogError(message.c_str());
        }
    } catch (...) { if (plugin != nullptr) plugin->LogError("Item Database UI message failure: unknown exception"); }
    return D2RL::SharedEvents::UiMessageAction::Consume;
}

template<class Service>
bool requireService(const D2RL::PluginContext* plugin, const Service** output, uint32_t size, const char* name) noexcept {
    if (plugin->QueryService(output) == D2RL::ServiceQueryResult::Success && output != nullptr && *output != nullptr && (*output)->serviceSize >= size) return true;
    const std::string message = std::string("Item Database requires D2RLoader ") + name + " service";
    plugin->LogError(message.c_str());
    return false;
}
}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &PluginInfo; }

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* plugin) noexcept -> bool {
    try {
        if (!D2RL::HasContext(plugin)) return false;
        pluginContext = plugin;
        pluginContext->LogInfo("D2RR Item Database plugin load started");
        if (!requireService(plugin, &resources, D2RL::ResourceServiceRequiredSize, "resource") ||
            !requireService(plugin, &panels, D2RL::PanelServiceRequiredSize, "panel") ||
            !requireService(plugin, &widgets, D2RL::WidgetServiceRequiredSize, "widget") ||
            !requireService(plugin, &events, D2RL::SharedEventServiceRequiredSize, "shared-event") ||
            !requireService(plugin, &threads, D2RL::ThreadServiceRequiredSize, "thread")) return false;

        const wchar_t* directory = D2RL::GetPluginDirectory(plugin);
        if (directory == nullptr) {
            plugin->LogError("Item Database database load failed: plugin directory unavailable");
            return false;
        }
        auto databasePath = std::filesystem::path(directory) / L"item-database" / L"database.json";
        if (!std::filesystem::exists(databasePath)) databasePath = std::filesystem::path(directory) / L"database.json";
        database = std::make_unique<itemdb::Database>(itemdb::Database::load(databasePath));
        model = std::make_unique<itemdb::PrototypeViewModel>(*database);
        const std::string loaded = "Item Database database loaded: " + std::to_string(database->records.size()) +
            " records, " + std::to_string(model->uniqueCount()) + " Uniques";
        plugin->LogInfo(loaded.c_str());

        for (size_t tab = 0; tab < itemdb::Tabs.size(); ++tab) {
            for (size_t firstPage = 0, chunkNumber = 0; firstPage < model->pageCount(tab);
                 firstPage += itemdb::PrototypePagesPerPanel, ++chunkNumber) {
                PanelChunk chunk;
                chunk.tab = tab;
                chunk.firstPage = firstPage;
                chunk.pageCount = std::min(itemdb::PrototypePagesPerPanel, model->pageCount(tab) - firstPage);
                chunk.localId = "ItemDatabase-" + std::to_string(tab) + "-" + std::to_string(chunkNumber);
                const std::string resourcePath = "data/global/ui/layouts/item-database/" + chunk.localId + "hd.json";
                const std::string chunkLayout = itemdb::buildPrototypeLayout(*model, chunk.localId, tab, firstPage, chunk.pageCount);
                const D2RL::Resources::ResourceRegistration resource{
                    .structSize = D2RL::Resources::ResourceRegistrationSize,
                    .path = resourcePath.c_str(),
                    .bytes = chunkLayout.data(),
                    .byteCount = chunkLayout.size(),
                };
                auto result = resources->registerResource(plugin, &resource, &chunk.resource);
                if (result != D2RL::Resources::Result::Success) {
                    logResult("Item Database layout chunk registration failed", static_cast<uint32_t>(result));
                    return false;
                }
                const D2RL::Panels::PanelRegistration registration{
                    .structSize = D2RL::Panels::PanelRegistrationSize,
                    .flags = D2RL::Panels::PanelFlags::CloseOnEscape,
                    .localId = chunk.localId.c_str(),
                };
                auto panelResult = panels->registerPanel(plugin, &registration, &chunk.panel);
                if (panelResult != D2RL::Panels::Result::Success) {
                    logResult("Item Database panel chunk registration failed", static_cast<uint32_t>(panelResult));
                    return false;
                }
                panelChunks.push_back(std::move(chunk));
            }
        }
        const std::string registered = "Item Database panel chunks registered: " + std::to_string(panelChunks.size());
        plugin->LogInfo(registered.c_str());

        const D2RL::SharedEvents::UiMessageListener listener{
            .structSize = D2RL::SharedEvents::UiMessageListenerSize,
            .priority = 100,
            .callback = onUiMessage,
        };
        auto listenerResult = events->registerUiMessageListener(plugin, &listener, &messageListener);
        if (listenerResult != D2RL::SharedEvents::Result::Success) {
            logResult("Item Database UI message listener registration failed", static_cast<uint32_t>(listenerResult));
            return false;
        }
        if (!plugin->RegisterConsoleCommand("itemdb", toggleCommand, "Open or close the D2RR Item Database prototype.")) {
            plugin->LogError("Item Database console command registration failed");
            return false;
        }

        if (plugin->QueryService(&input) == D2RL::ServiceQueryResult::Success &&
            D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
            const D2RL::Input::ActionRegistration action{
                .structSize = D2RL::Input::ActionRegistrationSize,
                .logicalId = "open-item-database",
                .displayName = "Open Item Database",
                .category = "D2RR Item Database",
                .defaultPrimary = {.key = D2RL::Input::Key::F8},
                .defaultSecondary = {.key = D2RL::Input::Key::None},
                .callback = onOpenAction,
            };
            if (input->registerAction(plugin, &action, &openAction) != D2RL::Input::Result::Success) {
                plugin->LogWarn("Item Database F8 action was not registered; use the itemdb console command");
            }
        } else plugin->LogWarn("Item Database input/thread service unavailable; use the itemdb console command");
        plugin->LogInfo("D2RR Item Database plugin load complete");
        return true;
    } catch (const std::exception& error) {
        if (plugin != nullptr) {
            const std::string message = std::string("Item Database UI initialization failure: ") + error.what();
            plugin->LogError(message.c_str());
        }
        return false;
    } catch (...) {
        if (plugin != nullptr) plugin->LogError("Item Database UI initialization failure: unknown exception");
        return false;
    }
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (pluginContext != nullptr) pluginContext->LogInfo("D2RR Item Database plugin unloaded");
    panelChunks.clear();
    model.reset();
    database.reset();
    pluginContext = nullptr;
}
