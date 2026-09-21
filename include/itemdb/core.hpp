#pragma once
#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace itemdb {
using Json = nlohmann::json;
inline constexpr std::array<const char*,4> Tabs{"uniques","sets","runewords","bases"};
inline constexpr std::array<const char*,8> CatalogTabs{
    "uniques","sets","runewords","bases","cube-recipes","item-enchants","item-crafting","orbs"};
struct Property {
    std::string id, name, text, scope;
    std::optional<double> min, max;
    bool conditional=false, perLevel=false;
};
struct GuideTable {
    std::string title;
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
};
struct Record {
    std::string id,name,tab,search;
    std::map<std::string,std::vector<std::string>> fields;
    std::map<std::string,double> numbers;
    std::vector<Property> properties;
    std::vector<std::string> lines;
    std::vector<GuideTable> guideTables;
};
struct Database {
    std::vector<Record> records;
    Json provenance;
    static Database parse(const Json&);
    static Database load(const std::filesystem::path&);
};
std::string lower(std::string);
std::string trim(std::string);
enum class Op { Eq,Ge,Le,Gt,Lt };
struct Numeric { std::string field; Op op=Op::Ge; double value=0; };
struct PropertyFilter {
    std::string id;
    std::optional<Numeric> numeric;
    bool guaranteed=false, includeConditional=false, includePerLevel=false;
    std::string scope;
};
struct Query {
    std::string tab="uniques",text,sort="name";
    bool descending=false;
    std::map<std::string,std::vector<std::string>> any;
    std::vector<Numeric> numeric;
    std::vector<PropertyFilter> properties;
    std::vector<std::string> runes;
    std::vector<std::string> sequence;
};
struct Results { std::vector<size_t> indices; double milliseconds=0; };
Results execute(const Database&,const Query&);
// User-facing structured filter language. Unknown fields/invalid values are errors.
Query parseQuery(std::string tab,std::string text,const std::string& clauses,std::string sort="name");
Query queryFromJson(const Json&);
inline constexpr size_t PageSize=14;
struct TabState {
    Query query;
    std::string clauses;
    size_t page=0;
    std::optional<std::string> selected;
};
class Session {
    std::array<TabState,4> tabs_;
    size_t active_=0;
public:
    Session();
    TabState& current() { return tabs_[active_]; }
    const TabState& current() const { return tabs_[active_]; }
    size_t active() const { return active_; }
    void switchTab(size_t);
    void reset();
    void setQuery(Query,std::string clauses);
    void reconcile(const Database&,const Results&);
    void select(const Database&,const Results&,size_t visibleRow);
};
}
