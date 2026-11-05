// Test-only controller. Every reply is synthetic; never used by the UI launcher.
#include "json.hpp"
#include <windows.h>
#include <shellapi.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using json=nlohmann::json;
namespace fs=std::filesystem;
std::string utf8(const wchar_t* value) {
    int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    std::string result(n,'\0');WideCharToMultiByte(CP_UTF8,0,value,-1,result.data(),n,nullptr,nullptr);result.pop_back();return result;
}
int main() {
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    std::vector<std::string> args;for(int i=1;i<argc;i++)args.push_back(utf8(argv[i]));LocalFree(argv);
    if(args.size()<3)return 1;args.erase(args.begin(),args.begin()+2);
    const char* directory=std::getenv("AICAGE_UI_FIXTURE_DIR");if(!directory)return 1;
    fs::path root=directory; fs::create_directories(root);
    auto on=[&](const char* name){return fs::exists(root/name);};
    auto put=[&](const char* name,bool value){if(value){std::ofstream(root/name)<<"1";}else fs::remove(root/name);};
    auto send=[&](const json& event){std::cout<<event.dump()<<"\n"<<std::flush;};
    if(args[0]=="status") {
        std::cout<<"AICage / main\nWorkload: "<<(on("running")?"running":"stopped")<<"\nInternet: "<<(on("internet")?"on":"off")<<" (public HTTP/HTTPS only)\nSensors: "<<(on("sensors")?"on":"off")<<"\nExternal monitor: "<<(on("running")?"running":"stopped")<<"\n";
    } else if(args[0]=="start")put("running",true);
    else if(args[0]=="stop")put("running",false);
    else if(args[0]=="internet"||args[0]=="sensors")put(args[0].c_str(),args.at(1)=="on");
    else if(args[0]=="logs")send({{"time",1793908800},{"type","fixture_event"},{"detail","Synthetic test event; no live sandbox data."}});
    else if(args[0]=="run") {
        std::ofstream(root/"arguments.json")<<json(args).dump();
        if(args.at(1)!="codex"||args.at(2)!="exec")return 2;
        auto prompt=args.back();
        send({{"type","thread.started"},{"thread_id","01234567-89ab-cdef-0123-456789abcdef"}});
        if(prompt=="test-error") {send({{"type","turn.failed"},{"error",{{"message","Synthetic model access error"}}}});return 3;}
        send({{"type","item.started"},{"item",{{"id","command_1"},{"type","command_execution"},{"command","printf 'synthetic test'"}}}});
        if(prompt=="test-long") {for(int i=0;i<200;i++){std::this_thread::sleep_for(std::chrono::milliseconds(100));if(!on("running"))return 137;}}
        else std::this_thread::sleep_for(std::chrono::milliseconds(350));
        send({{"type","item.completed"},{"item",{{"id","command_1"},{"type","command_execution"},{"command","printf 'synthetic test'"},{"aggregated_output","Synthetic test output"}}}});
        send({{"type","item.completed"},{"item",{{"id","message_1"},{"type","agent_message"},{"text","Synthetic fixture reply: "+prompt+"\n\n```cpp\nint main() { return 0; }\n```"}}}});
        send({{"type","turn.completed"},{"usage",{{"input_tokens",10},{"output_tokens",20}}}});
    } else return 2;
    return 0;
}
