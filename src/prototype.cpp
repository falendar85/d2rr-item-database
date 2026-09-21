#include <itemdb/prototype.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace itemdb {
namespace {
std::string number(double value) {
    std::ostringstream out;
    if (std::floor(value) == value) out << static_cast<long long>(value);
    else out << std::fixed << std::setprecision(1) << value;
    return out.str();
}

std::string titleCase(std::string value) {
    bool capitalize = true;
    for (char& character : value) {
        if (capitalize && character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
        capitalize = character == ' ' || character == '-' || character == '/';
    }
    return value;
}

std::string joined(const Record& record, const char* key) {
    auto found = record.fields.find(key);
    if (found == record.fields.end() || found->second.empty()) return {};
    std::string result;
    for (const auto& value : found->second) {
        if (!result.empty()) result += ", ";
        result += titleCase(value);
    }
    return result;
}

std::string originalSetName(const Record& record) {
    for (const auto& line : record.lines) if (line.starts_with("Set: ")) return line.substr(5);
    return record.name;
}

std::string withoutTier(std::string name) {
    if (name.size() >= 4 && name[name.size() - 4] == ' ' && name[name.size() - 3] == '[' && name.back() == ']') name.resize(name.size() - 4);
    return name;
}

int tierOrder(const Record& record) {
    const auto tier = joined(record, "tier");
    if (tier == "Normal") return 0;
    if (tier == "Exceptional") return 1;
    if (tier == "Elite") return 2;
    return 3;
}

void addField(std::vector<std::string>& lines, const Record& record, const char* key, const char* label) {
    auto value = joined(record, key);
    if (!value.empty() && value != "unknown" && value != "other") lines.emplace_back(std::string(label) + ": " + value);
}

void addNumber(std::vector<std::string>& lines, const Record& record, const char* key, const char* label) {
    auto found = record.numbers.find(key);
    if (found != record.numbers.end()) lines.emplace_back(std::string(label) + ": " + number(found->second));
}

Json rect(int x, int y, int width, int height) {
    return {{"x", x}, {"y", y}, {"width", width}, {"height", height}};
}

Json textWidget(std::string name, std::string text, Json bounds, Json style = "$StyleModalDialogDescription") {
    return {{"type", "TextBoxWidget"}, {"name", std::move(name)}, {"fields", {
        {"rect", std::move(bounds)}, {"text", std::move(text)}, {"style", std::move(style)}
    }}};
}

Json detailTextStyle() {
    return {
        {"fontColor", "$ModalDescriptionTextColor"},
        {"pointSize", "$SmallFontSize"},
        {"alignment", {{"h", "center"}, {"v", "top"}}},
        {"options", {{"lineWrap", true}, {"newlineHandling", "standard"}}},
        {"spacing", "$ReducedSpacing"},
    };
}

Json compactDetailTextStyle() {
    auto style = detailTextStyle();
    style["pointSize"] = "$SmallFontSize";
    return style;
}

Json buttonWidget(std::string name, std::string text, Json bounds, std::string message) {
    return {{"type", "ButtonWidget"}, {"name", std::move(name)}, {"fields", {
        {"rect", std::move(bounds)}, {"filename", "Panel\\Modals\\ModalButton"},
        {"focusIndicatorFilename", "Controller/HoverImages/ModalButton_Hover"},
        {"pressedFrame", 1}, {"disabledFrame", 2}, {"hoveredFrame", 3},
        {"textString", std::move(text)}, {"pointSize", "$MediumFontSize"},
        {"text/style", "$StyleFEMultiLineButtonText"},
        {"onClickMessage", std::move(message)}, {"textColor", "$FontColorWhite"},
        {"acceptsReturnKey", true}, {"focusOnMouseOver", true}, {"sound", "select"}
    }}};
}

Json closeButtonWidget(Json bounds, std::string message) {
    return {{"type", "ButtonWidget"}, {"name", "CloseButton"}, {"fields", {
        {"rect", std::move(bounds)}, {"filename", "PANEL\\closebtn_4x"}, {"hoveredFrame", 3},
        {"tooltipString", "@d2r:strClose"}, {"sound", "cursor_close_window_hd"}, {"onClickMessage", std::move(message)}
    }}};
}

Json centeredTitleStyle() {
    return {
        {"fontColor", "$FontColorLightGold"},
        {"pointSize", "$LargeFontSize"},
        {"alignment", {{"h", "center"}, {"v", "center"}}},
    };
}

std::string detailText(const PrototypeDetail& detail) {
    std::string text;
    for (const auto& line : detail.lines) {
        if (!text.empty()) text += "\n";
        text += line;
    }
    return text;
}


std::vector<std::string> baseLines(const Record& record) {
    std::vector<std::string> lines;
    for (size_t i = 1; i < record.lines.size(); ++i) {
        const auto& line = record.lines[i];
        if (line.starts_with("Automagic ") || line.starts_with("Origin:")) continue;
        if (line == record.name) continue;
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> setLines(const PrototypeViewModel& model, size_t row) {
    std::vector<std::string> lines;
    std::vector<std::string> setBonuses;
    std::set<std::string> seenBonuses;
    for (size_t member = 0; member < model.groupSize(1, row); ++member) {
        const auto* record = model.groupRecordAt(1, row, member);
        for (const auto& property : record->properties) {
            if ((property.text.starts_with("Partial set bonus:") || property.text.starts_with("Full set bonus:")) &&
                seenBonuses.insert(property.text).second) setBonuses.push_back(property.text);
        }
    }
    lines.emplace_back("SET BONUSES");
    lines.insert(lines.end(), setBonuses.begin(), setBonuses.end());
    for (size_t member = 0; member < model.groupSize(1, row); ++member) {
        const auto* record = model.groupRecordAt(1, row, member);
        lines.emplace_back("");
        lines.emplace_back(record->name);
        addField(lines, *record, "base", "Base");
        addField(lines, *record, "type", "Item Type");
        addNumber(lines, *record, "required_level", "Required Level");
        auto strength = record->numbers.find("strength");
        if (strength != record->numbers.end() && strength->second > 0) lines.emplace_back("Required Strength: " + number(strength->second));
        auto dexterity = record->numbers.find("dexterity");
        if (dexterity != record->numbers.end() && dexterity->second > 0) lines.emplace_back("Required Dexterity: " + number(dexterity->second));
        auto minimum = record->numbers.find("min_damage"), maximum = record->numbers.find("max_damage");
        if (minimum != record->numbers.end() && maximum != record->numbers.end()) lines.emplace_back("Damage: " + number(minimum->second) + "-" + number(maximum->second));
        addNumber(lines, *record, "defense", "Defense");
        for (const auto& property : record->properties) {
            if (!property.text.empty() && !property.text.starts_with("Partial set bonus:") && !property.text.starts_with("Full set bonus:")) lines.push_back(property.text);
        }
    }
    return lines;
}

Json scrollDetail(std::string suffix, std::string title, const std::vector<std::string>& lines) {
    const std::string viewName = "SetScrollView" + suffix;
    const int contentHeight = std::max(840, static_cast<int>(lines.size()) * 34);
    Json content = Json::array();
    content.push_back(textWidget("SetDetailText" + suffix, [&] {
        std::string text;
        for (const auto& line : lines) { if (!text.empty()) text += "\n"; text += line; }
        return text;
    }(), rect(0, 0, 1540, contentHeight), compactDetailTextStyle()));
    Json children = Json::array();
    children.push_back(textWidget("SetDetailTitle" + suffix, std::move(title), rect(0, 0, 1620, 60), centeredTitleStyle()));
    children.push_back({{"type", "RectangleWidget"}, {"name", "SetScrollTrack" + suffix}, {"fields", {
        {"rect", rect(1570, 70, 12, 820)}, {"color", Json::array({0.16, 0.13, 0.07, 1.0})}}}});
    children.push_back({{"type", "ScrollControllerWidget"}, {"name", "SetScrollController" + suffix}, {"fields", {
        {"rect", rect(1558, 70, 36, 820)}, {"upArrowFilepath", "FrontEnd\\HD\\Final\\FrontEnd_ScrollUpBtn"},
        {"downArrowFilepath", "FrontEnd\\HD\\Final\\FrontEnd_ScrollDownBtn"}, {"barFilepath", "PauseMenu\\VerticalIndicator"},
        {"viewName", viewName}, {"buttonScrollAmount", 80}, {"wheelScrollSound", "cursor_scroll_hd"}, {"buttonScrollSound", "cursor_scroll_hd"}}}});
    children.push_back({{"type", "ScrollViewWidget"}, {"name", viewName}, {"fields", {
        {"rect", rect(0, 70, 1540, 820)}, {"scrollControllerName", "SetScrollController" + suffix}}}, {"children", Json::array({
            {{"type", "Widget"}, {"name", "SetScrollContent" + suffix}, {"fields", {{"rect", rect(0, 0, 1540, contentHeight)}}}, {"children", std::move(content)}}
        })}});
    return {{"type", "Widget"}, {"name", "Detail" + suffix}, {"fields", {{"rect", rect(690, 15, 1660, 920)}}}, {"children", std::move(children)}};
}
}

PrototypeViewModel::PrototypeViewModel(const Database& database, size_t visibleLimit)
    : database_(&database), visibleLimit_(std::max<size_t>(1, visibleLimit)) {
    for (size_t tab = 0; tab < Tabs.size(); ++tab) {
        Query query;
        query.tab = Tabs[tab];
        query.sort = "name";
        const auto indices = execute(database, query).indices;
        if (tab == 1 || tab == 3) {
            std::map<std::string, std::pair<std::string, std::vector<size_t>>> grouped;
            for (const size_t index : indices) {
                const auto& record = database.records[index];
                std::string key;
                std::string label;
                if (tab == 1) {
                    auto found = record.fields.find("set");
                    key = found == record.fields.end() || found->second.empty() ? lower(record.name) : found->second.front();
                    label = originalSetName(record);
                } else {
                    auto found = record.fields.find("base_family_code");
                    key = found == record.fields.end() || found->second.empty() ? record.id : found->second.front();
                    label = withoutTier(record.name);
                }
                auto& group = grouped[key];
                if (group.first.empty() || (tab == 3 && tierOrder(record) == 0)) group.first = label;
                group.second.push_back(index);
            }
            std::vector<std::pair<std::string, std::vector<size_t>>> ordered;
            for (auto& [key, value] : grouped) ordered.push_back(std::move(value));
            std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) { return lower(left.first) < lower(right.first); });
            for (auto& [label, members] : ordered) {
                if (tab == 3) std::sort(members.begin(), members.end(), [&](size_t left, size_t right) { return tierOrder(database.records[left]) < tierOrder(database.records[right]); });
                labels_[tab].push_back(std::move(label));
                groups_[tab].push_back(std::move(members));
            }
        } else {
            for (const size_t index : indices) {
                labels_[tab].push_back(database.records[index].name);
                groups_[tab].push_back({index});
            }
        }
        if (!groups_[tab].empty()) selectedRows_[tab] = 0;
    }
}

size_t PrototypeViewModel::count(size_t tab) const {
    if (tab >= Tabs.size()) throw std::out_of_range("Prototype tab is outside the available tabs");
    return groups_[tab].size();
}

size_t PrototypeViewModel::visibleCount(size_t tab) const {
    return std::min(visibleLimit_, count(tab));
}

bool PrototypeViewModel::switchTab(size_t tab) noexcept {
    if (tab >= Tabs.size()) return false;
    activeTab_ = tab;
    return true;
}

bool PrototypeViewModel::select(size_t visibleRow) noexcept {
    if (visibleRow >= visibleCount(activeTab_)) return false;
    selectedRows_[activeTab_] = visibleRow;
    return true;
}

std::optional<size_t> PrototypeViewModel::selectedRow(size_t tab) const {
    if (tab >= Tabs.size()) return std::nullopt;
    return selectedRows_[tab];
}

const Record* PrototypeViewModel::recordAt(size_t tab, size_t visibleRow) const noexcept {
    return groupRecordAt(tab, visibleRow, 0);
}

const Record* PrototypeViewModel::groupRecordAt(size_t tab, size_t visibleRow, size_t member) const noexcept {
    if (database_ == nullptr || tab >= Tabs.size() || visibleRow >= std::min(visibleLimit_, groups_[tab].size()) || member >= groups_[tab][visibleRow].size()) return nullptr;
    return &database_->records[groups_[tab][visibleRow][member]];
}

size_t PrototypeViewModel::groupSize(size_t tab, size_t visibleRow) const noexcept {
    if (tab >= Tabs.size() || visibleRow >= std::min(visibleLimit_, groups_[tab].size())) return 0;
    return groups_[tab][visibleRow].size();
}

const std::string& PrototypeViewModel::labelAt(size_t tab, size_t visibleRow) const {
    if (tab >= Tabs.size() || visibleRow >= std::min(visibleLimit_, labels_[tab].size())) throw std::out_of_range("Result label is outside the prototype page");
    return labels_[tab][visibleRow];
}

const Record* PrototypeViewModel::selectedRecord() const noexcept {
    const auto row = selectedRows_[activeTab_];
    return row ? recordAt(activeTab_, *row) : nullptr;
}

PrototypeDetail PrototypeViewModel::detailFor(size_t tab, size_t visibleRow) const {
    const Record* record = recordAt(tab, visibleRow);
    if (record == nullptr) throw std::out_of_range("Result row is outside the prototype page");
    PrototypeDetail detail;
    detail.title = record->name;
    addField(detail.lines, *record, "base", "Base");
    addField(detail.lines, *record, "set", "Set");
    addField(detail.lines, *record, "runes", "Runes");
    addField(detail.lines, *record, "type", "Item Type");
    addField(detail.lines, *record, "category", "Category");
    addField(detail.lines, *record, "tier", "Tier");
    addNumber(detail.lines, *record, "required_level", "Required Level");
    auto strength = record->numbers.find("strength");
    if (strength != record->numbers.end() && strength->second > 0) detail.lines.emplace_back("Required Strength: " + number(strength->second));
    auto dexterity = record->numbers.find("dexterity");
    if (dexterity != record->numbers.end() && dexterity->second > 0) detail.lines.emplace_back("Required Dexterity: " + number(dexterity->second));
    addField(detail.lines, *record, "class", "Class");
    addField(detail.lines, *record, "weapon_type", "Weapon Type");
    auto minimum = record->numbers.find("min_damage");
    auto maximum = record->numbers.find("max_damage");
    if (minimum != record->numbers.end() && maximum != record->numbers.end()) {
        detail.lines.emplace_back("Damage: " + number(minimum->second) + "-" + number(maximum->second));
    }
    addNumber(detail.lines, *record, "avg_damage", "Average Damage");
    addNumber(detail.lines, *record, "defense", "Defense");
    auto sockets = record->numbers.find("sockets");
    if (sockets != record->numbers.end()) detail.lines.emplace_back("Sockets: " + number(sockets->second));
    else addNumber(detail.lines, *record, "max_sockets", "Maximum Sockets");
    addField(detail.lines, *record, "origin", "Origin");
    if (!record->properties.empty()) {
        detail.lines.emplace_back("Properties:");
        bool addedPropertyText = false;
        for (const auto& property : record->properties) {
            if (!property.text.empty()) {
                detail.lines.push_back(property.text);
                addedPropertyText = true;
            }
        }
        // Older deterministic fixtures predate normalized property text. The
        // already-normalized display lines are still a safe detail fallback.
        if (!addedPropertyText) {
            for (const auto& line : record->lines) if (!line.empty()) detail.lines.push_back(line);
        }
    }
    return detail;
}

std::string buildPrototypeLayout(const PrototypeViewModel& model) {
    Json anchorChildren = Json::array();
    anchorChildren.push_back(textWidget("Title", "D2R REIMAGINED ITEM DATABASE", rect(0, 15, 2688, 70), centeredTitleStyle()));
    anchorChildren.push_back(closeButtonWidget(rect(2588, 15, 80, 80), "PanelManager:ClosePanel:item-database/ItemDatabase"));
    static constexpr std::array<const char*, 4> labels{"Uniques", "Sets", "Runewords", "Bases"};
    for (size_t i = 0; i < labels.size(); ++i) {
        anchorChildren.push_back(buttonWidget("Tab" + std::to_string(i), labels[i], rect(50 + static_cast<int>(i) * 520, 115, 520, 72),
                                              "PanelManager:ClosePanel:item-database/action/tab/" + std::string(Tabs[i])));
    }

    static constexpr std::array<const char*, 4> singularLabels{"unique item", "set", "runeword", "base family"};
    for (size_t tab = 0; tab < Tabs.size(); ++tab) {
        Json children = Json::array();
        const std::string prefix = std::string(Tabs[tab]);
        children.push_back(textWidget("Count" + std::to_string(tab), std::to_string(model.count(tab)) + " " + singularLabels[tab] +
            (model.count(tab) == 1 ? "" : "s") + " loaded - first " + std::to_string(model.visibleCount(tab)) + " shown", rect(45, 20, 650, 45)));
        for (size_t row = 0; row < model.visibleCount(tab); ++row) {
            children.push_back(buttonWidget("Row" + std::to_string(tab) + "_" + std::to_string(row), model.labelAt(tab, row),
                rect(45, 85 + static_cast<int>(row) * 78, 610, 65),
                "PanelManager:ClosePanel:item-database/action/select/" + prefix + "/" + std::to_string(row)));
            const std::string suffix = std::to_string(tab) + "_" + std::to_string(row);
            if (tab == 1) {
                children.push_back(scrollDetail(suffix, model.labelAt(tab, row), setLines(model, row)));
                continue;
            }
            if (tab == 3) {
                Json cards = Json::array();
                for (size_t member = 0; member < model.groupSize(tab, row); ++member) {
                    const auto* base = model.groupRecordAt(tab, row, member);
                    PrototypeDetail compact{base->name, baseLines(*base)};
                    Json cardChildren = Json::array();
                    cardChildren.push_back(textWidget("BaseTitle" + suffix + "_" + std::to_string(member), base->name, rect(0, 0, 500, 55), centeredTitleStyle()));
                    cardChildren.push_back(textWidget("BaseText" + suffix + "_" + std::to_string(member), detailText(compact), rect(0, 60, 500, 810), compactDetailTextStyle()));
                    cards.push_back({{"type", "Widget"}, {"name", "BaseCard" + suffix + "_" + std::to_string(member)},
                        {"fields", {{"rect", rect(static_cast<int>(member) * 580, 0, 500, 880)}}}, {"children", std::move(cardChildren)}});
                }
                children.push_back({{"type", "Widget"}, {"name", "Detail" + suffix}, {"fields", {{"rect", rect(690, 15, 1700, 900)}}}, {"children", std::move(cards)}});
                continue;
            }
            const auto detail = model.detailFor(tab, row);
            Json detailChildren = Json::array();
            detailChildren.push_back(textWidget("DetailTitle" + suffix, detail.title, rect(10, 0, 1100, 60), centeredTitleStyle()));
            detailChildren.push_back(textWidget("DetailText" + suffix, detailText(detail), rect(10, 65, 1100, 700), detailTextStyle()));
            children.push_back({{"type", "Widget"}, {"name", "Detail" + suffix},
                {"fields", {{"rect", rect(720, 40, 1140, 770)}}}, {"children", std::move(detailChildren)}});
        }
        anchorChildren.push_back({{"type", "Widget"}, {"name", "Pane" + std::to_string(tab)},
                                  {"fields", {{"rect", rect(0, 210, 2620, 940)}}}, {"children", std::move(children)}});
    }

    Json root = {
        {"type", "Panel"}, {"name", "item-database/ItemDatabase"},
        {"fields", {{"priority", 9002}, {"fitToParent", true}}},
        {"children", Json::array({
            {{"type", "RectangleWidget"}, {"name", "ScreenDim"}, {"fields", {{"fitToScreen", true}, {"color", Json::array({0.0, 0.0, 0.0, 0.82})}}},
             {"children", Json::array({{{"type", "ClickCatcherWidget"}, {"name", "ClickCatcher"}, {"fields", {{"fitToParent", true}}}}})}},
            {{"type", "RectangleWidget"}, {"name", "PanelBackground"}, {"fields", {{"anchor", {{"x", 0.5}, {"y", 0.5}}}, {"rect", rect(-1120, -500, 2688, 1240)}, {"color", Json::array({0.055, 0.045, 0.03, 0.98})}}},
             {"children", std::move(anchorChildren)}}
        })}
    };
    return root.dump();
}
}
