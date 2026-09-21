#include <itemdb/core.hpp>
#include <iostream>
int main(int argc,char** argv){try{if(argc<2){std::cerr<<"itemdb_cli database.json [tab] [text] [filters] [sort]\n";return 2;}auto db=itemdb::Database::load(argv[1]);auto q=itemdb::parseQuery(argc>2?argv[2]:"uniques",argc>3?argv[3]:"",argc>4?argv[4]:"",argc>5?argv[5]:"name");auto r=itemdb::execute(db,q);for(auto i:r.indices)std::cout<<db.records[i].id<<"\t"<<db.records[i].name<<"\n";std::cerr<<r.indices.size()<<" results in "<<r.milliseconds<<" ms\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
