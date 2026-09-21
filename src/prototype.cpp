#include <itemdb/prototype.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace itemdb {

SetBonusKind classifySetBonus(std::string_view text) {
    const std::string normalized = lower(std::string(text));
    if (normalized.starts_with("item set bonus:")) return SetBonusKind::ItemSpecific;
    if (normalized.find("set bonus:") != std::string::npos) return SetBonusKind::Shared;
    return SetBonusKind::None;
}

std::string setBonusDisplayText(std::string_view text) {
    const auto colon = text.find(':');
    return colon == std::string_view::npos ? std::string(text) : trim(std::string(text.substr(colon + 1)));
}

int setBonusRank(std::string_view text) {
    const std::string normalized = lower(std::string(text));
    if (normalized.find("(full set)") != std::string::npos) return 10000;
    size_t open = 0;
    while ((open = normalized.find('(', open)) != std::string::npos) {
        size_t cursor = open + 1;
        int pieces = 0;
        bool foundDigit = false;
        while (cursor < normalized.size() && std::isdigit(static_cast<unsigned char>(normalized[cursor]))) {
            foundDigit = true;
            pieces = pieces * 10 + (normalized[cursor] - '0');
            ++cursor;
        }
        if (foundDigit && (normalized.compare(cursor, 5, " item") == 0 ||
                           normalized.compare(cursor, 10, " set piece") == 0)) return pieces;
        ++open;
    }
    return 9000;
}

void sortSetBonuses(std::vector<std::string>& bonuses) {
    std::stable_sort(bonuses.begin(), bonuses.end(), [](const std::string& left, const std::string& right) {
        return setBonusRank(left) < setBonusRank(right);
    });
}

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
            if (classifySetBonus(property.text) == SetBonusKind::Shared) {
                const auto display = setBonusDisplayText(property.text);
                if (seenBonuses.insert(display).second) setBonuses.push_back(display);
            }
        }
    }
    sortSetBonuses(setBonuses);
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
        std::vector<std::string> itemBonuses;
        for (const auto& property : record->properties) {
            const auto kind = classifySetBonus(property.text);
            if (!property.text.empty() && kind == SetBonusKind::None) lines.push_back(property.text);
            else if (kind == SetBonusKind::ItemSpecific) itemBonuses.push_back(setBonusDisplayText(property.text));
        }
        sortSetBonuses(itemBonuses);
        lines.insert(lines.end(), itemBonuses.begin(), itemBonuses.end());
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

bool hasFieldValue(const Record&, const char*, const std::string&);
std::optional<double> damageAverage(const Record&, const std::string&);
bool matchesCatalogFilter(const Record&, const CatalogFilters&);

PrototypeViewModel::PrototypeViewModel(const Database& database, size_t visibleLimit)
    : database_(&database), visibleLimit_(std::max<size_t>(1, visibleLimit)) {
    for (size_t tab = 0; tab < CatalogTabs.size(); ++tab) rebuild(tab);
}

void PrototypeViewModel::rebuild(size_t tab) {
    groups_[tab].clear();
    labels_[tab].clear();
    pages_[tab] = 0;
    selectedRows_[tab].reset();
    const auto& filter = filters_[tab];
    Query query;
    query.tab = CatalogTabs[tab];
    query.text = filter.text;
    query.sort = tab < Tabs.size() ? "name" : "order";
    if (!filter.itemType.empty()) query.any["type"] = {filter.itemType};
    if (!filter.equipment.empty()) query.any["base"] = {filter.equipment};
    if (!filter.itemClass.empty()) query.any["class"] = {filter.itemClass};
    if (!filter.category.empty()) query.any["category"] = {filter.category};
    if (!filter.tier.empty()) query.any["tier"] = {filter.tier};
    if (filter.runeCount > 0) query.numeric.push_back({"rune_count", Op::Eq, static_cast<double>(filter.runeCount)});
    if (!filter.runes.empty()) query.runes = filter.runes;
    auto indices = execute(*database_, query).indices;
    std::erase_if(indices, [&](size_t index) { return !matchesCatalogFilter(database_->records[index], filter); });

    const bool descending = filter.damageSort.ends_with("descending");
    if (!filter.damageSort.empty()) {
        std::stable_sort(indices.begin(), indices.end(), [&](size_t left, size_t right) {
            const auto a = damageAverage(database_->records[left], filter.damageSort);
            const auto b = damageAverage(database_->records[right], filter.damageSort);
            if (a.has_value() != b.has_value()) return a.has_value();
            if (a && b && *a != *b) return descending ? *a > *b : *a < *b;
            return lower(database_->records[left].name) < lower(database_->records[right].name);
        });
    }

    if (tab == 1 || tab == 3) {
        std::set<std::string> matchedKeys;
        for (const size_t index : indices) {
            const auto& record = database_->records[index];
            const char* field = tab == 1 ? "set" : "base_family_code";
            const auto found = record.fields.find(field);
            matchedKeys.insert(found == record.fields.end() || found->second.empty() ? record.id : lower(found->second.front()));
        }
        std::map<std::string, std::pair<std::string, std::vector<size_t>>> grouped;
        for (size_t index = 0; index < database_->records.size(); ++index) {
            const auto& record = database_->records[index];
            if (record.tab != CatalogTabs[tab]) continue;
            const char* field = tab == 1 ? "set" : "base_family_code";
            const auto found = record.fields.find(field);
            const std::string key = found == record.fields.end() || found->second.empty() ? record.id : lower(found->second.front());
            if (!matchedKeys.contains(key)) continue;
            const std::string label = tab == 1 ? originalSetName(record) : withoutTier(record.name);
            auto& group = grouped[key];
            if (group.first.empty() || (tab == 3 && tierOrder(record) == 0)) group.first = label;
            group.second.push_back(index);
        }
        std::vector<std::pair<std::string, std::vector<size_t>>> ordered;
        for (auto& [key, value] : grouped) ordered.push_back(std::move(value));
        std::sort(ordered.begin(), ordered.end(), [&](const auto& left, const auto& right) {
            if (!filter.damageSort.empty()) {
                const auto groupDamage = [&](const auto& group) -> std::optional<double> {
                    std::optional<double> best;
                    for (const size_t index : group.second) {
                        const auto value = damageAverage(database_->records[index], filter.damageSort);
                        if (value && (!best || (descending ? *value > *best : *value < *best))) best = value;
                    }
                    return best;
                };
                const auto a = groupDamage(left), b = groupDamage(right);
                if (a.has_value() != b.has_value()) return a.has_value();
                if (a && b && *a != *b) return descending ? *a > *b : *a < *b;
            }
            return lower(left.first) < lower(right.first);
        });
        for (auto& [label, members] : ordered) {
            if (tab == 3) std::sort(members.begin(), members.end(), [&](size_t left, size_t right) {
                return tierOrder(database_->records[left]) < tierOrder(database_->records[right]);
            });
            labels_[tab].push_back(std::move(label));
            groups_[tab].push_back(std::move(members));
        }
    } else {
        for (const size_t index : indices) {
            labels_[tab].push_back(database_->records[index].name);
            groups_[tab].push_back({index});
        }
    }
    if (!groups_[tab].empty()) selectedRows_[tab] = 0;
}

CatalogFilters& PrototypeViewModel::filters(size_t tab) {
    if (tab >= CatalogTabs.size()) throw std::out_of_range("Prototype tab is outside the available tabs");
    return filters_[tab];
}

const CatalogFilters& PrototypeViewModel::filters(size_t tab) const {
    if (tab >= CatalogTabs.size()) throw std::out_of_range("Prototype tab is outside the available tabs");
    return filters_[tab];
}

bool PrototypeViewModel::applyFilters(size_t tab) {
    if (tab >= CatalogTabs.size()) return false;
    rebuild(tab);
    return true;
}

bool PrototypeViewModel::resetFilters(size_t tab) {
    if (tab >= CatalogTabs.size()) return false;
    filters_[tab] = {};
    rebuild(tab);
    return true;
}

std::vector<std::string> PrototypeViewModel::filterOptions(size_t tab, const std::string& field) const {
    if (tab >= CatalogTabs.size()) throw std::out_of_range("Prototype tab is outside the available tabs");
    const char* source = nullptr;
    if (field == "type") source = "type";
    else if (field == "equipment") source = "base";
    else if (field == "class") source = "class";
    else if (field == "rune") source = "runes";
    else throw std::out_of_range("Unknown catalog filter option field");
    std::map<std::string, std::string> ordered;
    for (const auto& record : database_->records) {
        if (record.tab != CatalogTabs[tab]) continue;
        const auto found = record.fields.find(source);
        if (found == record.fields.end()) continue;
        for (const auto& value : found->second) if (!value.empty()) ordered.emplace(lower(value), titleCase(value));
    }
    std::vector<std::string> result;
    result.reserve(ordered.size());
    for (auto& [key, value] : ordered) result.push_back(std::move(value));
    return result;
}

size_t PrototypeViewModel::count(size_t tab) const {
    if (tab >= CatalogTabs.size()) throw std::out_of_range("Prototype tab is outside the available tabs");
    return groups_[tab].size();
}

size_t PrototypeViewModel::visibleCount(size_t tab) const {
    return visibleCount(tab, page(tab));
}

bool hasFieldValue(const Record& record, const char* field, const std::string& value) {
    if (value.empty()) return true;
    const auto found = record.fields.find(field);
    if (found == record.fields.end()) return false;
    const auto wanted = lower(value);
    return std::any_of(found->second.begin(), found->second.end(), [&](const std::string& current) {
        return lower(current) == wanted;
    });
}

std::optional<double> damageAverage(const Record& record, const std::string& mode) {
    std::vector<std::string> prefixes;
    if (mode.starts_with("avg-1h-phys")) prefixes = {"One-Hand Damage:", "Damage:"};
    else if (mode.starts_with("avg-2h-phys")) prefixes = {"Two-Hand Damage:"};
    else if (mode.starts_with("avg-throw-phys")) prefixes = {"Throw Damage:"};
    else if (mode.starts_with("avg-non-phys")) prefixes = {"Elemental Damage:"};
    else return std::nullopt;
    static const std::regex numberPattern(R"((\d+(?:\.\d+)?))");
    for (const auto& prefix : prefixes) {
        for (const auto& line : record.lines) {
            if (!line.starts_with(prefix)) continue;
            double total = 0;
            size_t count = 0;
            for (std::sregex_iterator match(line.begin() + static_cast<std::ptrdiff_t>(prefix.size()), line.end(), numberPattern), end;
                 match != end; ++match) {
                total += std::stod((*match)[1].str());
                ++count;
            }
            if (count != 0) return total / static_cast<double>(count);
        }
    }
    return std::nullopt;
}

bool matchesCatalogFilter(const Record& record, const CatalogFilters& filter) {
    if (filter.hideVanilla && hasFieldValue(record, "origin", "Vanilla")) return false;
    if (!filter.weaponMode.empty() && !damageAverage(record, filter.weaponMode == "1h" ? "avg-1h-phys" : "avg-2h-phys")) return false;
    if (filter.sockets > 0) {
        const auto found = record.numbers.find("max_sockets");
        if (found == record.numbers.end() || static_cast<int>(found->second) != filter.sockets) return false;
    }
    if (filter.exactRunes && !filter.runes.empty()) {
        const auto found = record.fields.find("runes");
        if (found == record.fields.end()) return false;
        std::set<std::string> actual, wanted;
        for (const auto& rune : found->second) actual.insert(lower(rune));
        for (const auto& rune : filter.runes) wanted.insert(lower(rune));
        if (actual != wanted) return false;
    }
    return true;
}

size_t PrototypeViewModel::visibleCount(size_t tab, size_t targetPage) const {
    if (tab >= CatalogTabs.size()) throw std::out_of_range("Prototype tab is outside the available tabs");
    const size_t start = targetPage * visibleLimit_;
    if (start >= groups_[tab].size()) return 0;
    return std::min(visibleLimit_, groups_[tab].size() - start);
}

size_t PrototypeViewModel::pageCount(size_t tab) const {
    if (tab >= CatalogTabs.size()) throw std::out_of_range("Prototype tab is outside the available tabs");
    return groups_[tab].empty() ? 0 : (groups_[tab].size() + visibleLimit_ - 1) / visibleLimit_;
}

size_t PrototypeViewModel::page(size_t tab) const {
    if (tab >= CatalogTabs.size()) throw std::out_of_range("Prototype tab is outside the available tabs");
    return pages_[tab];
}

bool PrototypeViewModel::switchTab(size_t tab) noexcept {
    if (tab >= CatalogTabs.size()) return false;
    activeTab_ = tab;
    return true;
}

bool PrototypeViewModel::switchPage(size_t targetPage) noexcept {
    if (targetPage >= pageCount(activeTab_)) return false;
    pages_[activeTab_] = targetPage;
    selectedRows_[activeTab_] = visibleCount(activeTab_) == 0 ? std::nullopt : std::optional<size_t>{0};
    return true;
}

bool PrototypeViewModel::previousPage() noexcept {
    return pages_[activeTab_] > 0 && switchPage(pages_[activeTab_] - 1);
}

bool PrototypeViewModel::nextPage() noexcept {
    return pages_[activeTab_] + 1 < pageCount(activeTab_) && switchPage(pages_[activeTab_] + 1);
}

bool PrototypeViewModel::select(size_t visibleRow) noexcept {
    if (visibleRow >= visibleCount(activeTab_)) return false;
    selectedRows_[activeTab_] = visibleRow;
    return true;
}

std::optional<size_t> PrototypeViewModel::selectedRow(size_t tab) const {
    if (tab >= CatalogTabs.size()) return std::nullopt;
    return selectedRows_[tab];
}

const Record* PrototypeViewModel::recordAt(size_t tab, size_t visibleRow) const noexcept {
    if (tab >= CatalogTabs.size()) return nullptr;
    return recordAt(tab, pages_[tab], visibleRow);
}

const Record* PrototypeViewModel::recordAt(size_t tab, size_t targetPage, size_t visibleRow) const noexcept {
    return groupRecordAt(tab, targetPage, visibleRow, 0);
}

const Record* PrototypeViewModel::groupRecordAt(size_t tab, size_t visibleRow, size_t member) const noexcept {
    if (tab >= CatalogTabs.size()) return nullptr;
    return groupRecordAt(tab, pages_[tab], visibleRow, member);
}

const Record* PrototypeViewModel::groupRecordAt(size_t tab, size_t targetPage, size_t visibleRow, size_t member) const noexcept {
    const size_t absoluteRow = targetPage * visibleLimit_ + visibleRow;
    if (database_ == nullptr || tab >= CatalogTabs.size() || targetPage >= pageCount(tab) || visibleRow >= visibleCount(tab, targetPage) ||
        absoluteRow >= groups_[tab].size() || member >= groups_[tab][absoluteRow].size()) return nullptr;
    return &database_->records[groups_[tab][absoluteRow][member]];
}

size_t PrototypeViewModel::groupSize(size_t tab, size_t visibleRow) const noexcept {
    if (tab >= CatalogTabs.size()) return 0;
    return groupSize(tab, pages_[tab], visibleRow);
}

size_t PrototypeViewModel::groupSize(size_t tab, size_t targetPage, size_t visibleRow) const noexcept {
    const size_t absoluteRow = targetPage * visibleLimit_ + visibleRow;
    if (tab >= CatalogTabs.size() || targetPage >= pageCount(tab) || visibleRow >= visibleCount(tab, targetPage) || absoluteRow >= groups_[tab].size()) return 0;
    return groups_[tab][absoluteRow].size();
}

const std::string& PrototypeViewModel::labelAt(size_t tab, size_t visibleRow) const {
    return labelAt(tab, page(tab), visibleRow);
}

const std::string& PrototypeViewModel::labelAt(size_t tab, size_t targetPage, size_t visibleRow) const {
    const size_t absoluteRow = targetPage * visibleLimit_ + visibleRow;
    if (tab >= CatalogTabs.size() || targetPage >= pageCount(tab) || visibleRow >= visibleCount(tab, targetPage) || absoluteRow >= labels_[tab].size())
        throw std::out_of_range("Result label is outside the prototype page");
    return labels_[tab][absoluteRow];
}

const Record* PrototypeViewModel::selectedRecord() const noexcept {
    const auto row = selectedRows_[activeTab_];
    return row ? recordAt(activeTab_, *row) : nullptr;
}

PrototypeDetail PrototypeViewModel::detailFor(size_t tab, size_t visibleRow) const {
    return detailFor(tab, page(tab), visibleRow);
}

PrototypeDetail PrototypeViewModel::detailFor(size_t tab, size_t targetPage, size_t visibleRow) const {
    const Record* record = recordAt(tab, targetPage, visibleRow);
    if (record == nullptr) throw std::out_of_range("Result row is outside the prototype page");
    PrototypeDetail detail;
    detail.title = record->name;
    if (tab >= Tabs.size()) {
        detail.lines = record->lines;
        return detail;
    }
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
