#include <itemdb/core.hpp>
#include <itemdb/prototype.hpp>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <stdexcept>
using namespace itemdb;
int checks=0;
void check(bool b,const char* message){++checks;if(!b)throw std::runtime_error(message);}
template<class F> void throws(F f,const char* msg){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,msg);}
const Json* findNode(const Json& node,const std::string& name){
    if(node.is_object()&&node.value("name",std::string{})==name)return &node;
    if(node.is_object()&&node.contains("children"))for(const auto& child:node["children"])if(auto* found=findNode(child,name))return found;
    return nullptr;
}
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
    PrototypeViewModel prototype(db,1);check(prototype.uniqueCount()==2,"prototype unique count");check(prototype.visibleUniqueCount()==1,"bounded prototype page");
    check(prototype.selectedUnique()!=nullptr&&prototype.selectedUnique()->name=="Alpha Axe","default unique selection");
    auto detail=prototype.detailFor(0);check(detail.title=="Alpha Axe","detail title");
    check(std::find(detail.lines.begin(),detail.lines.end(),"Base: Hatchet")!=detail.lines.end(),"detail base from normalized field");
    check(std::find(detail.lines.begin(),detail.lines.end(),"Tier: Normal")!=detail.lines.end(),"detail tier from normalized field");
    check(std::find(detail.lines.begin(),detail.lines.end(),"Required Level: 20")!=detail.lines.end(),"detail numeric from normalized field");
    check(std::find(detail.lines.begin(),detail.lines.end(),"20% Increased Attack Speed")!=detail.lines.end(),"detail property line from normalized data");
    check(!prototype.selectUnique(1)&&prototype.selectedUnique()->name=="Alpha Axe","invalid selection is safe");
    check(prototype.switchTab(1)&&prototype.activeTab()==1,"set tab state");check(prototype.switchTab(2)&&prototype.activeTab()==2,"runeword tab state");check(prototype.switchTab(3)&&prototype.activeTab()==3,"base tab state");
    check(!prototype.switchTab(4)&&prototype.activeTab()==3,"invalid tab is safe");check(prototype.switchTab(0),"return to unique tab");
    auto layout=Json::parse(buildPrototypeLayout(prototype));check(layout["type"]=="Panel"&&layout["name"]=="item-database/ItemDatabase","native panel layout root");
    const auto* background=findNode(layout,"PanelBackground");check(background!=nullptr&&(*background)["fields"]["rect"]["width"]==2688&&(*background)["fields"]["rect"]["height"]==1240&&(*background)["fields"]["color"][0]==0.0,"wider black panel background");
    const auto* close=findNode(layout,"CloseButton");check(close!=nullptr&&(*close)["fields"]["onClickMessage"]=="PanelManager:ClosePanel:item-database/ItemDatabase","native close message");check((*close)["fields"]["filename"]=="PANEL\\closebtn_4x"&&(*close)["fields"]["rect"]["x"]==2588,"corner X close button");check((*close)["fields"]["tooltipString"]=="@d2r:strClose","namespaced close tooltip");
    const auto* title=findNode(layout,"Title");check(title!=nullptr&&(*title)["fields"]["rect"]["width"]==2688&&(*title)["fields"]["style"]["alignment"]["h"]=="center","centered database title");
    for(size_t i=0;i<4;++i){const auto* button=findNode(layout,"Tab"+std::to_string(i));check(button!=nullptr,"four tab buttons");check((*button)["fields"]["onClickMessage"]=="PanelManager:ClosePanel:item-database/action/tab/"+std::string(Tabs[i]),"native tab message");check((*button)["fields"]["rect"]["x"]==50+static_cast<int>(i)*520,"separate tab borders");}
    const auto* count=findNode(layout,"Count0");check(count!=nullptr&&(*count)["fields"]["rect"]["y"]==20,"lowered Unique count");
    const auto* row=findNode(layout,"Row0_0");check(row!=nullptr&&findNode(layout,"Row0_1")==nullptr,"bounded layout rows");
    check((*row)["fields"]["rect"]["y"]==85,"lowered Unique row");
    check((*row)["fields"]["onClickMessage"]=="PanelManager:ClosePanel:item-database/action/select/uniques/0","native row message");
    check(findNode(layout,"Detail0_0")!=nullptr,"unique detail widget");
    const auto* detailTitle=findNode(layout,"DetailTitle0_0");check(detailTitle!=nullptr&&(*detailTitle)["fields"]["style"]["alignment"]["h"]=="center","centered item title");
    const auto* detailText=findNode(layout,"DetailText0_0");check(detailText!=nullptr&&(*detailText)["fields"]["style"]["alignment"]["v"]=="top","top-aligned detail text");
    check((*detailText)["fields"]["style"]["pointSize"]=="$SmallFontSize","bounded detail font size");
    for(size_t i=0;i<4;++i)check(findNode(layout,"Pane"+std::to_string(i))!=nullptr,"tab pane");
    std::cout<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";return 1;}}

