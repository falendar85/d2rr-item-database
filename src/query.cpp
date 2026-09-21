#include <itemdb/core.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <set>

namespace itemdb {
static const std::set<std::string> Fields{"name","base","base_code","base_family","base_family_code","set","type","category","class","tier","origin","weapon_type","compatible_base"};
static const std::set<std::string> Numbers{"required_level","strength","dexterity","min_damage","max_damage","avg_damage","weapon_speed","defense","max_sockets","sockets","rune_count","sockets_low","sockets_mid","sockets_high"};
static bool compare(double a,Op op,double b) { switch(op){case Op::Eq:return a==b; case Op::Ge:return a>=b;case Op::Le:return a<=b;case Op::Gt:return a>b;case Op::Lt:return a<b;} return false; }
static std::vector<std::string> split(const std::string& s,char delimiter) {std::vector<std::string> out; std::istringstream in(s); std::string t;while(std::getline(in,t,delimiter)) if(!(t=trim(t)).empty())out.push_back(lower(t));return out;}
static bool containsAll(const std::vector<std::string>& have,const std::vector<std::string>& want) {
    for(auto& v:want) if(std::count(have.begin(),have.end(),v)<std::count(want.begin(),want.end(),v))return false; return true;
}
static bool matches(const Record& r,const Query& q,const std::vector<std::string>& terms) {
    if(r.tab!=q.tab)return false;
    for(auto& t:terms)if(r.search.find(t)==std::string::npos)return false;
    for(auto& [key,choices]:q.any) {
        if(choices.empty())continue;
        auto found=r.fields.find(key);if(found==r.fields.end())return false;
        bool ok=false;for(auto& v:choices) if(std::find(found->second.begin(),found->second.end(),v)!=found->second.end()){ok=true;break;} if(!ok)return false;
    }
    for(auto& n:q.numeric) {auto it=r.numbers.find(n.field);if(it==r.numbers.end() || !compare(it->second,n.op,n.value))return false;}
    auto rune=r.fields.find("runes");
    if(!q.runes.empty() && (rune==r.fields.end() || !containsAll(rune->second,q.runes)))return false;
    if(!q.sequence.empty() && (rune==r.fields.end() || rune->second!=q.sequence))return false;
    for(auto& f:q.properties) {
        bool ok=false;
        for(auto& p:r.properties) {
            if(p.id!=f.id || (!f.includeConditional&&p.conditional)||(!f.includePerLevel&&p.perLevel)||(!f.scope.empty()&&p.scope!=f.scope))continue;
            if(!f.numeric){ok=true;break;}
            if(!p.min)continue;
            // Possible: any roll in [min,max] can satisfy the comparison.
            // Guaranteed: every roll must satisfy it. Never sum duplicate/scoped lines.
            auto n=*f.numeric; bool match=false;
            if(n.op==Op::Eq)match=f.guaranteed?(*p.min==n.value&&*p.max==n.value):(*p.min<=n.value&&*p.max>=n.value);
            else if(n.op==Op::Ge||n.op==Op::Gt)match=compare(f.guaranteed?*p.min:*p.max,n.op,n.value);
            else match=compare(f.guaranteed?*p.max:*p.min,n.op,n.value);
            if(match){ok=true;break;}
        }
        if(!ok)return false;
    }
    return true;
}
Results execute(const Database& db,const Query& input) {
    auto start=std::chrono::steady_clock::now();
    Query q=input;
    if(std::find(Tabs.begin(),Tabs.end(),q.tab)==Tabs.end())throw std::runtime_error("Unknown tab");
    if(q.sort!="name"&&q.sort!="base"&&!Numbers.contains(q.sort))throw std::runtime_error("Unknown sort field");
    for(auto& [key,values]:q.any) { if(!Fields.contains(key))throw std::runtime_error("Unknown field: "+key);for(auto& v:values)v=lower(v); }
    for(auto& n:q.numeric)if(!Numbers.contains(n.field)||!std::isfinite(n.value))throw std::runtime_error("Invalid numeric filter");
    for(auto& p:q.properties){p.id=lower(p.id);if(p.numeric&&!std::isfinite(p.numeric->value))throw std::runtime_error("Invalid property value");}
    for(auto& r:q.runes)r=lower(r);for(auto& r:q.sequence)r=lower(r);
    auto terms=split(q.text,'+');
    Results out;out.indices.reserve(db.records.size());
    for(size_t i=0;i<db.records.size();++i)if(matches(db.records[i],q,terms))out.indices.push_back(i);
    auto textKey=[&](const Record& r){if(q.sort=="name")return lower(r.name);auto it=r.fields.find("base");return it==r.fields.end()||it->second.empty()?std::string{}:it->second.front();};
    std::sort(out.indices.begin(),out.indices.end(),[&](size_t a,size_t b){
        const auto& x=db.records[a];const auto& y=db.records[b];
        if(q.sort=="name"||q.sort=="base") {auto kx=textKey(x),ky=textKey(y);if(kx!=ky)return q.descending?kx>ky:kx<ky;}
        else {auto ix=x.numbers.find(q.sort),iy=y.numbers.find(q.sort);if((ix==x.numbers.end())!=(iy==y.numbers.end()))return ix!=x.numbers.end();if(ix!=x.numbers.end()&&ix->second!=iy->second)return q.descending?ix->second>iy->second:ix->second<iy->second;}
        return x.id<y.id;
    });
    out.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return out;
}
static Numeric parseNumeric(std::string clause) {
    auto pos=clause.find_first_of("<>=");if(pos==clause.npos)throw std::runtime_error("Expected numeric comparison");
    Numeric n;n.field=trim(clause.substr(0,pos));auto rest=clause.substr(pos);size_t len=1;
    if(rest.starts_with(">=")){n.op=Op::Ge;len=2;}else if(rest.starts_with("<=")){n.op=Op::Le;len=2;}else if(rest[0]=='>')n.op=Op::Gt;else if(rest[0]=='<')n.op=Op::Lt;else n.op=Op::Eq;
    auto v=trim(rest.substr(len));size_t used=0;try{n.value=std::stod(v,&used);}catch(...){throw std::runtime_error("Invalid number: "+v);}if(used!=v.size()||!std::isfinite(n.value))throw std::runtime_error("Invalid number: "+v);return n;
}
Query parseQuery(std::string tab,std::string text,const std::string& clauses,std::string sort) {
    Query q;q.tab=std::move(tab);q.text=std::move(text);q.sort=std::move(sort);
    if(q.sort.starts_with("-")){q.descending=true;q.sort.erase(0,1);}
    for(auto clause:split(clauses,';')) {
        if(clause.starts_with("prop:")) {
            PropertyFilter p;auto value=trim(clause.substr(5));
            if(value.starts_with("!")){p.guaranteed=true;value.erase(0,1);}
            auto scope=value.find('@');if(scope!=value.npos){p.scope=trim(value.substr(scope+1));value=trim(value.substr(0,scope));}
            if(value.find_first_of("<>=")!=value.npos){auto n=parseNumeric(value);p.id=n.field;p.numeric=n;}else p.id=value;
            if(p.id.empty())throw std::runtime_error("Missing property id");q.properties.push_back(p);continue;
        }
        auto pos=clause.find_first_of("<>=");if(pos==clause.npos)throw std::runtime_error("Use field=value or prop:id");
        auto key=trim(clause.substr(0,pos)),value=trim(clause.substr(pos+1));
        if(Numbers.contains(key)){q.numeric.push_back(parseNumeric(clause));continue;}
        if(clause[pos]!='='||value.empty())throw std::runtime_error("Expected field=value");
        if(key=="runes")q.runes=split(value,',');else if(key=="sequence")q.sequence=split(value,',');
        else if(Fields.contains(key)) {auto choices=split(value,'|');if(q.any.contains(key))throw std::runtime_error("Use | for OR within "+key);q.any[key]=choices;}
        else throw std::runtime_error("Unknown field: "+key);
    }
    return q;
}
Query queryFromJson(const Json& j){auto q=parseQuery(j.value("tab","uniques"),j.value("text",""),j.value("filters",""),j.value("sort","name"));return q;}
}
