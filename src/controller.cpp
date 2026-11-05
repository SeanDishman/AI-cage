#include "common.hpp"
#include <csignal>
#include <regex>
#include <thread>
#include <sys/file.h>
#include <sys/stat.h>

using namespace cage;
static constexpr const char* workload_image="aicage-workload:1";
static constexpr const char* proxy_image="aicage-proxy:1";
static Result docker(const std::vector<std::string>& args,bool capture=true){
    std::vector<std::string> cmd{"docker","--host","unix:///var/run/docker.sock"};cmd.insert(cmd.end(),args.begin(),args.end());return process(cmd,capture);
}
static std::string dock(const std::vector<std::string>& args){auto r=docker(args);require(r.code==0,"Docker: "+trim(r.out));return trim(r.out);}
struct Lock{int fd;explicit Lock(const fs::path& p){fd=open(p.c_str(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);require(fd>=0&&flock(fd,LOCK_EX)==0,"Cannot lock sandbox controls");}~Lock(){if(fd>=0)close(fd);}};
class Sandbox {
    std::string name_,container_,proxy_,internal_,external_;
    fs::path state_,build_,project_;
    std::string inspect(const std::string& object,const std::string& format){return dock({"inspect","--format",format,object});}
    bool exists(const std::string& object){return docker({"inspect",object}).code==0;}
    void own(const std::string& object){require(inspect(object,"{{index .Config.Labels \"io.aicage.managed\"}} {{index .Config.Labels \"io.aicage.instance\"}}") == "true "+name_,"Refusing to control an object not owned by this sandbox");}
    bool running(){return exists(container_)&&inspect(container_,"{{.State.Running}}") == "true";}
    int host_pid(){own(container_);return std::stoi(inspect(container_,"{{.State.Pid}}"));}
    bool monitor_alive(){
        auto text=trim(read(state_/"monitor.pid"));if(text.empty())return false;
        int pid=std::stoi(text);if(pid<=1||kill(pid,0)!=0)return false;
        std::error_code ec;auto exe=fs::canonical("/proc/"+text+"/exe",ec);return !ec&&exe==fs::canonical(build_/"cage-monitor");
    }
    void healthy(){
        require(running(),"Sandbox is stopped; run start first");
        auto pid=std::to_string(host_pid());
        require(monitor_alive()&&trim(read(state_/"monitor.ready"))==pid,"Monitor unavailable; refusing to launch commands");
        auto h=trim(read(state_/"heartbeat"));require(!h.empty()&&time(nullptr)-std::stoll(h)<10,"Monitor heartbeat is stale");
        auto tracer=read("/proc/"+pid+"/status");
        require(tracer.find("TracerPid:\t"+trim(read(state_/"monitor.pid"))+"\n")!=std::string::npos,"Workload is not attached to the expected monitor");
    }
    fs::path firewall(){auto p=build_/"firewall/cage-firewall";require(fs::exists(p),"Build cage-firewall first");return p;}
    void log(const std::string& t,const std::string& detail){event(state_/"events.jsonl",t,detail);}
    void proxy_config(){
        std::string config=R"(http_port 3128
visible_hostname aicage-egress
pid_filename /tmp/squid.pid
cache_log /dev/stderr
coredump_dir /tmp
cache deny all
pinger_enable off
shutdown_lifetime 1 seconds
acl safe_ports port 80 443
acl ssl_ports port 443
acl connect method CONNECT
acl private dst 0.0.0.0/8 10.0.0.0/8 100.64.0.0/10 127.0.0.0/8 169.254.0.0/16 172.16.0.0/12 192.0.0.0/24 192.0.2.0/24 192.88.99.0/24 192.168.0.0/16 198.18.0.0/15 198.51.100.0/24 203.0.113.0/24 224.0.0.0/4 240.0.0.0/4 ::/128 ::1/128 fc00::/7 fe80::/10 ff00::/8 2001:db8::/32 2001::/23 2002::/16 3fff::/20 64:ff9b::/96 64:ff9b:1::/48
http_access deny !safe_ports
http_access deny connect !ssl_ports
http_access deny private
http_access allow all
forwarded_for delete
via on
logformat cage %ts.%03tu %rm %>rd %Ss/%>Hs
)";
        config+=trim(read(state_/"sensors"))=="off"?"access_log none\n":"access_log stdio:/dev/stdout cage\n";
        write(state_/"proxy/squid.conf",config);chmod((state_/"proxy/squid.conf").c_str(),0644);
    }
    void network(const std::string& n,bool internal){
        if(docker({"network","inspect",n}).code==0){
            require(dock({"network","inspect","--format","{{index .Labels \"io.aicage.instance\"}}",n})==name_,"Network name is already owned by another application");return;
        }
        std::vector<std::string> a{"network","create","--label","io.aicage.managed=true","--label","io.aicage.instance="+name_};if(internal)a.push_back("--internal");a.push_back(n);dock(a);
    }
    void provision(){
        network(internal_,true);network(external_,false);proxy_config();
        if(!exists(proxy_)){
            dock({"create","--name",proxy_,"--label","io.aicage.managed=true","--label","io.aicage.instance="+name_,"--network",internal_,"--read-only","--cap-drop","ALL","--security-opt","no-new-privileges=true","--pids-limit","64","--memory","256m","--memory-swap","256m","--cpus","0.5","--tmpfs","/tmp:rw,noexec,nosuid,size=64m","--tmpfs","/run:rw,noexec,nosuid,size=8m","--mount","type=bind,source="+(state_/"proxy").string()+",target=/etc/aicage,readonly","--log-opt","max-size=4m","--log-opt","max-file=2",proxy_image});
            dock({"network","connect",external_,proxy_});
        }
        own(proxy_);
        if(inspect(proxy_,"{{.State.Running}}")!="true")dock({"start",proxy_});
        std::string ip=inspect(proxy_,"{{(index .NetworkSettings.Networks \""+internal_+"\").IPAddress}}");
        require(!ip.empty(),"Proxy has no internal address");write(state_/"proxy.ip",ip);
        if(exists(container_)){own(container_);return;}
        std::vector<std::string> args{"create","--name",container_,"--label","io.aicage.managed=true","--label","io.aicage.instance="+name_,"--hostname","aicage","--network",internal_,"--user","0:0","--cap-drop","ALL"};
        for(auto cap:{"CHOWN","DAC_OVERRIDE","FOWNER","FSETID","SETUID","SETGID","KILL"}){args.push_back("--cap-add");args.push_back(cap);}
        std::vector<std::string> rest{"--security-opt","no-new-privileges=true","--pids-limit","256","--memory","4g","--memory-swap","4g","--cpus","2","--ulimit","nofile=4096:4096","--ulimit","core=0","--ipc","private","--cgroupns","private","--restart","no","--mount","type=volume,source="+container_+"-home,target=/root","--mount","type=volume,source="+container_+"-work,target=/workspace","--mount","type=bind,source=/var/lib/aicage/bin/broker,target=/aicage/broker,readonly","--tmpfs","/run:rw,noexec,nosuid,size=32m","--tmpfs","/tmp:rw,nosuid,size=512m","--env","HOME=/root","--env","CODEX_HOME=/root/.codex","--env","TERM=xterm-256color","--env","HTTP_PROXY=http://"+ip+":3128","--env","HTTPS_PROXY=http://"+ip+":3128","--env","http_proxy=http://"+ip+":3128","--env","https_proxy=http://"+ip+":3128","--env","NO_PROXY=localhost,127.0.0.1,::1","--env","no_proxy=localhost,127.0.0.1,::1","--log-opt","max-size=4m","--log-opt","max-file=2",workload_image};
        args.insert(args.end(),rest.begin(),rest.end());dock(args);
    }
public:
    explicit Sandbox(std::string n):name_(std::move(n)),container_("aicage-"+name_),proxy_(container_+"-proxy"),internal_(container_+"-inside"),external_(container_+"-outside"),state_("/var/lib/aicage/"+name_),build_(fs::path(own_exe()).parent_path()),project_(build_.parent_path()){
        require(std::regex_match(name_,std::regex("[a-z][a-z0-9-]{0,23}")),"Name must be 1-24 lowercase letters, digits or hyphens");
        require(geteuid()==0,"Run on the Docker host as root (the Windows launcher handles this)");
        umask(0077);fs::create_directories(state_/"proxy");chmod((state_/"proxy").c_str(),0755);
        if(!fs::exists(state_/"sensors"))write(state_/"sensors","on");
        if(!fs::exists(state_/"internet"))write(state_/"internet","off");
    }
    void setup(){
        Lock lock(state_/"control.lock");
        require(!running(),"Stop this sandbox before running setup");
        require(dock({"info","--format","{{.OSType}}"})=="linux","A local Linux Docker engine is required");
        require(fs::exists(build_/"cage-broker")&&fs::exists(build_/"cage-monitor"),"Build Linux binaries first");firewall();
        fs::create_directories("/var/lib/aicage/bin");
        fs::copy_file(build_/"cage-broker","/var/lib/aicage/bin/broker.new",fs::copy_options::overwrite_existing);chmod("/var/lib/aicage/bin/broker.new",0755);fs::rename("/var/lib/aicage/bin/broker.new","/var/lib/aicage/bin/broker");
        for(auto target:{"workload","proxy"}){
            const char* tag=std::string(target)=="workload"?workload_image:proxy_image;
            require(docker({"build","--target",target,"--tag",tag,"--file",(project_/"container/Dockerfile").string(),project_.string()},false).code==0,"Image build failed");
        }
        provision();log("setup","Provisioned isolated workspace and web proxy");std::cout<<"Setup complete. Workload is stopped; internet defaults to off.\n";
    }
    void start(){
        Lock lock(state_/"control.lock");
        if(running()){healthy();return;}
        require(exists(container_),"Run setup first");own(container_);own(proxy_);
        if(inspect(proxy_,"{{.State.Running}}")!="true")dock({"start",proxy_});
        for(int i=0;i<50&&monitor_alive();i++)std::this_thread::sleep_for(std::chrono::milliseconds(100));
        require(!monitor_alive(),"Previous monitor is still exiting; try again shortly");
        fs::remove(state_/"monitor.ready");fs::remove(state_/"heartbeat");
        dock({"start",container_});int pid=host_pid();
        try{
            bool gate=false;
            for(int i=0;i<100;i++){
                auto status=read("/proc/"+std::to_string(pid)+"/status");auto pos=status.find("SigCgt:\t");
                if(pos!=std::string::npos){auto caught=std::stoull(status.substr(pos+8,16),nullptr,16);if(caught&(1ULL<<(SIGUSR1-1))){gate=true;break;}}
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            require(gate,"Workload did not enter its startup gate");
            auto ip=inspect(proxy_,"{{(index .NetworkSettings.Networks \""+internal_+"\").IPAddress}}");
            require(ip==trim(read(state_/"proxy.ip")),"Proxy address changed; rebuild this sandbox before launch");
            must({firewall().string(),"setup",std::to_string(pid),ip,trim(read(state_/"internet"))});
            pid_t child=fork();require(child>=0,"Cannot start monitor");
            if(child==0){
                setsid();int fd=open((state_/"monitor.log").c_str(),O_CREAT|O_WRONLY|O_TRUNC,0600);int null=open("/dev/null",O_RDONLY);dup2(null,0);dup2(fd,1);dup2(fd,2);close(fd);close(null);
                auto executable=(build_/"cage-monitor").string();auto p=std::to_string(pid);auto dir=state_.string();execl(executable.c_str(),executable.c_str(),p.c_str(),dir.c_str(),nullptr);_exit(127);
            }
            bool ready=false;
            for(int i=0;i<100;i++){if(trim(read(state_/"monitor.ready"))==std::to_string(pid)&&monitor_alive()){ready=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(50));}
            require(ready,"External monitor failed: "+read(state_/"monitor.log"));
            // Only the trusted controller releases the first workload execution.
            require(kill(pid,SIGUSR1)==0,"Cannot release startup gate");
            bool broker=false;
            for(int i=0;i<100;i++){if(docker({"exec",container_,"/aicage/broker","ping"}).code==0){broker=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(50));}
            require(broker,"Broker failed to start");healthy();log("started","Firewall, seccomp and external monitor active");
            std::cerr<<"Sandbox started. Internet "<<trim(read(state_/"internet"))<<"; sensors "<<trim(read(state_/"sensors"))<<".\n";
        }catch(...){docker({"kill",container_});throw;}
    }
    void internet(const std::string& mode){
        require(mode=="on"||mode=="off","Use internet on|off");Lock lock(state_/"control.lock");
        if(running()){if(mode=="on")healthy();must({firewall().string(),"set",std::to_string(host_pid()),mode});}
        write(state_/"internet",mode);log("internet",mode);std::cout<<"Internet "<<mode<<". Host/LAN access stays blocked.\n";
    }
    void sensors(const std::string& mode){
        require(mode=="on"||mode=="off","Use sensors on|off");Lock lock(state_/"control.lock");
        write(state_/"sensors",mode);proxy_config();
        if(exists(proxy_)&&inspect(proxy_,"{{.State.Running}}") == "true"){own(proxy_);dock({"kill","--signal","HUP",proxy_});}
        log("sensors",mode);std::cout<<"Access-attempt recording "<<mode<<". Kernel isolation remains active.\n";
    }
    void stop(){Lock lock(state_/"control.lock");if(running())must({firewall().string(),"kill",container_});log("stopped","Workload stopped; files and installed packages retained");std::cout<<"Sandbox stopped.\n";}
    void status(){
        std::cout<<"AICage / "<<name_<<"\nWorkload: "<<(running()?"running":"stopped")<<"\nInternet: "<<trim(read(state_/"internet"))<<" (public HTTP/HTTPS only)\nSensors: "<<trim(read(state_/"sensors"))<<"\nExternal monitor: "<<(monitor_alive()?"running":"stopped")<<"\nWorkspace: isolated Docker volume\nLimits: 4 GiB RAM, 2 CPU cores, 256 processes\n";
        if(running())healthy();
    }
    int run(std::vector<std::string> command,bool capture=false){
        start();healthy();require(!command.empty(),"Missing sandbox command");
        std::vector<std::string> a{"exec","-i"};if(!capture&&isatty(0)&&isatty(1))a.push_back("-t");a.push_back(container_);a.push_back("/aicage/broker");a.push_back("client");a.insert(a.end(),command.begin(),command.end());
        auto result=docker(a,capture);if(capture)std::cout<<result.out;return result.code;
    }
    void import_auth(const fs::path& auth){
        Lock lock(state_/"control.lock");require(!running(),"Stop this sandbox before importing the account cache");require(exists(container_),"Run setup first");own(container_);
        require(fs::is_regular_file(auth)&&fs::file_size(auth)>0&&fs::file_size(auth)<1024*1024,"Expected a Codex auth.json file");
        dock({"cp",fs::absolute(auth).string(),container_+":/root/.codex/auth.json"});
        log("account_imported","Copied only the requested Codex auth cache; no token contents recorded");std::cout<<"Codex sign-in copied into the isolated home.\n";
    }
    void logs(bool proxy){if(proxy){own(proxy_);docker({"logs","--tail","100",proxy_},false);return;}auto text=read(state_/"events.jsonl");size_t at=text.size();int lines=0;while(at>0&&lines<100){--at;if(text[at]=='\n')++lines;}std::cout<<text.substr(at?at+1:0);}
    int menu(){
        for(;;){std::cout<<"\nAICage  |  Codex inside an isolated Linux workspace\n\n1  Open Codex\n2  Open sandbox shell\n3  Internet ON\n4  Internet OFF\n5  Sensors ON\n6  Sensors OFF\n7  Status\n8  Sensor log\n9  Emergency stop\n0  Exit controls\n\nChoose: "<<std::flush;
            std::string choice;if(!std::getline(std::cin,choice))return 0;
            try{if(choice=="0")return 0;else if(choice=="1")run({"codex","--dangerously-bypass-approvals-and-sandbox","--no-alt-screen"});else if(choice=="2")run({"bash"});else if(choice=="3")internet("on");else if(choice=="4")internet("off");else if(choice=="5")sensors("on");else if(choice=="6")sensors("off");else if(choice=="7")status();else if(choice=="8")logs(false);else if(choice=="9")stop();else std::cout<<"Choose 0 through 9.\n";}catch(const std::exception& e){std::cerr<<e.what()<<"\n";}
        }
    }
};
int main(int argc,char** argv){
    try{
        std::vector<std::string> args;for(int i=1;i<argc;i++)args.emplace_back(argv[i]);std::string name="main";
        if(args.size()>=2&&args[0]=="--name"){name=args[1];args.erase(args.begin(),args.begin()+2);}
        if(args.empty())args.push_back("menu");
        if(args[0]=="help"||args[0]=="--help"){std::cout<<"AICage [--name NAME] setup|start|stop|status|menu\n       internet on|off | sensors on|off | logs [--proxy]\n       codex [ARGS] | shell | run COMMAND [ARGS]\n       import-auth /path/to/auth.json | login\nAll commands execute inside the workload; network control and logs stay outside.\n";return 0;}
        Sandbox s(name);const auto cmd=args[0];
        if(cmd=="setup")s.setup();else if(cmd=="start")s.start();else if(cmd=="stop"||cmd=="kill")s.stop();else if(cmd=="status")s.status();
        else if(cmd=="internet"&&args.size()==2)s.internet(args[1]);else if(cmd=="sensors"&&args.size()==2)s.sensors(args[1]);
        else if(cmd=="import-auth"&&args.size()==2)s.import_auth(args[1]);else if(cmd=="logs")s.logs(args.size()>1&&args[1]=="--proxy");
        else if(cmd=="menu")return s.menu();else if(cmd=="shell")return s.run({"bash"});else if(cmd=="login")return s.run({"codex","login","--device-auth"});
        else if(cmd=="codex"){std::vector<std::string> c{"codex","--dangerously-bypass-approvals-and-sandbox","--no-alt-screen"};c.insert(c.end(),args.begin()+1,args.end());return s.run(c);}
        else if(cmd=="run"&&args.size()>1)return s.run({args.begin()+1,args.end()});else throw std::runtime_error("Unknown command. Use help.");
        return 0;
    }catch(const std::exception& e){std::cerr<<"AICage: "<<e.what()<<"\n";return 125;}
}
