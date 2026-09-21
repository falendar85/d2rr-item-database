#pragma once
#include <itemdb/core.hpp>
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
    std::vector<size_t> uniqueResults_;
    size_t visibleLimit_ = PrototypePageSize;
    size_t activeTab_ = 0;
    std::optional<size_t> selectedRow_;
public:
    explicit PrototypeViewModel(const Database&, size_t visibleLimit = PrototypePageSize);
    size_t activeTab() const { return activeTab_; }
    size_t uniqueCount() const { return uniqueResults_.size(); }
    size_t visibleUniqueCount() const;
    bool switchTab(size_t tab) noexcept;
    bool selectUnique(size_t visibleRow) noexcept;
    std::optional<size_t> selectedRow() const { return selectedRow_; }
    const Record* uniqueAt(size_t visibleRow) const noexcept;
    const Record* selectedUnique() const noexcept;
    PrototypeDetail detailFor(size_t visibleRow) const;
};

std::string buildPrototypeLayout(const PrototypeViewModel&);
}
