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
        for (size_t tab = 0; tab < Tabs.size(); ++tab) {
            require(model.switchTab(tab) && model.activeTab() == tab, "tab switch failed");
            require(model.count(tab) > PrototypePageSize, "normalized tab records unavailable");
            require(model.visibleCount(tab) == PrototypePageSize, "tab page is not bounded");
            require(model.selectedRecord() == model.recordAt(tab, 0), "initial tab selection missing");
            for (size_t row = 0; row < model.visibleCount(tab); ++row) {
                require(model.select(row), "valid item selection failed");
                require(model.selectedRecord() == model.recordAt(tab, row), "selected item mismatch");
                const auto detail = model.detailFor(tab, row);
                require(detail.title == model.recordAt(tab, row)->name, "detail title is not derived from the record");
                require(!detail.lines.empty(), "detail lines missing");
            }
            const auto* selected = model.selectedRecord();
            require(!model.select(model.visibleCount(tab)), "invalid item selection was accepted");
            require(model.selectedRecord() == selected, "invalid selection changed state");
            require(model.pageCount(tab) > 1 && model.page(tab) == 0, "tab paging unavailable");
            require(!model.previousPage(), "first page moved backward");
            const auto* firstRecord = model.recordAt(tab, 0);
            require(model.nextPage() && model.page(tab) == 1, "next page failed");
            require(model.selectedRow(tab) == 0 && model.selectedRecord() != firstRecord, "page change did not select its first item");
            require(model.previousPage() && model.page(tab) == 0, "previous page failed");
            require(model.switchPage(model.pageCount(tab) - 1), "last page switch failed");
            require(model.visibleCount(tab) > 0 && model.visibleCount(tab) <= PrototypePageSize, "last page size invalid");
            require(!model.nextPage(), "last page moved forward");
            size_t visited = 0;
            for (size_t page = 0; page < model.pageCount(tab); ++page) {
                require(model.switchPage(page), "catalog page traversal failed");
                for (size_t row = 0; row < model.visibleCount(tab); ++row) {
                    require(!model.labelAt(tab, row).empty() && model.recordAt(tab, row) != nullptr, "paged result is incomplete");
                    require(model.groupSize(tab, row) > 0, "paged result group is empty");
                    for (size_t member = 0; member < model.groupSize(tab, row); ++member)
                        require(model.groupRecordAt(tab, row, member) != nullptr, "paged group member is unavailable");
                    ++visited;
                }
            }
            require(visited == model.count(tab), "paging did not cover the complete tab catalog");
            require(model.switchPage(0), "failed to restore first page");
        }
        require(model.switchTab(0) && model.nextPage(), "failed to retain a Unique page");
        require(model.switchTab(1) && model.nextPage(), "failed to retain a Set page");
        require(model.switchTab(0) && model.page(0) == 1, "Unique page was not retained across tabs");
        require(model.switchPage(0) && model.switchTab(1) && model.switchPage(0), "failed to reset retained pages");
        const size_t activeBeforeInvalidTab = model.activeTab();
        require(!model.switchTab(Tabs.size()) && model.activeTab() == activeBeforeInvalidTab, "invalid tab changed state");
        PrototypeViewModel completeModel(database, database.records.size());
        size_t hadesRow = completeModel.count(1);
        for (size_t row = 0; row < completeModel.count(1); ++row) if (completeModel.labelAt(1, row) == "Hades' Underworld") hadesRow = row;
        require(hadesRow < completeModel.count(1), "Hades' Underworld set group missing");
        require(completeModel.groupSize(1, hadesRow) == 5, "Hades' Underworld does not contain all five set pieces");
        require(completeModel.groupRecordAt(1, hadesRow, 0) != nullptr, "set member lookup failed");
        size_t targeRow = completeModel.count(3);
        for (size_t row = 0; row < completeModel.count(3); ++row) if (completeModel.labelAt(3, row) == "Targe") targeRow = row;
        require(targeRow < completeModel.count(3), "Targe base family missing");
        require(completeModel.groupSize(3, targeRow) == 3, "Targe family does not contain all three tiers");
        require(completeModel.groupRecordAt(3, targeRow, 0)->name == "Targe [N]" &&
                completeModel.groupRecordAt(3, targeRow, 1)->name == "Akaran Targe [X]" &&
                completeModel.groupRecordAt(3, targeRow, 2)->name == "Sacred Targe [E]", "base family tier order invalid");
        require(model.switchTab(0), "failed to return to Uniques");
        const auto layoutText = buildPrototypeLayout(model);
        require(layoutText.size() < 1024 * 1024, "prototype layout unexpectedly large");
        const auto layout = Json::parse(layoutText);
        require(layout["type"] == "Panel" && layout["name"] == "item-database/ItemDatabase", "panel root invalid");
        const auto* background = findNode(layout, "PanelBackground");
        require(background != nullptr && (*background)["fields"]["rect"]["width"] == 2688 && (*background)["fields"]["rect"]["height"] == 1240 &&
                (*background)["fields"]["color"] == Json::array({0.055, 0.045, 0.03, 0.98}),
                "expanded panel background invalid");
        const auto* close = findNode(layout, "CloseButton");
        require(close != nullptr && (*close)["fields"]["onClickMessage"] == "PanelManager:ClosePanel:item-database/ItemDatabase", "close message invalid");
        require((*close)["fields"]["filename"] == "PANEL\\closebtn_4x", "native X close button missing");
        require((*close)["fields"]["tooltipString"] == "@d2r:strClose", "close tooltip is not game-namespaced");
        require((*close)["fields"]["rect"]["x"] == 2588 && (*close)["fields"]["rect"]["y"] == 15, "close button is not in the upper-right corner");
        const auto* title = findNode(layout, "Title");
        require(title != nullptr && (*title)["fields"]["rect"]["width"] == 2688 && (*title)["fields"]["style"]["alignment"]["h"] == "center",
                "database title is not centered across the panel");
        for (size_t tab = 0; tab < Tabs.size(); ++tab) {
            const auto* button = findNode(layout, "Tab" + std::to_string(tab));
            require(button != nullptr, "tab widget missing");
            require((*button)["fields"]["onClickMessage"] == "PanelManager:ClosePanel:item-database/action/tab/" + std::string(Tabs[tab]), "tab message invalid");
            require((*button)["fields"]["rect"]["x"] == 50 + static_cast<int>(tab) * 520, "tab borders are not evenly separated");
            require((*button)["fields"]["rect"]["width"] == 520 && (*button)["fields"]["text/style"] == "$StyleFEMultiLineButtonText",
                    "tab label is not centered in its full button frame");
        }
        for (size_t tab = 0; tab < Tabs.size(); ++tab) {
            require(findNode(layout, "Pane" + std::to_string(tab)) != nullptr, "tab pane missing");
            const auto* count = findNode(layout, "Count" + std::to_string(tab));
            require(count != nullptr && (*count)["fields"]["rect"]["y"] == 20, "tab count was not lowered");
            for (size_t row = 0; row < PrototypePageSize; ++row) {
                const auto suffix = std::to_string(tab) + "_" + std::to_string(row);
                const auto* button = findNode(layout, "Row" + suffix);
                require(button != nullptr, "tab row widget missing");
                require((*button)["fields"]["onClickMessage"] == "PanelManager:ClosePanel:item-database/action/select/" +
                    std::string(Tabs[tab]) + "/" + std::to_string(row), "tab row message invalid");
                require((*button)["fields"]["rect"]["y"] == 85 + static_cast<int>(row) * 78, "tab row was not lowered");
                require(findNode(layout, "Detail" + suffix) != nullptr, "tab detail widget missing");
                if (tab == 1) {
                    require(findNode(layout, "SetDetailTitle" + suffix) != nullptr, "set title missing");
                    const auto* scroll = findNode(layout, "SetScrollController" + suffix);
                    require(scroll != nullptr && (*scroll)["fields"]["rect"]["height"] == 820, "bounded set scrollbar missing");
                    require(findNode(layout, "SetScrollTrack" + suffix) != nullptr, "bounded set scroll track missing");
                    require(findNode(layout, "SetScrollView" + suffix) != nullptr, "set scroll view missing");
                    const auto* setText = findNode(layout, "SetDetailText" + suffix);
                    require(setText != nullptr && (*setText)["fields"]["text"].get<std::string>().find("SET BONUSES") != std::string::npos,
                            "complete set detail content missing");
                } else if (tab == 3) {
                    require(findNode(layout, "BaseTitle" + suffix + "_0") != nullptr, "base family title missing");
                    const auto* baseText = findNode(layout, "BaseText" + suffix + "_0");
                    require(baseText != nullptr, "base family detail missing");
                    require((*baseText)["fields"]["rect"]["width"] == 500, "base family columns were not widened");
                    require((*baseText)["fields"]["style"]["alignment"]["h"] == "center", "base detail text is not centered");
                    const auto* baseTitle = findNode(layout, "BaseTitle" + suffix + "_0");
                    require(baseTitle != nullptr && (*baseTitle)["fields"]["rect"]["width"] == (*baseText)["fields"]["rect"]["width"] &&
                            (*baseTitle)["fields"]["style"]["alignment"]["h"] == "center", "base title and detail column centers differ");
                    if (const auto* secondCard = findNode(layout, "BaseCard" + suffix + "_1"))
                        require((*secondCard)["fields"]["rect"]["x"] == 580, "base cards do not have the enlarged gutter");
                    require((*baseText)["fields"]["text"].get<std::string>().find("Automagic") == std::string::npos,
                            "base family detail still includes automagic overflow");
                } else {
                    const auto* detailTitle = findNode(layout, "DetailTitle" + suffix);
                    require(detailTitle != nullptr && (*detailTitle)["fields"]["style"]["alignment"]["h"] == "center", "item title is not centered");
                    const auto* detailText = findNode(layout, "DetailText" + suffix);
                    require(detailText != nullptr && (*detailText)["fields"]["style"]["alignment"]["v"] == "top", "detail text is not top aligned");
                    require((*detailText)["fields"]["style"]["pointSize"] == "$SmallFontSize", "detail text does not use the bounded font size");
                }
            }
            require(findNode(layout, "Row" + std::to_string(tab) + "_8") == nullptr, "tab rendered too many rows");
        }
        std::cout << "Prototype smoke passed: " << database.records.size() << " records, "
                  << model.uniqueCount() << " Uniques, " << layoutText.size() << " layout bytes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
