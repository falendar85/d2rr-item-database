#include <itemdb/prototype.hpp>
#include <algorithm>
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
        require(layoutText.size() < 64ull * 1024 * 1024, "prototype layout exceeds D2RLoader's resource limit");
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
        size_t chunkCount = 0;
        size_t aggregateChunkBytes = 0;
        for (size_t tab = 0; tab < Tabs.size(); ++tab) {
            for (size_t firstPage = 0; firstPage < model.pageCount(tab); firstPage += PrototypePagesPerPanel) {
                const std::string chunkId = "All-" + std::to_string(tab) + "-" + std::to_string(chunkCount);
                const auto chunkText = buildPrototypeLayout(model, chunkId, tab, firstPage, PrototypePagesPerPanel);
                aggregateChunkBytes += chunkText.size();
                require(Json::parse(chunkText)["name"] == "item-database/" + chunkId, "generated chunk root invalid");
                ++chunkCount;
            }
        }
        require(chunkCount == 32, "panel chunk count exceeds the tested registration plan");
        require(aggregateChunkBytes < 64ull * 1024 * 1024, "aggregate panel chunks exceed the loader resource budget");
        for (size_t tab = 0; tab < Tabs.size(); ++tab) {
            const std::string localId = "Test-" + std::to_string(tab);
            const auto tabLayoutText = buildPrototypeLayout(model, localId, tab, 0, PrototypePagesPerPanel);
            require(tabLayoutText.size() < 2ull * 1024 * 1024, "panel chunk is unexpectedly large");
            const auto tabLayout = Json::parse(tabLayoutText);
            require(tabLayout["name"] == "item-database/" + localId, "chunk panel root invalid");
            const auto* chunkClose = findNode(tabLayout, "CloseButton");
            require(chunkClose != nullptr && (*chunkClose)["fields"]["onClickMessage"] ==
                    "PanelManager:ClosePanel:item-database/" + localId, "chunk close message invalid");
            require(findNode(tabLayout, "Pane" + std::to_string(tab)) != nullptr, "tab pane missing");
            require(findNode(tabLayout, "Pane" + std::to_string((tab + 1) % Tabs.size())) == nullptr, "chunk contains another tab's pane");
            require(findNode(tabLayout, "Page" + std::to_string(tab) + "_0") != nullptr, "first tab page missing");
            const size_t finalChunkPage = std::min(PrototypePagesPerPanel, model.pageCount(tab)) - 1;
            require(findNode(tabLayout, "Page" + std::to_string(tab) + "_" + std::to_string(finalChunkPage)) != nullptr, "last page in first chunk missing");
            require(findNode(tabLayout, "Page" + std::to_string(tab) + "_" + std::to_string(PrototypePagesPerPanel)) == nullptr, "chunk rendered too many pages");
            const size_t lastChunkStart = ((model.pageCount(tab) - 1) / PrototypePagesPerPanel) * PrototypePagesPerPanel;
            const auto lastChunk = Json::parse(buildPrototypeLayout(model, localId + "-last", tab, lastChunkStart, PrototypePagesPerPanel));
            require(findNode(lastChunk, "Page" + std::to_string(tab) + "_" + std::to_string(model.pageCount(tab) - 1)) != nullptr,
                    "final catalog page missing from final chunk");
            const auto* count = findNode(tabLayout, "Count" + std::to_string(tab) + "_0");
            require(count != nullptr && (*count)["fields"]["rect"]["y"] == 20, "tab count was not lowered");
            const auto pageSuffix = std::to_string(tab) + "_0";
            const auto* previous = findNode(tabLayout, "Previous" + pageSuffix);
            const auto* next = findNode(tabLayout, "Next" + pageSuffix);
            const auto* pageNumber = findNode(tabLayout, "PageNumber" + pageSuffix);
            require(previous != nullptr && next != nullptr && pageNumber != nullptr, "page controls missing");
            require((*previous)["fields"]["onClickMessage"] == "PanelManager:ClosePanel:item-database/action/page/" +
                std::string(Tabs[tab]) + "/0/previous", "previous page message invalid");
            require((*next)["fields"]["onClickMessage"] == "PanelManager:ClosePanel:item-database/action/page/" +
                std::string(Tabs[tab]) + "/0/next", "next page message invalid");
            for (size_t row = 0; row < PrototypePageSize; ++row) {
                const auto suffix = std::to_string(tab) + "_0_" + std::to_string(row);
                const auto* button = findNode(tabLayout, "Row" + suffix);
                require(button != nullptr, "tab row widget missing");
                require((*button)["fields"]["onClickMessage"] == "PanelManager:ClosePanel:item-database/action/select/" +
                    std::string(Tabs[tab]) + "/0/" + std::to_string(row), "tab row message invalid");
                require((*button)["fields"]["rect"]["y"] == 85 + static_cast<int>(row) * 78, "tab row was not lowered");
                require(findNode(tabLayout, "Detail" + suffix) != nullptr, "tab detail widget missing");
                if (tab == 1) {
                    require(findNode(tabLayout, "SetDetailTitle" + suffix) != nullptr, "set title missing");
                    const auto* scroll = findNode(tabLayout, "SetScrollController" + suffix);
                    require(scroll != nullptr && (*scroll)["fields"]["rect"]["height"] == 820, "bounded set scrollbar missing");
                    require(findNode(tabLayout, "SetScrollTrack" + suffix) != nullptr, "bounded set scroll track missing");
                    require(findNode(tabLayout, "SetScrollView" + suffix) != nullptr, "set scroll view missing");
                    const auto* setText = findNode(tabLayout, "SetDetailText" + suffix);
                    require(setText != nullptr && (*setText)["fields"]["text"].get<std::string>().find("SET BONUSES") != std::string::npos,
                            "complete set detail content missing");
                } else if (tab == 3) {
                    require(findNode(tabLayout, "BaseTitle" + suffix + "_0") != nullptr, "base family title missing");
                    const auto* baseText = findNode(tabLayout, "BaseText" + suffix + "_0");
                    require(baseText != nullptr, "base family detail missing");
                    require((*baseText)["fields"]["rect"]["width"] == 500, "base family columns were not widened");
                    require((*baseText)["fields"]["style"]["alignment"]["h"] == "center", "base detail text is not centered");
                    const auto* baseTitle = findNode(tabLayout, "BaseTitle" + suffix + "_0");
                    require(baseTitle != nullptr && (*baseTitle)["fields"]["rect"]["width"] == (*baseText)["fields"]["rect"]["width"] &&
                            (*baseTitle)["fields"]["style"]["alignment"]["h"] == "center", "base title and detail column centers differ");
                    if (const auto* secondCard = findNode(tabLayout, "BaseCard" + suffix + "_1"))
                        require((*secondCard)["fields"]["rect"]["x"] == 580, "base cards do not have the enlarged gutter");
                    require((*baseText)["fields"]["text"].get<std::string>().find("Automagic") == std::string::npos,
                            "base family detail still includes automagic overflow");
                } else {
                    const auto* detailTitle = findNode(tabLayout, "DetailTitle" + suffix);
                    require(detailTitle != nullptr && (*detailTitle)["fields"]["style"]["alignment"]["h"] == "center", "item title is not centered");
                    const auto* detailText = findNode(tabLayout, "DetailText" + suffix);
                    require(detailText != nullptr && (*detailText)["fields"]["style"]["alignment"]["v"] == "top", "detail text is not top aligned");
                    require((*detailText)["fields"]["style"]["pointSize"] == "$SmallFontSize", "detail text does not use the bounded font size");
                }
            }
            require(findNode(tabLayout, "Row" + std::to_string(tab) + "_0_8") == nullptr, "tab rendered too many rows per page");
        }
        std::cout << "Prototype smoke passed: " << database.records.size() << " records, "
                  << model.uniqueCount() << " Uniques, " << layoutText.size() << " layout bytes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
