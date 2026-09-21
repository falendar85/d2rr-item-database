#include <itemdb/core.hpp>
#include <fstream>
#include <set>
#include <cmath>
#include <stdexcept>
#include <cctype>

namespace itemdb {
std::string lower(std::string s) { for(auto& c:s) if(static_cast<unsigned char>(c)<128) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string trim(std::string s) { auto a=s.find_first_not_of(" \r\n\t"),b=s.find_last_not_of(" \r\n\t"); return a==s.npos?"":s.substr(a,b-a+1); }
static std::string string(const Json& j,const char* key) { auto s=j.at(key).get<std::string>(); if(s.empty()) throw std::runtime_error(std::string("empty ")+key); return s; }
Database Database::parse(const Json& j) {
    if(j.at("schema_version")!=1 || !j.at("records").is_array()) throw std::runtime_error("Unsupported database schema");
    if(j.at("records").size()>100000) throw std::runtime_error("Database exceeds 100000 records");
    Database db; db.provenance=j.value("provenance",Json::object());
    std::set<std::string> ids;
    for(const auto& x:j.at("records")) {
        Record r; r.id=string(x,"id"); r.name=string(x,"name"); r.tab=string(x,"tab");
        if(std::find(Tabs.begin(),Tabs.end(),r.tab)==Tabs.end() || !ids.insert(r.id).second) throw std::runtime_error("Invalid tab or duplicate id: "+r.id);
        r.fields=x.at("fields").get<decltype(r.fields)>();
        for(auto& [k,v]:r.fields) for(auto& s:v) s=lower(s);
        for(auto it=x.at("numbers").begin();it!=x.at("numbers").end();++it) {
            if(!it.value().is_number()) throw std::runtime_error("Invalid numeric field "+it.key());
            double n=it.value().get<double>(); if(!std::isfinite(n)) throw std::runtime_error("Nonfinite number");
            r.numbers[it.key()]=n;
        }
        r.lines=x.at("display_lines").get<std::vector<std::string>>();
        for(const auto& p:x.at("properties")) {
            Property prop; prop.id=lower(string(p,"property_id")); prop.name=p.value("canonical_name",prop.id); prop.text=p.value("text","");
            if(p.contains("min_value") && !p["min_value"].is_null()) prop.min=p["min_value"].get<double>();
            if(p.contains("max_value") && !p["max_value"].is_null()) prop.max=p["max_value"].get<double>();
            if(prop.min.has_value()!=prop.max.has_value() || (prop.min && (!std::isfinite(*prop.min)||!std::isfinite(*prop.max)||*prop.min>*prop.max))) throw std::runtime_error("Invalid property range: "+r.id);
            prop.scope=lower(p.value("scope","")); prop.conditional=p.value("conditional",false); prop.perLevel=p.value("per_level",false);
            r.properties.push_back(std::move(prop));
        }
        r.search=lower(r.name+"\n"+x.value("search_text",""));
        for(auto& [k,vs]:r.fields) for(auto& v:vs) r.search+="\n"+v;
        for(auto& l:r.lines) r.search+="\n"+lower(l);
        db.records.push_back(std::move(r));
    }
    if(db.records.empty()) throw std::runtime_error("Empty database");
    return db;
}
Database Database::load(const std::filesystem::path& path) {
    std::error_code ec; auto n=std::filesystem::file_size(path,ec);
    if(ec || n>64*1024*1024) throw std::runtime_error("Missing/unreadable database or file exceeds 64 MiB: "+path.string());
    std::ifstream in(path,std::ios::binary); if(!in) throw std::runtime_error("Cannot read database");
    Json j; in>>j; return parse(j);
}
}
