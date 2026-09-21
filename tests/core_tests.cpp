#include <itemdb/core.hpp>
#include <iostream>
#include <fstream>
#include <stdexcept>
using namespace itemdb;
int checks=0;
void check(bool b,const char* message){++checks;if(!b)throw std::runtime_error(message);}
template<class F> void throws(F f,const char* msg){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,msg);}
int main(int argc,char** argv){try{
    check(argc==2,"fixture arg");auto db=Database::load(argv[1]);
    auto run=[&](std::string tab,std::string text,std::string f,std::string sort="name"){return execute(db,parseQuery(tab,text,f,sort));};
    check(run("uniques","fanaticism","").indices.size()==1,"search properties");
    check(run("uniques","axe + deadly","").indices.size()==2,"multi-term search");
    check(run("uniques","","tier=elite;type=axe;prop:deadly_strike;prop:ias>=30").indices.size()==1,"AND all categories");
    check(run("uniques","","tier=elite|normal;type=axe").indices.size()==2,"OR within category");
    check(run("uniques","","prop:ias>=40").indices.size()==1,"possible roll");
    check(run("uniques","","prop:!ias>=40").indices.empty(),"guaranteed roll");
    check(run("uniques","","prop:ias<=25").indices.size()==2,"possible lower bound");
    check(run("uniques","","prop:!ias<=25").indices.size()==1,"guaranteed upper bound");
    check(run("uniques","","prop:ias=35").indices.size()==1,"equality within range");
    check(run("uniques","","prop:!ias=35").indices.empty(),"guaranteed equality");
    check(run("uniques","","required_level<=75;strength<=150").indices.size()==1,"numeric and unknown missing");
    check(run("uniques","","tier=exceptional").indices.empty(),"tier");
    check(run("uniques","","prop:fcr").indices.empty(),"exclude conditional bonus");
    check(run("uniques","","prop:life").indices.empty(),"exclude per-level");
    check(run("uniques","","prop:ias>=30@armor").indices.empty(),"property scope");
    check(run("runewords","","runes=jah;type=sword;rune_count=4;prop:aura").indices.size()==1,"runes AND other categories");
    check(run("runewords","","runes=jah,jah").indices.empty(),"duplicate rune multiplicity");
    check(run("runewords","","sequence=jah,ith,ber,el").indices.size()==1,"exact sequence");
    check(run("runewords","","sequence=ber,ith,jah,el").indices.empty(),"ordered sequence");
    auto result=run("uniques","","","required_level");check(db.records[result.indices[0]].name=="Alpha Axe","numeric sort");
    result=run("uniques","","","-required_level");check(db.records[result.indices[0]].name=="Omega Axe","reverse sort");
    result=run("uniques","","","-defense");check(db.records[result.indices.back()].name=="Omega Axe","missing numbers sort last both directions");
    throws([&]{run("bad","","");},"unknown tab");throws([&]{run("uniques","","tier>elite");},"bad comparison");
    throws([&]{run("uniques","","required_level<=nan");},"NaN");throws([&]{run("uniques","","required_level<=3abc");},"trailing numeric junk");
    throws([&]{run("uniques","","required_leevl=5");},"typo rejected");throws([&]{run("uniques","","type=axe;type=sword");},"duplicate categories rejected");
    Session session;session.setQuery(parseQuery("uniques","alpha","type=axe"),"type=axe");session.switchTab(2);check(session.current().query.text.empty(),"independent tab");session.setQuery(parseQuery("runewords","","runes=jah"),"runes=jah");session.switchTab(0);check(session.current().query.text=="alpha","preserve filters");
    session.reset();check(session.current().query.text.empty(),"reset active");session.switchTab(2);check(session.current().clauses=="runes=jah","reset not other tabs");
    session.current().page=999;result=execute(db,session.current().query);session.reconcile(db,result);check(session.current().page==0 && session.current().selected.has_value(),"clamp pagination");
    session.current().query.text="nothing";result=execute(db,session.current().query);session.reconcile(db,result);check(!session.current().selected,"clear stale selected item");throws([&]{session.switchTab(4);},"bad tab state");
    auto j=Json{{"schema_version",1},{"records",Json::array()}};throws([&]{Database::parse(j);},"empty data");throws([&]{Database::parse(Json::object());},"missing fields");throws([&]{Database::load("not-present-itemdb.json");},"missing file");
    std::ifstream in(argv[1]);in>>j;auto duplicate=j;j["records"].push_back(j["records"][0]);throws([&]{Database::parse(j);},"duplicate id");j=duplicate;j["records"][0]["properties"][0]["min_value"]=100;throws([&]{Database::parse(j);},"inverted range");j=duplicate;j["schema_version"]=2;throws([&]{Database::parse(j);},"version");j=duplicate;j["records"][0]["numbers"]["strength"]="bad";throws([&]{Database::parse(j);},"malformed number");
    std::cout<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";return 1;}}

