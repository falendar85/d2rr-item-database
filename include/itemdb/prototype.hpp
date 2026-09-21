#pragma once
#include <itemdb/core.hpp>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace itemdb {
inline constexpr size_t PrototypePageSize = 8;

struct PrototypeDetail {
    std::string title;
    std::vector<std::string> lines;
};

class PrototypeViewModel {
    const Database* database_ = nullptr;
    std::array<std::vector<size_t>, Tabs.size()> results_;
    size_t visibleLimit_ = PrototypePageSize;
    size_t activeTab_ = 0;
    std::array<std::optional<size_t>, Tabs.size()> selectedRows_;
public:
    explicit PrototypeViewModel(const Database&, size_t visibleLimit = PrototypePageSize);
    size_t activeTab() const { return activeTab_; }
    size_t count(size_t tab) const;
    size_t visibleCount(size_t tab) const;
    size_t uniqueCount() const { return count(0); }
    size_t visibleUniqueCount() const { return visibleCount(0); }
    bool switchTab(size_t tab) noexcept;
    bool select(size_t visibleRow) noexcept;
    bool selectUnique(size_t visibleRow) noexcept { return activeTab_ == 0 && select(visibleRow); }
    std::optional<size_t> selectedRow() const { return selectedRows_[activeTab_]; }
    std::optional<size_t> selectedRow(size_t tab) const;
    const Record* recordAt(size_t tab, size_t visibleRow) const noexcept;
    const Record* uniqueAt(size_t visibleRow) const noexcept { return recordAt(0, visibleRow); }
    const Record* selectedRecord() const noexcept;
    const Record* selectedUnique() const noexcept { return activeTab_ == 0 ? selectedRecord() : nullptr; }
    PrototypeDetail detailFor(size_t tab, size_t visibleRow) const;
    PrototypeDetail detailFor(size_t visibleRow) const { return detailFor(activeTab_, visibleRow); }
};

std::string buildPrototypeLayout(const PrototypeViewModel&);
}
