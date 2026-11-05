// Runs on the Docker host, outside the workload's PID, mount and user scopes.
#include "common.hpp"
#include <csignal>
#include <map>
#include <array>
#include <arpa/inet.h>
#include <sys/ptrace.h>
#include <linux/ptrace.h>
#include <linux/audit.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/stat.h>

using namespace cage;
static volatile sig_atomic_t stopping=0;
static void stop(int){stopping=1;}
static void tick(int){}
static long trace(int request,pid_t pid,void* address=nullptr,void* data=nullptr){return ptrace(static_cast<__ptrace_request>(request),pid,address,data);}
static bool memory(pid_t pid,uint64_t address,void* destination,size_t len){
    auto* out=static_cast<char*>(destination);
    for(size_t i=0;i<len;i+=sizeof(long)){errno=0;long word=trace(PTRACE_PEEKDATA,pid,reinterpret_cast<void*>(address+i));if(errno)return false;memcpy(out+i,&word,std::min(sizeof word,len-i));}return true;
}
static std::string string_at(pid_t pid,uint64_t address){
    std::string out;
    for(size_t at=0;at<256;at+=sizeof(long)){long word=0;if(!memory(pid,address+at,&word,sizeof word))return out.empty()?"<unreadable>":out;const auto* p=reinterpret_cast<const char*>(&word);for(size_t i=0;i<sizeof word;i++){if(p[i]==0)return out;out+=p[i];}}
    return out+"...";
}
struct Call{long number=-1;std::string name;std::string target;bool boundary=false;bool sensitive=false;bool network=false;};
static Call describe(pid_t pid,const ptrace_syscall_info& i){
    Call c;c.number=static_cast<long>(i.entry.nr);uint64_t path=0;
    switch(c.number){
    case SYS_open:c.name="open";path=i.entry.args[0];break;
    case SYS_openat:c.name="openat";path=i.entry.args[1];break;
    case SYS_openat2:c.name="openat2";path=i.entry.args[1];break;
    case SYS_access:c.name="access";path=i.entry.args[0];break;
    case SYS_faccessat:case SYS_faccessat2:c.name="faccessat";path=i.entry.args[1];break;
    case SYS_newfstatat:c.name="stat";path=i.entry.args[1];break;
    case SYS_statx:c.name="statx";path=i.entry.args[1];break;
    case SYS_readlink:c.name="readlink";path=i.entry.args[0];break;
    case SYS_readlinkat:c.name="readlinkat";path=i.entry.args[1];break;
    case SYS_unlinkat:c.name="unlinkat";path=i.entry.args[1];break;
    case SYS_mkdirat:c.name="mkdirat";path=i.entry.args[1];break;
    case SYS_connect:{c.name="connect";c.network=true;sockaddr_storage addr{};if(i.entry.args[2]>=sizeof(sa_family_t)&&memory(pid,i.entry.args[1],&addr,std::min<uint64_t>(sizeof addr,i.entry.args[2]))){char buf[INET6_ADDRSTRLEN]{};if(addr.ss_family==AF_INET){auto* v=reinterpret_cast<sockaddr_in*>(&addr);inet_ntop(AF_INET,&v->sin_addr,buf,sizeof buf);c.target=std::string(buf)+":"+std::to_string(ntohs(v->sin_port));}else if(addr.ss_family==AF_INET6){auto* v=reinterpret_cast<sockaddr_in6*>(&addr);inet_ntop(AF_INET6,&v->sin6_addr,buf,sizeof buf);c.target="["+std::string(buf)+"]:"+std::to_string(ntohs(v->sin6_port));}}break;}
    case SYS_sendto:c.name="sendto";c.network=true;break;
    case SYS_sendmsg:c.name="sendmsg";c.network=true;break;
    case SYS_socket:c.name="socket";c.network=true;break;
    case SYS_mount:c.name="mount";c.sensitive=true;break;
    case SYS_umount2:c.name="umount";c.sensitive=true;break;
    case SYS_ptrace:c.name="ptrace";c.sensitive=true;break;
    case SYS_setns:c.name="setns";c.sensitive=true;break;
    case SYS_unshare:c.name="unshare";c.sensitive=true;break;
    case SYS_bpf:c.name="bpf";c.sensitive=true;break;
    case SYS_perf_event_open:c.name="perf_event_open";c.sensitive=true;break;
    case SYS_open_by_handle_at:c.name="open_by_handle_at";c.sensitive=true;break;
    case SYS_process_vm_readv:case SYS_process_vm_writev:c.name="process_vm";c.sensitive=true;break;
    case SYS_io_uring_setup:c.name="io_uring_setup";c.sensitive=true;break;
    case SYS_keyctl:c.name="keyctl";c.sensitive=true;break;
    default:break;
    }
    if(path){c.target=string_at(pid,path);const char* watched[]={"/mnt/","/host","/var/run/docker.sock","/run/docker.sock","/proc/1/root","/proc/kcore","/dev/mem","/sys/kernel"};for(auto p:watched)if(c.target.find(p)!=std::string::npos)c.boundary=true;}
    return c;
}
int main(int argc,char** argv){
    if(argc!=3){std::cerr<<"Usage: cage-monitor HOST_PID STATE_DIRECTORY\n";return 2;}
    pid_t root=0;fs::path state=argv[2];
    try{
        root=std::stoi(argv[1]);require(root>1&&geteuid()==0,"Monitor needs root and a valid workload PID");
        umask(0077);
        signal(SIGTERM,stop);signal(SIGINT,stop);
        struct sigaction sa{};sa.sa_handler=tick;sigemptyset(&sa.sa_mask);sigaction(SIGALRM,&sa,nullptr);
        itimerval timer{{1,0},{1,0}};setitimer(ITIMER_REAL,&timer,nullptr);
        const unsigned long flags=PTRACE_O_TRACEFORK|PTRACE_O_TRACEVFORK|PTRACE_O_TRACECLONE|PTRACE_O_TRACEEXEC|PTRACE_O_TRACESYSGOOD|PTRACE_O_EXITKILL;
        require(trace(PTRACE_SEIZE,root,nullptr,reinterpret_cast<void*>(flags))==0,"Cannot attach external monitor: "+std::string(strerror(errno)));
        require(trace(PTRACE_INTERRUPT,root)==0,"Cannot interrupt startup gate");
        int status=0;while(waitpid(root,&status,__WALL)<0){if(errno!=EINTR)throw std::runtime_error("Initial monitor wait failed");}
        require(WIFSTOPPED(status),"Workload exited before monitoring");
        require(trace(PTRACE_SYSCALL,root)==0,"Cannot resume monitored gate");
        write(state/"monitor.pid",std::to_string(getpid()));
        write(state/"monitor.ready",std::to_string(root));
        event(state/"events.jsonl","monitor_attached","Host PID "+std::to_string(root)+"; EXITKILL active");
        std::map<pid_t,Call> calls;
        bool enabled=true;auto refresh=std::chrono::steady_clock::time_point{};
        while(!stopping){
            auto now=std::chrono::steady_clock::now();
            if(now-refresh>std::chrono::seconds(1)){enabled=trim(read(state/"sensors"))!="off";write(state/"heartbeat",std::to_string(time(nullptr)));refresh=now;}
            pid_t pid=waitpid(-1,&status,__WALL);
            if(pid<0){if(errno==EINTR)continue;if(errno==ECHILD)break;throw std::runtime_error("Monitor wait failed");}
            if(WIFEXITED(status)||WIFSIGNALED(status)){calls.erase(pid);if(pid==root)break;continue;}
            if(!WIFSTOPPED(status))continue;
            unsigned event_code=static_cast<unsigned>(status)>>16;int sig=WSTOPSIG(status);
            if(sig==(SIGTRAP|0x80)){
                ptrace_syscall_info info{};
                require(trace(PTRACE_GET_SYSCALL_INFO,pid,reinterpret_cast<void*>(sizeof info),&info)>=0,"Cannot inspect syscall");
                if(info.op==PTRACE_SYSCALL_INFO_ENTRY){
                    if(info.arch!=AUDIT_ARCH_X86_64||(info.entry.nr&0x40000000)){event(state/"events.jsonl","unsupported_abi","Terminating process tree to preserve sensor coverage");throw std::runtime_error("Unsupported syscall architecture");}
                    if(enabled){
                        auto call=describe(pid,info);
                        if(call.name=="connect"&&!call.target.empty()){
                            const bool local=call.target.rfind("127.0.0.1:",0)==0||call.target.rfind("[::1]:",0)==0;
                            const bool proxy=call.target==trim(read(state/"proxy.ip"))+":3128"&&trim(read(state/"internet"))=="on";
                            if(!local&&!proxy)event(state/"events.jsonl","network_policy_attempt","pid="+std::to_string(pid)+" connect "+call.target+" outside configured network policy");
                        }
                        calls[pid]=std::move(call);
                    }else calls.erase(pid);
                }else if(info.op==PTRACE_SYSCALL_INFO_EXIT){
                    auto it=calls.find(pid);
                    if(enabled&&it!=calls.end()){
                        const auto& c=it->second;long result=info.exit.rval;
                        bool denied=result==-EPERM||result==-EACCES;
                        bool netfail=c.network&&(result==-ENETUNREACH||result==-EHOSTUNREACH||result==-ECONNREFUSED);
                        if(denied||netfail||c.boundary||c.sensitive){
                            std::string kind=denied?"access_denied":netfail?"network_failed":c.boundary?"boundary_probe":"privileged_operation";
                            std::string name=c.name.empty()?"syscall_"+std::to_string(c.number):c.name;
                            event(state/"events.jsonl",kind,"pid="+std::to_string(pid)+" "+name+" "+c.target+" result="+std::to_string(result));
                        }
                    }
                    calls.erase(pid);
                }
                sig=0;
            }else if(event_code){
                if(event_code==PTRACE_EVENT_EXEC){unsigned long old=0;trace(PTRACE_GETEVENTMSG,pid,nullptr,&old);if(static_cast<pid_t>(old)!=pid)calls.erase(old);calls.erase(pid);}
                sig=0;
            }else if(sig==SIGSTOP){sig=0;}
            if(trace(PTRACE_SYSCALL,pid,nullptr,reinterpret_cast<void*>(static_cast<intptr_t>(sig)))<0&&errno!=ESRCH)throw std::runtime_error("Lost monitored process");
        }
        if(stopping)kill(root,SIGKILL);
        event(state/"events.jsonl","monitor_stopped",stopping?"Workload killed with monitor":"Workload exited");
        fs::remove(state/"monitor.ready");fs::remove(state/"monitor.pid");return 0;
    }catch(const std::exception& e){
        if(root>1)kill(root,SIGKILL);
        std::error_code ec;fs::remove(state/"monitor.ready",ec);
        std::cerr<<"Monitor: "<<e.what()<<"\n";
        try{event(state/"events.jsonl","monitor_failure",e.what());}catch(...){}
        return 125;
    }
}
