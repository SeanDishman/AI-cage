#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/syscall.h>

int main(){
    int failures=0;
    auto check=[&](bool ok,const char* what){std::cout<<(ok?"PASS ":"FAIL ")<<what<<"\n";if(!ok)failures++;};
    check(getuid()==0,"root permissions inside container");
    int f=open("/workspace/containment-probe.txt",O_CREAT|O_WRONLY|O_TRUNC,0600);
    check(f>=0,"can write workspace");if(f>=0){check(write(f,"contained\n",10)==10,"workspace write completes");close(f);}
    f=open("/usr/local/share/containment-probe.txt",O_CREAT|O_WRONLY|O_TRUNC,0600);
    check(f>=0,"can install files in container system directories");if(f>=0)close(f);
    f=open("/mnt/c/Users",O_RDONLY|O_DIRECTORY);
    check(f<0,"Windows user files absent");if(f>=0)close(f);
    f=open("/var/run/docker.sock",O_RDWR);
    check(f<0,"Docker control socket absent");if(f>=0)close(f);
    check(mount("none","/mnt","tmpfs",0,nullptr)<0&&errno==EPERM,"mount denied");
    check(unshare(CLONE_NEWUSER)<0&&errno==EPERM,"new user namespace denied");
    check(syscall(SYS_io_uring_setup,1,nullptr)<0&&errno==EPERM,"async I/O bypass denied");
    int sock=socket(AF_INET,SOCK_STREAM,0);sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(443);inet_pton(AF_INET,"1.1.1.1",&a.sin_addr);
    timeval t{2,0};setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof t);
    check(connect(sock,reinterpret_cast<sockaddr*>(&a),sizeof a)<0,"direct internet socket denied (proxy required)");close(sock);
    check(getenv("AICAGE_HOST_SECRET")==nullptr,"host environment is not inherited");
    std::ifstream status("/proc/self/status");std::string all((std::istreambuf_iterator<char>(status)),{});
    check(all.find("NoNewPrivs:\t1")!=std::string::npos,"no-new-privileges active");
    check(all.find("Seccomp:\t2")!=std::string::npos,"kernel syscall filter active");
    return failures?1:0;
}
