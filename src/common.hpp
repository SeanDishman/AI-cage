#pragma once
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cage {
namespace fs = std::filesystem;
inline std::string trim(std::string s) {
    while (!s.empty() && (s.back()=='\n' || s.back()=='\r' || s.back()==' ')) s.pop_back();
    return s;
}
inline std::string read(const fs::path& p) {
    std::ifstream f(p); return {std::istreambuf_iterator<char>(f), {}};
}
inline void write(const fs::path& p, const std::string& s) {
    auto tmp=p; tmp += ".new";
    std::ofstream f(tmp, std::ios::trunc); f << s; f.close();
    if(!f) throw std::runtime_error("Cannot write " + p.string());
    fs::rename(tmp,p);
}
inline std::string json(const std::string& s) {
    std::string o="\"";
    for(unsigned char c:s) {
        if(c=='"'||c=='\\') { o+='\\';o+=static_cast<char>(c); }
        else if(c<32||c>=127) { const char* h="0123456789abcdef";o+="\\u00";o+=h[c>>4];o+=h[c&15]; }
        else o+=static_cast<char>(c);
    } return o+'"';
}
struct Result {int code;std::string out;};
inline Result process(const std::vector<std::string>& args, bool capture=true) {
    if(args.empty()) throw std::runtime_error("Empty command");
    int p[2]{-1,-1}; if(capture&&pipe2(p,O_CLOEXEC)) throw std::runtime_error("pipe failed");
    pid_t pid=fork();
    if(pid<0) throw std::runtime_error("fork failed");
    if(pid==0) {
        if(capture) {dup2(p[1],1);dup2(p[1],2);close(p[0]);close(p[1]);}
        std::vector<char*> av;for(const auto& a:args) av.push_back(const_cast<char*>(a.c_str()));av.push_back(nullptr);
        execvp(av[0],av.data()); _exit(127);
    }
    std::string out;
    if(capture) {close(p[1]);char b[4096];ssize_t n;while((n=::read(p[0],b,sizeof b))!=0){if(n<0){if(errno==EINTR)continue;break;}if(out.size()<1024*1024)out.append(b,n);}close(p[0]);}
    int st=0;while(waitpid(pid,&st,0)<0&&errno==EINTR){}
    return {WIFEXITED(st)?WEXITSTATUS(st):128+(WIFSIGNALED(st)?WTERMSIG(st):0),out};
}
inline std::string must(const std::vector<std::string>& args) {
    auto r=process(args);if(r.code)throw std::runtime_error(args[0]+" failed: "+trim(r.out));return trim(r.out);
}
inline std::string own_exe() { return fs::canonical("/proc/self/exe").string(); }
inline void require(bool b,const std::string& message){if(!b)throw std::runtime_error(message);}
inline void event(const fs::path& path,const std::string& type,const std::string& detail) {
    if(fs::exists(path)&&fs::file_size(path)>8*1024*1024) {
        std::error_code ec;fs::rename(path.string()+".1",path.string()+".2",ec);fs::rename(path,path.string()+".1",ec);
    }
    auto sec=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream f(path,std::ios::app);f<<"{\"time\":"<<sec<<",\"type\":"<<json(type)<<",\"detail\":"<<json(detail)<<"}\n";f.flush();
    if(!f)throw std::runtime_error("Cannot persist sensor log");
}
}
