#include <D2RLPlugin/api.h>
#include <itemdb/overlay.hpp>
#include <itemdb/prototype.hpp>
#include <itemdb/resource_ids.h>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
constexpr D2RL::PluginInfo PluginInfo {
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "item-database",
    .name = "D2RR Item Database",
    .version = "0.5.3",
    .author = "Falendar and contributors",
    .description = "Dynamic D2R Reimagined item database overlay.",
    .flags = D2RL::PluginFlags::Client,
};

const D2RL::PluginContext* pluginContext = nullptr;
const D2RL::InputService* input = nullptr;
D2RL::Input::ActionHandle openAction = D2RL::Input::InvalidHandle;
std::unique_ptr<itemdb::Database> database;
std::unique_ptr<itemdb::PrototypeViewModel> model;
std::unique_ptr<itemdb::OverlayHost> overlay;

itemdb::Database loadEmbeddedDatabase(unsigned resourceId, const char* label) {
    const auto module = reinterpret_cast<HMODULE>(&__ImageBase);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10));
    if (resource == nullptr) throw std::runtime_error(std::string("Missing embedded ") + label);
    const auto byteCount = SizeofResource(module, resource);
    if (byteCount == 0 || byteCount > 64U * 1024U * 1024U)
        throw std::runtime_error(std::string("Invalid embedded ") + label + " size");
    const auto loaded = LoadResource(module, resource);
    const auto* bytes = static_cast<const char*>(LockResource(loaded));
    if (bytes == nullptr) throw std::runtime_error(std::string("Cannot read embedded ") + label);
    return itemdb::Database::parse(itemdb::Json::parse(bytes, bytes + byteCount));
}

D2RL::Input::ActionResult __cdecl onOpenAction(const D2RL::PluginContext* plugin,
                                                const D2RL::Input::ActionEvent* event,
                                                void*) noexcept {
    if (plugin == nullptr || overlay == nullptr ||
        !D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) ||
        event->kind != D2RL::Input::ActionEventKind::Pressed) {
        return D2RL::Input::ActionResult::Ignored;
    }
    overlay->toggle();
    return D2RL::Input::ActionResult::Handled;
}

D2RL::ConsoleCommandResult toggleCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept {
    if (command == nullptr || command->plugin == nullptr || overlay == nullptr)
        return D2RL::ConsoleCommandResult::Failed;
    overlay->toggle();
    return D2RL::ConsoleCommandResult::Handled;
}
}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &PluginInfo; }

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* plugin) noexcept -> bool {
    try {
        if (!D2RL::HasContext(plugin)) return false;
        pluginContext = plugin;
        plugin->LogInfo("D2RR Item Database overlay load started");
        database = std::make_unique<itemdb::Database>(
            loadEmbeddedDatabase(ITEMDB_DATABASE_RESOURCE_ID, "database.json"));
        auto guides = loadEmbeddedDatabase(ITEMDB_GUIDES_RESOURCE_ID, "guides.json");
        database->records.insert(database->records.end(),
            std::make_move_iterator(guides.records.begin()), std::make_move_iterator(guides.records.end()));
        database->provenance["guides"] = std::move(guides.provenance);
        model = std::make_unique<itemdb::PrototypeViewModel>(*database);
        overlay = std::make_unique<itemdb::OverlayHost>(*model, plugin);
        if (!overlay->start()) {
            plugin->LogError("Item Database overlay window could not be created");
            overlay.reset(); model.reset(); database.reset();
            return false;
        }
        const std::string loaded = "Item Database overlay ready: " + std::to_string(database->records.size()) +
            " records, " + std::to_string(model->uniqueCount()) + " Uniques";
        plugin->LogInfo(loaded.c_str());
        if (!plugin->RegisterConsoleCommand("itemdb", toggleCommand, "Open or close the D2RR Item Database overlay.")) {
            plugin->LogError("Item Database console command registration failed");
            overlay->stop(); overlay.reset(); model.reset(); database.reset();
            return false;
        }
        if (plugin->QueryService(&input) == D2RL::ServiceQueryResult::Success &&
            D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
            const D2RL::Input::ActionRegistration action{
                .structSize = D2RL::Input::ActionRegistrationSize,
                .logicalId = "open-item-database",
                .displayName = "Open Item Database",
                .category = "D2RR Item Database",
                .defaultPrimary = {.key = D2RL::Input::Key::S, .modifier = D2RL::Input::Modifier::Alt},
                .defaultSecondary = {.key = D2RL::Input::Key::None},
                .callback = onOpenAction,
            };
            if (input->registerAction(plugin, &action, &openAction) != D2RL::Input::Result::Success)
                plugin->LogWarn("Item Database Alt+S action was not registered; use the itemdb console command");
        } else plugin->LogWarn("Item Database input service unavailable; use the itemdb console command");
        plugin->LogInfo("D2RR Item Database overlay load complete");
        return true;
    } catch (const std::exception& error) {
        if (plugin != nullptr) {
            const std::string message = std::string("Item Database overlay initialization failure: ") + error.what();
            plugin->LogError(message.c_str());
        }
        overlay.reset(); model.reset(); database.reset();
        return false;
    } catch (...) {
        if (plugin != nullptr) plugin->LogError("Item Database overlay initialization failure: unknown exception");
        overlay.reset(); model.reset(); database.reset();
        return false;
    }
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (overlay) overlay->stop();
    overlay.reset();
    model.reset();
    database.reset();
    if (pluginContext != nullptr) pluginContext->LogInfo("D2RR Item Database overlay unloaded");
    pluginContext = nullptr;
}
