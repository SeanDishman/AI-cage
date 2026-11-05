// Trusted, immutable in-container command broker. Every payload is a descendant
// of PID 1, so the host tracer sees exec, fork, clone and syscall results.
#include "common.hpp"
#include <array>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <poll.h>
#include <pty.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <termios.h>

using namespace cage;
static constexpr const char* socket_path="/run/aicage/control.sock";
static volatile sig_atomic_t released=0;
static bool transfer(int fd, void* data, size_t size, bool sending) {
    auto* p=static_cast<char*>(data);
    while(size) {ssize_t n=sending?send(fd,p,size,MSG_NOSIGNAL):recv(fd,p,size,0);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;size-=n;}return true;
}
static bool frame(int fd,char type,const void* data,uint32_t size) {
    if(!transfer(fd,&type,1,true)||!transfer(fd,&size,4,true))return false;
    return size==0||transfer(fd,const_cast<void*>(data),size,true);
}
static bool receive(int fd,char& type,std::string& data) {
    uint32_t size=0;if(!transfer(fd,&type,1,false)||!transfer(fd,&size,4,false)||size>1024*1024)return false;
    data.resize(size);return size==0||transfer(fd,data.data(),size,false);
}
static void guard_syscalls() {
    std::vector<sock_filter> f;
    f.push_back(BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,arch)));
    f.push_back(BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_X86_64,1,0));
    f.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_KILL_PROCESS));
    f.push_back(BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,nr)));
    f.push_back(BPF_JUMP(BPF_JMP|BPF_JSET|BPF_K,0x40000000,0,1));
    f.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_KILL_PROCESS));
    // Asynchronous I/O would hide file/network operations from syscall sensors.
    const int denied[]={SYS_mount,SYS_umount2,SYS_pivot_root,SYS_ptrace,SYS_bpf,
        SYS_perf_event_open,SYS_userfaultfd,SYS_keyctl,SYS_add_key,SYS_request_key,
        SYS_open_by_handle_at,SYS_setns,SYS_unshare,SYS_reboot,SYS_kexec_load,
        SYS_init_module,SYS_finit_module,SYS_delete_module,
        SYS_process_vm_readv,SYS_process_vm_writev,SYS_io_uring_setup,
        SYS_io_uring_enter,SYS_io_uring_register};
    for(int n:denied){f.push_back(BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,static_cast<unsigned>(n),0,1));f.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM));}
    f.push_back(BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_clone3,0,1));
    f.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|ENOSYS));
    f.push_back(BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_clone,0,3));
    f.push_back(BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args[0])));
    f.push_back(BPF_JUMP(BPF_JMP|BPF_JSET|BPF_K,CLONE_UNTRACED|CLONE_NEWCGROUP|CLONE_NEWIPC|CLONE_NEWNET|CLONE_NEWNS|CLONE_NEWPID|CLONE_NEWUSER|CLONE_NEWUTS,0,1));
    f.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM));
    f.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW));
    sock_fprog program{static_cast<unsigned short>(f.size()),f.data()};
    require(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)==0,"no_new_privs failed");
    require(prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&program)==0,"seccomp failed");
}
static int raw_session(int peer,const std::vector<std::string>& args){
    int in[2],out[2],err[2];
    require(pipe2(in,O_CLOEXEC)==0&&pipe2(out,O_CLOEXEC)==0&&pipe2(err,O_CLOEXEC)==0,"Command pipes failed");
    pid_t child=fork();require(child>=0,"Command fork failed");
    if(child==0){
        signal(SIGPIPE,SIG_DFL);
        setsid();dup2(in[0],0);dup2(out[1],1);dup2(err[1],2);
        for(int fd:{in[0],in[1],out[0],out[1],err[0],err[1],peer})close(fd);
        if(chdir("/workspace")<0)_exit(125);
        std::vector<char*> av;for(const auto& a:args)av.push_back(const_cast<char*>(a.c_str()));av.push_back(nullptr);
        execvp(av[0],av.data());std::cerr<<"Cannot execute "<<args[0]<<": "<<strerror(errno)<<"\n";_exit(127);
    }
    close(in[0]);close(out[1]);close(err[1]);
    fcntl(in[1],F_SETFL,fcntl(in[1],F_GETFL)|O_NONBLOCK);
    std::string pending;bool eof=false;bool output=true;bool errors=true;bool alive=true;bool exited=false;int status=0;
    std::array<char,16384> buffer{};
    while(output||errors||!exited){
        pollfd fds[4]{{out[0],static_cast<short>(output?POLLIN:0),0},{err[0],static_cast<short>(errors?POLLIN:0),0},{peer,static_cast<short>(pending.size()<65536?POLLIN:0),0},{in[1],static_cast<short>(!pending.empty()?POLLOUT:0),0}};
        if(poll(fds,4,250)<0){if(errno==EINTR)continue;alive=false;break;}
        if(!exited&&waitpid(child,&status,WNOHANG)==child)exited=true;
        for(int i=0;i<2;i++){
            bool& open=i==0?output:errors;
            if(open&&(fds[i].revents&(POLLIN|POLLHUP|POLLERR))){
                auto n=::read(fds[i].fd,buffer.data(),buffer.size());
                if(n<=0){open=false;close(fds[i].fd);if(i==0)out[0]=-1;else err[0]=-1;}
                else if(!frame(peer,i==0?'O':'R',buffer.data(),static_cast<uint32_t>(n))){alive=false;break;}
            }
        }
        if(!alive)break;
        if(fds[2].revents&(POLLIN|POLLHUP|POLLERR)){
            char type=0;std::string data;
            if(!receive(peer,type,data)){alive=false;break;}
            if(type=='I'&&!eof){pending+=data;require(pending.size()<=1024*1024,"Input buffer exceeded");}
            else if(type=='F')eof=true;
        }
        if(in[1]>=0&&!pending.empty()&&(fds[3].revents&(POLLOUT|POLLERR|POLLHUP))){
            auto n=::write(in[1],pending.data(),pending.size());
            if(n>0)pending.erase(0,static_cast<size_t>(n));else if(n<0&&errno!=EAGAIN&&errno!=EINTR){pending.clear();close(in[1]);in[1]=-1;}
        }
        if(eof&&pending.empty()&&in[1]>=0){close(in[1]);in[1]=-1;}
    }
    for(int fd:{in[1],out[0],err[0]})if(fd>=0)close(fd);
    if(!alive)kill(-child,SIGHUP);
    if(!exited)while(waitpid(child,&status,0)<0&&errno==EINTR){}
    int code=WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);
    frame(peer,'X',&code,sizeof code);return 0;
}
static int session(int peer) {
    signal(SIGCHLD,SIG_DFL);
    char type=0;std::string msg;
    if(!receive(peer,type,msg))return 1;
    if(type=='P'){int code=0;frame(peer,'X',&code,sizeof code);return 0;}
    if(type!='A'&&type!='T')return 1;
    const bool terminal_requested=type=='T';
    std::vector<std::string> args;
    size_t at=0;
    while(at<msg.size()){auto end=msg.find('\0',at);if(end==std::string::npos||args.size()>255)return 1;args.push_back(msg.substr(at,end-at));at=end+1;}
    if(args.empty()||args[0].empty())return 1;
    if(!terminal_requested)return raw_session(peer,args);
    winsize ws{30,120,0,0};int terminal=-1;
    pid_t child=forkpty(&terminal,nullptr,nullptr,&ws);
    if(child<0)return 1;
    if(child==0) {
        signal(SIGPIPE,SIG_DFL);
        close(peer);if(chdir("/workspace")<0)_exit(125);
        std::vector<char*> av;for(auto& a:args)av.push_back(a.data());av.push_back(nullptr);
        execvp(av[0],av.data());std::cerr<<"Cannot execute "<<args[0]<<": "<<strerror(errno)<<"\n";_exit(127);
    }
    bool input=true;std::array<char,16384> buffer{};
    for(;;) {
        pollfd fds[2]{{terminal,POLLIN,0},{peer,static_cast<short>(input?POLLIN:0),0}};
        if(poll(fds,2,-1)<0){if(errno==EINTR)continue;break;}
        if(input&&(fds[1].revents&(POLLIN|POLLHUP|POLLERR))) {
            if(!receive(peer,type,msg)){kill(-child,SIGHUP);break;}
            else if(type=='I'){size_t n=0;while(n<msg.size()){auto r=::write(terminal,msg.data()+n,msg.size()-n);if(r<0&&errno==EINTR)continue;if(r<=0)break;n+=r;}}
            else if(type=='F'){char eof=4;auto written=::write(terminal,&eof,1);(void)written;}
            else if(type=='W'&&msg.size()==sizeof(winsize)){memcpy(&ws,msg.data(),sizeof ws);ioctl(terminal,TIOCSWINSZ,&ws);}
        }
        if(fds[0].revents&(POLLIN|POLLHUP|POLLERR)) {
            ssize_t n=::read(terminal,buffer.data(),buffer.size());
            if(n<=0)break;
            if(!frame(peer,'O',buffer.data(),static_cast<uint32_t>(n))){kill(-child,SIGHUP);break;}
        }
    }
    close(terminal);int status=0;while(waitpid(child,&status,0)<0&&errno==EINTR){}
    int code=WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);
    frame(peer,'X',&code,sizeof code);return 0;
}
static int serve() {
    require(getpid()==1,"Broker must be container PID 1");
    sigset_t set;sigemptyset(&set);sigaddset(&set,SIGUSR1);sigprocmask(SIG_BLOCK,&set,nullptr);
    signal(SIGUSR1,[](int){released=1;});
    std::cout<<"AICAGE_GATE_READY\n"<<std::flush;
    sigset_t waiting;sigemptyset(&waiting);while(!released)sigsuspend(&waiting);
    sigprocmask(SIG_UNBLOCK,&set,nullptr);
    guard_syscalls();
    signal(SIGPIPE,SIG_IGN);
    // A copied Windows auth cache may have broad source mode bits.
    chmod("/root/.codex",0700);chmod("/root/.codex/auth.json",0600);
    mkdir("/run/aicage",0700);unlink(socket_path);
    int s=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);require(s>=0,"socket failed");
    sockaddr_un address{};address.sun_family=AF_UNIX;strcpy(address.sun_path,socket_path);
    require(bind(s,reinterpret_cast<sockaddr*>(&address),sizeof address)==0,"bind failed");
    chmod(socket_path,0600);require(listen(s,16)==0,"listen failed");
    signal(SIGCHLD,SIG_IGN);signal(SIGTERM,[](int){_exit(0);});
    std::cout<<"AICAGE_BROKER_READY\n"<<std::flush;
    for(;;){int p=accept4(s,nullptr,nullptr,SOCK_CLOEXEC);if(p<0){if(errno==EINTR)continue;return 1;}pid_t child=fork();if(child==0){close(s);int code=session(p);close(p);_exit(code);}close(p);}
}
static int client(int argc,char** argv) {
    int s=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);require(s>=0,"socket failed");
    sockaddr_un a{};a.sun_family=AF_UNIX;strcpy(a.sun_path,socket_path);
    require(connect(s,reinterpret_cast<sockaddr*>(&a),sizeof a)==0,"Sandbox broker is not ready");
    if(argc==0){char type=0;std::string data;require(frame(s,'P',nullptr,0)&&receive(s,type,data)&&type=='X',"Broker ping failed");close(s);return 0;}
    std::string args;for(int i=0;i<argc;i++){args+=argv[i];args+='\0';}
    bool tty=isatty(STDIN_FILENO);termios saved{};winsize ws{};
    require(args.size()<1024*1024,"Command too large");
    std::string outgoing,incoming;
    auto queue=[&](char type,const void* data,uint32_t size){outgoing+=type;outgoing.append(reinterpret_cast<const char*>(&size),4);if(size)outgoing.append(static_cast<const char*>(data),size);};
    queue(tty?'T':'A',args.data(),static_cast<uint32_t>(args.size()));
    if(tty){tcgetattr(0,&saved);termios raw=saved;cfmakeraw(&raw);tcsetattr(0,TCSANOW,&raw);if(ioctl(0,TIOCGWINSZ,&ws)==0)queue('W',&ws,sizeof ws);}
    struct Restore{bool tty;termios saved;~Restore(){if(tty)tcsetattr(0,TCSANOW,&saved);}} restore{tty,saved};
    fcntl(s,F_SETFL,fcntl(s,F_GETFL)|O_NONBLOCK);
    bool input=true;std::array<char,8192> buf{};
    for(;;){
        pollfd fds[2]{{s,static_cast<short>(POLLIN|(outgoing.empty()?0:POLLOUT)),0},{0,static_cast<short>(input&&outgoing.size()<65536?POLLIN:0),0}};
        if(poll(fds,2,250)<0){if(errno==EINTR)continue;break;}
        if(tty){winsize now{};if(ioctl(0,TIOCGWINSZ,&now)==0&&memcmp(&now,&ws,sizeof ws)!=0){ws=now;queue('W',&ws,sizeof ws);}}
        if((fds[0].revents&POLLOUT)&&!outgoing.empty()){auto n=send(s,outgoing.data(),outgoing.size(),MSG_NOSIGNAL);if(n>0)outgoing.erase(0,static_cast<size_t>(n));else if(n<0&&errno!=EAGAIN&&errno!=EINTR)break;}
        if(fds[0].revents&(POLLIN|POLLHUP|POLLERR)){
            auto n=recv(s,buf.data(),buf.size(),0);if(n==0)break;if(n<0){if(errno==EAGAIN||errno==EINTR)continue;break;}incoming.append(buf.data(),static_cast<size_t>(n));
            while(incoming.size()>=5){
                char type=incoming[0];uint32_t size=0;memcpy(&size,incoming.data()+1,4);require(size<=1024*1024,"Invalid broker frame");if(incoming.size()<5+size)break;
                if(type=='O'||type=='R'){size_t at=0;while(at<size){auto written=::write(type=='O'?1:2,incoming.data()+5+at,size-at);if(written<0&&errno==EINTR)continue;if(written<=0)break;at+=static_cast<size_t>(written);}}
                else if(type=='X'&&size==sizeof(int)){int code=0;memcpy(&code,incoming.data()+5,sizeof code);close(s);return code;}
                incoming.erase(0,5+size);
            }
        }
        if(input&&outgoing.size()<65536&&(fds[1].revents&(POLLIN|POLLHUP|POLLERR))){auto n=::read(0,buf.data(),buf.size());if(n>0)queue('I',buf.data(),static_cast<uint32_t>(n));else{input=false;queue('F',nullptr,0);}}
    }
    close(s);return 125;
}
int main(int argc,char** argv){try{if(argc==2&&std::string(argv[1])=="serve")return serve();if(argc==2&&std::string(argv[1])=="ping")return client(0,nullptr);if(argc>2&&std::string(argv[1])=="client")return client(argc-2,argv+2);throw std::runtime_error("Usage: broker serve | ping | client COMMAND [ARGS]");}catch(const std::exception& e){std::cerr<<"Broker: "<<e.what()<<"\n";return 125;}}
