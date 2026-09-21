#pragma once
#include <itemdb/core.hpp>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace itemdb {
inline constexpr size_t PrototypePageSize = 8;

struct PrototypeDetail {
    std::string title;
    std::vector<std::string> lines;
};

struct CatalogFilters {
    std::string text;
    std::string itemType;
    std::string equipment;
    std::string itemClass;
    std::string weaponMode;
    std::string damageSort;
    std::string category;
    std::string tier;
    std::vector<std::string> runes;
    int sockets = 0;
    int runeCount = 0;
    bool exactRunes = false;
    bool hideVanilla = false;
};

enum class SetBonusKind { None, Shared, ItemSpecific };

SetBonusKind classifySetBonus(std::string_view text);
std::string setBonusDisplayText(std::string_view text);
int setBonusRank(std::string_view text);
void sortSetBonuses(std::vector<std::string>& bonuses);

class PrototypeViewModel {
    const Database* database_ = nullptr;
    std::array<std::vector<std::vector<size_t>>, CatalogTabs.size()> groups_;
    std::array<std::vector<std::string>, CatalogTabs.size()> labels_;
    size_t visibleLimit_ = PrototypePageSize;
    size_t activeTab_ = 0;
    std::array<size_t, CatalogTabs.size()> pages_{};
    std::array<std::optional<size_t>, CatalogTabs.size()> selectedRows_;
    std::array<CatalogFilters, CatalogTabs.size()> filters_;
    void rebuild(size_t tab);
public:
    explicit PrototypeViewModel(const Database&, size_t visibleLimit = PrototypePageSize);
    size_t activeTab() const { return activeTab_; }
    size_t count(size_t tab) const;
    size_t visibleCount(size_t tab) const;
    size_t visibleCount(size_t tab, size_t page) const;
    size_t pageSize() const { return visibleLimit_; }
    size_t pageCount(size_t tab) const;
    size_t page(size_t tab) const;
    size_t uniqueCount() const { return count(0); }
    size_t visibleUniqueCount() const { return visibleCount(0); }
    bool switchTab(size_t tab) noexcept;
    bool switchPage(size_t page) noexcept;
    bool previousPage() noexcept;
    bool nextPage() noexcept;
    bool select(size_t visibleRow) noexcept;
    bool selectUnique(size_t visibleRow) noexcept { return activeTab_ == 0 && select(visibleRow); }
    std::optional<size_t> selectedRow() const { return selectedRows_[activeTab_]; }
    std::optional<size_t> selectedRow(size_t tab) const;
    const Record* recordAt(size_t tab, size_t visibleRow) const noexcept;
    const Record* recordAt(size_t tab, size_t page, size_t visibleRow) const noexcept;
    const Record* groupRecordAt(size_t tab, size_t visibleRow, size_t member) const noexcept;
    const Record* groupRecordAt(size_t tab, size_t page, size_t visibleRow, size_t member) const noexcept;
    size_t groupSize(size_t tab, size_t visibleRow) const noexcept;
    size_t groupSize(size_t tab, size_t page, size_t visibleRow) const noexcept;
    const std::string& labelAt(size_t tab, size_t visibleRow) const;
    const std::string& labelAt(size_t tab, size_t page, size_t visibleRow) const;
    const Record* uniqueAt(size_t visibleRow) const noexcept { return recordAt(0, visibleRow); }
    const Record* selectedRecord() const noexcept;
    const Record* selectedUnique() const noexcept { return activeTab_ == 0 ? selectedRecord() : nullptr; }
    CatalogFilters& filters(size_t tab);
    const CatalogFilters& filters(size_t tab) const;
    bool applyFilters(size_t tab);
    bool resetFilters(size_t tab);
    std::vector<std::string> filterOptions(size_t tab, const std::string& field) const;
    PrototypeDetail detailFor(size_t tab, size_t visibleRow) const;
    PrototypeDetail detailFor(size_t tab, size_t page, size_t visibleRow) const;
    PrototypeDetail detailFor(size_t visibleRow) const { return detailFor(activeTab_, visibleRow); }
};

std::string buildPrototypeLayout(const PrototypeViewModel&);
}
