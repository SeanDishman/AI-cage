// This trusted host process exposes only fixed sandbox-controller operations.
// It never executes a prompt or a requested command on Windows.
#include "process.hpp"
#include "httplib.h"
#include "json.hpp"
#include <shellapi.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <thread>

using json=nlohmann::json;
using namespace localui;
namespace fs=std::filesystem;
using namespace std::chrono_literals;

namespace {
std::string text_file(const fs::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error("Cannot read UI asset: "+utf8(path.wstring()));
    return {std::istreambuf_iterator<char>(input),{}};
}
void respond(httplib::Response& response,const json& body,int code=200) {
    response.status=code;
    response.set_content(body.dump(-1,' ',false,json::error_handler_t::replace),"application/json; charset=utf-8");
}
bool same_secret(const std::string& a,const std::string& b) {
    if(a.size()!=b.size()) return false;
    unsigned char diff=0; for(size_t i=0;i<a.size();++i) diff|=static_cast<unsigned char>(a[i]^b[i]);
    return diff==0;
}
std::string clipped(const std::string& value,size_t limit=24000) {
    if(value.size()<=limit) return value;
    size_t end=limit; while(end && (static_cast<unsigned char>(value[end])&0xc0)==0x80) --end;
    return value.substr(0,end)+"\n[output shortened by the UI]";
}
struct Job {
    std::string id=random_id();
    std::shared_ptr<Process> process;
    std::mutex mutex;
    std::condition_variable finished;
    std::deque<json> events;
    size_t next=1;
    bool done=false;
    void push(json event) {
        std::lock_guard lock(mutex);
        event["seq"]=next++; events.push_back(std::move(event));
        while(events.size()>256) events.pop_front();
    }
};
class Application {
public:
    Application(fs::path controller,std::string name):controller_(std::move(controller)),name_(std::move(name)) {}
    Result command(std::vector<std::string> args) {
        args.insert(args.begin(),{"--name",name_});
        Process process(controller_.wstring(),args); return process.read(45s);
    }
    json status() {
        auto result=command({"status"});
        json state={{"instance",name_},{"available",result.code==0},{"running",false},
                    {"internet",false},{"sensors",false},{"monitor",false}};
        if(result.code!=0) { state["error"]=clipped(result.output,4000); return state; }
        for(auto pair:{std::pair{"running","Workload: running"},{"internet","Internet: on"},
                       {"sensors","Sensors: on"},{"monitor","External monitor: running"}})
            state[pair.first]=result.output.find(pair.second)!=std::string::npos;
        if(result.output.find("Workload: ")==std::string::npos) {
            state["available"]=false; state["error"]="The controller returned an unrecognized status.";
        }
        {std::lock_guard lock(jobs_mutex_); state["activeJob"]=active_?active_->id:"";}
        return state;
    }
    json logs() {
        auto result=command({"logs"});
        if(result.code!=0) throw std::runtime_error(clipped(result.output,4000));
        json entries=json::array(); std::istringstream lines(result.output); std::string line;
        while(std::getline(lines,line)) {
            auto entry=json::parse(line,nullptr,false);
            if(entry.is_object()) entries.push_back({{"time",entry.value("time",0LL)},
                {"type",entry.value("type",std::string("event"))},{"detail",clipped(entry.value("detail",std::string{}),4000)}});
        }
        return {{"entries",entries}};
    }
    json control(const json& body) {
        auto action=body.value("action",std::string{});
        if(action!="start"&&action!="stop"&&action!="internet"&&action!="sensors")
            throw std::invalid_argument("Unknown sandbox control");
        std::unique_lock lifecycle(lifecycle_);
        if(action=="stop") {
            std::shared_ptr<Job> job;
            {std::lock_guard lock(jobs_mutex_); job=active_;}
            // Cancel and join the launcher before stopping the sandbox: a delayed
            // run command must never restart it after an emergency stop.
            if(job) {
                job->process->cancel(); std::unique_lock lock(job->mutex);
                if(!job->finished.wait_for(lock,10s,[&]{return job->done;}))
                    throw std::runtime_error("Agent launcher has not exited. Use AICage.exe stop from the terminal.");
            }
        }
        std::vector<std::string> args{action};
        if(action=="internet"||action=="sensors") {
            if(!body.contains("enabled")||!body["enabled"].is_boolean()) throw std::invalid_argument("enabled must be a boolean");
            args.push_back(body["enabled"].get<bool>()?"on":"off");
        }
        auto result=command(args);
        if(result.code!=0) throw std::runtime_error(clipped(result.output,4000));
        lifecycle.unlock();
        auto current=status();
        if(action=="stop" && (!current.value("available",false)||current.value("running",true)))
            throw std::runtime_error("Stop was requested, but the controller could not confirm the sandbox is stopped.");
        return {{"message",clipped(result.output,4000)},{"state",current}};
    }
    json chat(const json& body) {
        if(!body.contains("message")||!body["message"].is_string()) throw std::invalid_argument("A message is required");
        std::string prompt=body["message"],model=body.value("model",std::string("gpt-6-luna")),thread=body.value("thread",std::string{});
        if(prompt.empty()||prompt.find_first_not_of(" \r\n\t")==std::string::npos||prompt.size()>12000||prompt.find('\0')!=std::string::npos)
            throw std::invalid_argument("Write a message of up to 12,000 UTF-8 bytes");
        if(model!="gpt-6-luna"&&model!="") throw std::invalid_argument("Choose GPT-6 Luna or your Codex default model");
        if(!thread.empty()&&!std::regex_match(thread,std::regex("[a-fA-F0-9]{8}(-[a-fA-F0-9]{4}){3}-[a-fA-F0-9]{12}")))
            throw std::invalid_argument("Invalid conversation ID");
        std::lock_guard lifecycle(lifecycle_);
        {std::lock_guard lock(jobs_mutex_); if(active_) throw std::invalid_argument("Another message is still running");}
        auto state=status();
        if(!state.value("available",false)) throw std::runtime_error(state.value("error",std::string("Controller unavailable")));
        if(!state.value("running",false)) throw std::invalid_argument("Start the sandbox before sending a message");
        if(!state.value("internet",false)) throw std::invalid_argument("Turn on internet to connect Codex to OpenAI");
        std::vector<std::string> args{"--name",name_,"run","codex","exec"};
        if(!thread.empty()) args.push_back("resume");
        args.insert(args.end(),{"--json","--skip-git-repo-check","--dangerously-bypass-approvals-and-sandbox"});
        if(!model.empty()) args.insert(args.end(),{"--model",model});
        if(!thread.empty()) args.push_back(thread);
        // An argv value avoids the broker's interactive terminal line discipline.
        // '--' prevents a prompt beginning with '-' from becoming a Codex flag.
        args.push_back("--"); args.push_back(prompt);
        auto job=std::make_shared<Job>(); job->process=std::make_shared<Process>(controller_.wstring(),args);
        {std::lock_guard lock(jobs_mutex_);
            while(jobs_.size()>=16) {auto oldest=order_.front();order_.pop_front();jobs_.erase(oldest);}
            jobs_[job->id]=job;order_.push_back(job->id);active_=job;
        }
        std::thread([this,job]{consume(job);}).detach(); return {{"job",job->id}};
    }
    json events(const std::string& id,size_t after) {
        std::shared_ptr<Job> job;
        {std::lock_guard lock(jobs_mutex_); auto found=jobs_.find(id);if(found==jobs_.end())throw std::invalid_argument("Conversation stream expired"); job=found->second;}
        std::lock_guard lock(job->mutex); json entries=json::array();
        for(auto& event:job->events) if(event["seq"].get<size_t>()>after) entries.push_back(event);
        bool gap=!job->events.empty() && after+1<job->events.front()["seq"].get<size_t>();
        return {{"events",entries},{"done",job->done},{"gap",gap}};
    }
private:
    fs::path controller_; std::string name_;
    std::mutex lifecycle_,jobs_mutex_;
    std::map<std::string,std::shared_ptr<Job>> jobs_;
    std::deque<std::string> order_;
    std::shared_ptr<Job> active_;
    void consume(const std::shared_ptr<Job>& job) {
        bool reply=false,turn_failed=false;std::string diagnostic;
        try {
            auto result=job->process->read(30min,[&](const std::string& line){
                auto event=json::parse(line,nullptr,false);
                if(!event.is_object()) {if(!line.empty())diagnostic=clipped(line,3000);return;}
                auto type=event.value("type",std::string{});
                if(type=="thread.started") job->push({{"type","thread"},{"id",event.value("thread_id",std::string{})}});
                else if(type=="item.started"||type=="item.updated"||type=="item.completed") {
                    if(!event.contains("item")||!event["item"].is_object())return;
                    auto item=event["item"];auto kind=item.value("type",std::string{});
                    if(kind=="agent_message"&&type=="item.completed") {
                        reply=true;job->push({{"type","message"},{"text",clipped(item.value("text",std::string{}),200000)}});
                    } else if(kind=="command_execution"||kind=="file_change"||kind=="web_search"||kind=="mcp_tool_call") {
                        std::string label=kind=="command_execution"?item.value("command",std::string("Command")):
                            kind=="file_change"?"Updated workspace files":kind=="web_search"?"Searched the web":"Used a connected tool";
                        job->push({{"type","activity"},{"id",item.value("id",std::string{})},{"label",clipped(label,2000)},
                            {"status",type=="item.completed"?"completed":"running"},{"output",clipped(item.value("aggregated_output",std::string{}))}});
                    }
                } else if(type=="error"||type=="turn.failed") {
                    turn_failed=true;
                    auto message=event.value("message",std::string{});
                    if(event.contains("error")&&event["error"].is_object()) message=event["error"].value("message",message);
                    job->push({{"type","error"},{"message",clipped(message.empty()?"Codex could not complete this message.":message,6000)}});
                } else if(type=="turn.completed") job->push({{"type","usage"},{"usage",event.value("usage",json::object())}});
            });
            if(result.timed_out) {
                auto stopped=command({"stop"});
                job->push({{"type","error"},{"message",stopped.code==0?"The 30-minute turn limit was reached. The sandbox was stopped.":"The turn timed out. Stop could not be confirmed; use the terminal stop command."}});
            } else if(result.cancelled) job->push({{"type","cancelled"}});
            else if(result.code!=0&&!turn_failed) job->push({{"type","error"},{"message",diagnostic.empty()?"Codex exited with code "+std::to_string(result.code)+". Check sandbox sign-in and connectivity.":diagnostic}});
            else if(!reply&&!turn_failed) job->push({{"type","error"},{"message","Codex finished without a reply. Check account access and the selected model."}});
        } catch(const std::exception& error) {
            job->process->cancel();
            // A parsing/output failure cannot leave unobserved agent work running.
            try {command({"stop"});} catch(...) {}
            job->push({{"type","error"},{"message",std::string(error.what())+". Sandbox stop requested; check its status."}});
        }
        job->push({{"type","done"}});
        {std::lock_guard lock(jobs_mutex_);if(active_==job)active_.reset();}
        {std::lock_guard lock(job->mutex);job->done=true;}
        job->finished.notify_all();
    }
};
}

int main(int argc,char** argv) {
    try {
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        auto root=fs::path(executable).parent_path().parent_path().parent_path();
        fs::path assets=root/"ui/web",controller=root/"build/AICage.exe";
        int port=43127;std::string name="main";bool open=false;DWORD parent_pid=0;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--open")open=true;
            else if(i+1<argc&&arg=="--controller")controller=wide(argv[++i]);
            else if(i+1<argc&&arg=="--assets")assets=wide(argv[++i]);
            else if(i+1<argc&&arg=="--name")name=argv[++i];
            else if(i+1<argc&&arg=="--port")port=std::stoi(argv[++i]);
            else if(i+1<argc&&arg=="--parent-pid") {
                auto value=std::stoull(argv[++i]);
                if(value==0||value>MAXDWORD||value==GetCurrentProcessId())throw std::runtime_error("Invalid desktop parent PID");
                parent_pid=static_cast<DWORD>(value);
            }
            else throw std::runtime_error("Usage: AICageUI [--open] [--name main] [--port 43127] [--controller PATH] [--assets PATH] [--parent-pid PID]");
        }
        // A desktop helper cannot outlive its owning Electron process, including
        // after a crash. This stops the UI helper, not the isolated workload.
        if(parent_pid) {
            HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,parent_pid);
            if(!parent)throw std::runtime_error("Cannot watch the desktop process");
            std::thread([parent]{WaitForSingleObject(parent,INFINITE);CloseHandle(parent);ExitProcess(0);}).detach();
        }
        if(port<1024||port>65535||!std::regex_match(name,std::regex("[a-z0-9][a-z0-9-]{0,30}")))throw std::runtime_error("Invalid port or sandbox name");
        if(!fs::is_regular_file(controller))throw std::runtime_error("Build the sandbox controller first: scripts/build.ps1");
        const auto page=text_file(assets/"index.html"),css=text_file(assets/"app.css"),js=text_file(assets/"app.js");
        auto token=random_id();auto host="127.0.0.1:"+std::to_string(port),origin="http://"+host;
        // The object outlives detached turn readers for this process's lifetime.
        auto app=std::make_shared<Application>(fs::absolute(controller),name);
        httplib::Server server;
        server.new_task_queue=[] {return new httplib::ThreadPool(8,16,64);};
        server.set_payload_max_length(20000);server.set_read_timeout(5,0);server.set_write_timeout(10,0);
        server.set_keep_alive_max_count(20);
        server.set_default_headers({{"Cache-Control","no-store"},{"X-Content-Type-Options","nosniff"},
            {"Referrer-Policy","no-referrer"},{"X-Frame-Options","DENY"},
            {"Permissions-Policy","camera=(), microphone=(), geolocation=()"},
            {"Content-Security-Policy","default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; font-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'"}});
        server.set_pre_routing_handler([&](const httplib::Request& req,httplib::Response& res){
            using Response=httplib::Server::HandlerResponse;
            auto request_origin=req.get_header_value("Origin");
            auto site=req.get_header_value("Sec-Fetch-Site");
            if(req.get_header_value("Host")!=host||(!request_origin.empty()&&request_origin!=origin)||
               (!site.empty()&&site!="same-origin"&&site!="none")) {
                respond(res,{{"error","Only the local AICage window may access this server"}},403);return Response::Handled;
            }
            if(req.path.rfind("/api/",0)==0&&!same_secret(req.get_header_value("Authorization"),"Bearer "+token)) {
                respond(res,{{"error","Open the UI from its launcher to reconnect securely"}},401);return Response::Handled;
            }
            if(req.method=="POST"&&req.get_header_value("Content-Type").rfind("application/json",0)!=0) {
                respond(res,{{"error","Expected a JSON request"}},415);return Response::Handled;
            }
            return Response::Unhandled;
        });
        server.set_exception_handler([](const auto&,auto& res,std::exception_ptr pointer){
            try {if(pointer)std::rethrow_exception(pointer);}catch(const json::exception&){respond(res,{{"error","Invalid request data"}},400);}
            catch(const std::invalid_argument& error){respond(res,{{"error",error.what()}},400);}
            catch(const std::exception& error){respond(res,{{"error",error.what()}},503);}
        });
        server.Get("/",[&](const auto&,auto& res){res.set_content(page,"text/html; charset=utf-8");});
        server.Get("/app.css",[&](const auto&,auto& res){res.set_content(css,"text/css; charset=utf-8");});
        server.Get("/app.js",[&](const auto&,auto& res){res.set_content(js,"text/javascript; charset=utf-8");});
        server.Get("/api/status",[app](const auto&,auto& res){respond(res,app->status());});
        server.Get("/api/logs",[app](const auto&,auto& res){respond(res,app->logs());});
        server.Post("/api/control",[app](const auto& req,auto& res){respond(res,app->control(json::parse(req.body)));});
        server.Post("/api/chat",[app](const auto& req,auto& res){respond(res,app->chat(json::parse(req.body)),202);});
        server.Get("/api/events",[app](const auto& req,auto& res){
            auto cursor=req.get_param_value("after");
            if(cursor.size()>10||(!cursor.empty()&&!std::all_of(cursor.begin(),cursor.end(),[](unsigned char c){return c>='0'&&c<='9';})))throw std::invalid_argument("Invalid event cursor");
            respond(res,app->events(req.get_param_value("job"),cursor.empty()?0:std::stoull(cursor)));
        });
        if(!server.bind_to_port("127.0.0.1",port)) throw std::runtime_error("The UI port is already in use. Close the existing AICageUI process or use --port.");
        auto url=origin+"/#token="+token;
        std::cout<<url<<"\n"<<std::flush;
        if(open)ShellExecuteW(nullptr,L"open",wide(url).c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        if(!server.listen_after_bind())throw std::runtime_error("Local UI server stopped unexpectedly");
        return 0;
    } catch(const std::exception& error) {std::cerr<<"AICage UI: "<<error.what()<<"\n";return 1;}
}
