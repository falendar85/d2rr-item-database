#include <itemdb/core.hpp>
#include <algorithm>
#include <stdexcept>
namespace itemdb {
Session::Session(){for(size_t i=0;i<Tabs.size();++i)tabs_[i].query.tab=Tabs[i];}
void Session::switchTab(size_t i){if(i>=tabs_.size())throw std::out_of_range("Tab index");active_=i;}
void Session::reset(){tabs_[active_]=TabState{};tabs_[active_].query.tab=Tabs[active_];}
void Session::setQuery(Query q,std::string clauses){if(q.tab!=Tabs[active_])throw std::runtime_error("Query belongs to another tab");current().query=std::move(q);current().clauses=std::move(clauses);current().page=0;current().selected.reset();}
void Session::reconcile(const Database& db,const Results& results){auto& s=current();size_t pages=(results.indices.size()+PageSize-1)/PageSize;s.page=pages?std::min(s.page,pages-1):0;if(s.selected&&std::none_of(results.indices.begin(),results.indices.end(),[&](size_t i){return db.records[i].id==*s.selected;}))s.selected.reset();if(!s.selected&&!results.indices.empty())s.selected=db.records[results.indices[s.page*PageSize]].id;}
void Session::select(const Database& db,const Results& results,size_t row){size_t i=current().page*PageSize+row;if(row<PageSize&&i<results.indices.size())current().selected=db.records[results.indices[i]].id;}
}
