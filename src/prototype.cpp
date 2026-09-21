#include <itemdb/prototype.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
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

Json buttonWidget(std::string name, std::string text, Json bounds, std::string message) {
    return {{"type", "ButtonWidget"}, {"name", std::move(name)}, {"fields", {
        {"rect", std::move(bounds)}, {"filename", "Panel\\Modals\\ModalButton"},
        {"focusIndicatorFilename", "Controller/HoverImages/ModalButton_Hover"},
        {"pressedFrame", 1}, {"disabledFrame", 2}, {"hoveredFrame", 3},
        {"textString", std::move(text)}, {"pointSize", "$MediumFontSize"},
        {"onClickMessage", std::move(message)}, {"textColor", "$FontColorWhite"},
        {"acceptsReturnKey", true}, {"focusOnMouseOver", true}, {"sound", "select"}
    }}};
}

std::string detailText(const PrototypeDetail& detail) {
    std::string text;
    for (const auto& line : detail.lines) {
        if (!text.empty()) text += "\n";
        text += line;
    }
    return text;
}
}

PrototypeViewModel::PrototypeViewModel(const Database& database, size_t visibleLimit)
    : database_(&database), visibleLimit_(std::max<size_t>(1, visibleLimit)) {
    Query query;
    query.tab = "uniques";
    query.sort = "name";
    uniqueResults_ = execute(database, query).indices;
    if (!uniqueResults_.empty()) selectedRow_ = 0;
}

size_t PrototypeViewModel::visibleUniqueCount() const {
    return std::min(visibleLimit_, uniqueResults_.size());
}

bool PrototypeViewModel::switchTab(size_t tab) noexcept {
    if (tab >= Tabs.size()) return false;
    activeTab_ = tab;
    return true;
}

bool PrototypeViewModel::selectUnique(size_t visibleRow) noexcept {
    if (visibleRow >= visibleUniqueCount()) return false;
    selectedRow_ = visibleRow;
    return true;
}

const Record* PrototypeViewModel::uniqueAt(size_t visibleRow) const noexcept {
    if (database_ == nullptr || visibleRow >= visibleUniqueCount()) return nullptr;
    return &database_->records[uniqueResults_[visibleRow]];
}

const Record* PrototypeViewModel::selectedUnique() const noexcept {
    return selectedRow_ ? uniqueAt(*selectedRow_) : nullptr;
}

PrototypeDetail PrototypeViewModel::detailFor(size_t visibleRow) const {
    const Record* record = uniqueAt(visibleRow);
    if (record == nullptr) throw std::out_of_range("Unique result row is outside the prototype page");
    PrototypeDetail detail;
    detail.title = record->name;
    addField(detail.lines, *record, "base", "Base");
    addField(detail.lines, *record, "type", "Item Type");
    addField(detail.lines, *record, "category", "Category");
    addField(detail.lines, *record, "tier", "Tier");
    addNumber(detail.lines, *record, "required_level", "Required Level");
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
    anchorChildren.push_back(textWidget("Title", "D2R REIMAGINED ITEM DATABASE", rect(0, 15, 1700, 70), "$StyleSettingsTitle"));
    anchorChildren.push_back(buttonWidget("CloseButton", "Close", rect(1460, 25, 190, 70), "PanelManager:ClosePanel:item-database/ItemDatabase"));
    static constexpr std::array<const char*, 4> labels{"Uniques", "Sets", "Runewords", "Bases"};
    for (size_t i = 0; i < labels.size(); ++i) {
        anchorChildren.push_back(buttonWidget("Tab" + std::to_string(i), labels[i], rect(80 + static_cast<int>(i) * 390, 115, 360, 72),
                                              "PanelManager:ClosePanel:item-database/action/tab/" + std::string(Tabs[i])));
    }

    Json uniqueChildren = Json::array();
    uniqueChildren.push_back(textWidget("UniqueCount", std::to_string(model.uniqueCount()) + " unique items loaded - first " + std::to_string(model.visibleUniqueCount()) + " shown", rect(45, 5, 650, 45)));
    for (size_t row = 0; row < model.visibleUniqueCount(); ++row) {
        const auto* record = model.uniqueAt(row);
        uniqueChildren.push_back(buttonWidget("UniqueRow" + std::to_string(row), record->name, rect(45, 65 + static_cast<int>(row) * 78, 610, 65),
                                              "PanelManager:ClosePanel:item-database/action/select/" + std::to_string(row)));
        const auto detail = model.detailFor(row);
        Json detailChildren = Json::array();
        detailChildren.push_back(textWidget("DetailTitle" + std::to_string(row), detail.title, rect(10, 0, 860, 60), "$StyleSettingsTitle"));
        detailChildren.push_back(textWidget("DetailText" + std::to_string(row), detailText(detail), rect(10, 65, 860, 610), detailTextStyle()));
        uniqueChildren.push_back({{"type", "Widget"}, {"name", "UniqueDetail" + std::to_string(row)},
                                  {"fields", {{"rect", rect(720, 55, 900, 680)}}}, {"children", std::move(detailChildren)}});
    }
    anchorChildren.push_back({{"type", "Widget"}, {"name", "UniquePane"},
                              {"fields", {{"rect", rect(0, 210, 1700, 690)}}}, {"children", std::move(uniqueChildren)}});
    for (size_t i = 1; i < Tabs.size(); ++i) {
        Json children = Json::array();
        children.push_back(textWidget("PlaceholderText" + std::to_string(i), std::string(labels[i]) + "\n\nNot implemented in this milestone", rect(250, 180, 1200, 220), "$StyleSettingsTitle"));
        anchorChildren.push_back({{"type", "Widget"}, {"name", "PlaceholderPane" + std::to_string(i)},
                                  {"fields", {{"rect", rect(0, 210, 1700, 690)}}}, {"children", std::move(children)}});
    }

    Json root = {
        {"type", "Panel"}, {"name", "item-database/ItemDatabase"},
        {"fields", {{"priority", 9002}, {"fitToParent", true}}},
        {"children", Json::array({
            {{"type", "RectangleWidget"}, {"name", "ScreenDim"}, {"fields", {{"fitToScreen", true}, {"color", Json::array({0.0, 0.0, 0.0, 0.82})}}},
             {"children", Json::array({{{"type", "ClickCatcherWidget"}, {"name", "ClickCatcher"}, {"fields", {{"fitToParent", true}}}}})}},
            {{"type", "RectangleWidget"}, {"name", "PanelBackground"}, {"fields", {{"anchor", {{"x", 0.5}, {"y", 0.5}}}, {"rect", rect(-850, -460, 1700, 920)}, {"color", Json::array({0.055, 0.045, 0.03, 0.98})}}},
             {"children", std::move(anchorChildren)}}
        })}
    };
    return root.dump();
}
}
