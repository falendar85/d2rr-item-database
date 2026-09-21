#include <itemdb/prototype.hpp>
#include <iostream>
#include <stdexcept>

using namespace itemdb;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const Json* findNode(const Json& node, const std::string& name) {
    if (node.is_object() && node.value("name", std::string{}) == name) return &node;
    if (node.is_object() && node.contains("children")) {
        for (const auto& child : node["children"]) if (auto* result = findNode(child, name)) return result;
    }
    return nullptr;
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "database path argument required");
        const auto database = Database::load(argv[1]);
        PrototypeViewModel model(database);
        require(model.uniqueCount() > 1000, "normalized Unique records unavailable");
        require(model.visibleUniqueCount() == PrototypePageSize, "prototype page is not bounded");
        require(model.selectedUnique() != nullptr, "initial Unique selection missing");
        for (size_t row = 0; row < model.visibleUniqueCount(); ++row) {
            require(model.selectUnique(row), "valid Unique selection failed");
            require(model.selectedUnique() == model.uniqueAt(row), "selected Unique mismatch");
            const auto detail = model.detailFor(row);
            require(detail.title == model.uniqueAt(row)->name, "detail title is not derived from the record");
            require(!detail.lines.empty(), "detail lines missing");
        }
        const auto* selected = model.selectedUnique();
        require(!model.selectUnique(model.visibleUniqueCount()), "invalid Unique selection was accepted");
        require(model.selectedUnique() == selected, "invalid selection changed state");
        for (size_t tab = 0; tab < Tabs.size(); ++tab) {
            require(model.switchTab(tab) && model.activeTab() == tab, "tab switch failed");
        }
        require(!model.switchTab(Tabs.size()) && model.activeTab() == Tabs.size() - 1, "invalid tab changed state");
        require(model.switchTab(0), "failed to return to Uniques");
        const auto layoutText = buildPrototypeLayout(model);
        require(layoutText.size() < 1024 * 1024, "prototype layout unexpectedly large");
        const auto layout = Json::parse(layoutText);
        require(layout["type"] == "Panel" && layout["name"] == "item-database/ItemDatabase", "panel root invalid");
        const auto* close = findNode(layout, "CloseButton");
        require(close != nullptr && (*close)["fields"]["onClickMessage"] == "PanelManager:ClosePanel:item-database/ItemDatabase", "close message invalid");
        for (size_t tab = 0; tab < Tabs.size(); ++tab) {
            const auto* button = findNode(layout, "Tab" + std::to_string(tab));
            require(button != nullptr, "tab widget missing");
            require((*button)["fields"]["onClickMessage"] == "PanelManager:ClosePanel:item-database/action/tab/" + std::string(Tabs[tab]), "tab message invalid");
        }
        for (size_t row = 0; row < PrototypePageSize; ++row) {
            const auto* button = findNode(layout, "UniqueRow" + std::to_string(row));
            require(button != nullptr, "Unique row widget missing");
            require((*button)["fields"]["onClickMessage"] == "PanelManager:ClosePanel:item-database/action/select/" + std::to_string(row), "Unique row message invalid");
            require(findNode(layout, "UniqueDetail" + std::to_string(row)) != nullptr, "Unique detail widget missing");
            const auto* detailText = findNode(layout, "DetailText" + std::to_string(row));
            require(detailText != nullptr && (*detailText)["fields"]["style"]["alignment"]["v"] == "top", "detail text is not top aligned");
            require((*detailText)["fields"]["style"]["pointSize"] == "$SmallFontSize", "detail text does not use the bounded font size");
        }
        require(findNode(layout, "UniqueRow8") == nullptr, "prototype rendered too many rows");
        std::cout << "Prototype smoke passed: " << database.records.size() << " records, "
                  << model.uniqueCount() << " Uniques, " << layoutText.size() << " layout bytes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
