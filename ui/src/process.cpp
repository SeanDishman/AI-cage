#include "process.hpp"
#include <bcrypt.h>
#include <array>
#include <stdexcept>
#include <thread>

namespace localui {
namespace {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if(value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    HANDLE release() { auto h=value; value=nullptr; return h; }
};
void check(bool ok, const char* operation) {
    if(!ok) throw std::runtime_error(std::string(operation)+" (Windows error "+std::to_string(GetLastError())+")");
}
std::wstring quote(const std::wstring& argument) {
    std::wstring out=L"\""; size_t slashes=0;
    for(wchar_t c:argument) {
        if(c==L'\\') { ++slashes; continue; }
        if(c==L'"') out.append(slashes*2+1,L'\\'); else out.append(slashes,L'\\');
        out+=c; slashes=0;
    }
    out.append(slashes*2,L'\\'); return out+L'"';
}
}
std::wstring wide(const std::string& value) {
    if(value.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    check(n>0,"Invalid UTF-8"); std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n);
    return out;
}
std::string utf8(const std::wstring& value) {
    if(value.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    check(n>0,"Invalid UTF-16"); std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr);
    return out;
}
std::string random_id() {
    std::array<unsigned char,32> data{};
    if(BCryptGenRandom(nullptr,data.data(),static_cast<ULONG>(data.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
        throw std::runtime_error("Cannot obtain secure local session token");
    const char* hex="0123456789abcdef"; std::string out;
    for(auto b:data) { out+=hex[b>>4]; out+=hex[b&15]; } return out;
}
Process::Process(const std::wstring& executable,const std::vector<std::string>& arguments) {
    // No shell: neither prompts nor model names become host commands.
    std::wstring command=quote(executable);
    for(const auto& a:arguments) { command+=L' '; command+=quote(wide(a)); }
    if(command.size()>30000) throw std::runtime_error("Message is too long for the Windows launcher");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    Handle read_pipe,write_pipe,input,job;
    check(CreatePipe(&read_pipe.value,&write_pipe.value,&security,0),"Create output pipe");
    check(SetHandleInformation(read_pipe.value,HANDLE_FLAG_INHERIT,0),"Protect output pipe");
    input.value=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    check(input.value!=INVALID_HANDLE_VALUE,"Open child input");
    job.value=CreateJobObjectW(nullptr,nullptr); check(job.value!=nullptr,"Create process group");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    check(SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof limits),"Protect process group");
    SIZE_T size=0; InitializeProcThreadAttributeList(nullptr,1,0,&size);
    std::vector<unsigned char> memory(size);
    auto attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(memory.data());
    check(InitializeProcThreadAttributeList(attributes,1,0,&size),"Create handle allowlist");
    struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Cleanup(){DeleteProcThreadAttributeList(p);} } cleanup{attributes};
    HANDLE handles[]{input.value,write_pipe.value};
    check(UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof handles,nullptr,nullptr),"Restrict inherited handles");
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb=sizeof startup;
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=input.value;
    startup.StartupInfo.hStdOutput=write_pipe.value;
    startup.StartupInfo.hStdError=write_pipe.value; startup.lpAttributeList=attributes;
    PROCESS_INFORMATION info{};
    check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
                        CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&info),"Launch sandbox controller");
    Handle process{info.hProcess}, thread{info.hThread};
    if(!AssignProcessToJobObject(job.value,process.value)) {
        TerminateProcess(process.value,125); throw std::runtime_error("Cannot contain controller process group");
    }
    if(ResumeThread(thread.value)==static_cast<DWORD>(-1)) {
        TerminateJobObject(job.value,125); throw std::runtime_error("Cannot resume controller process");
    }
    process_=process.release(); job_=job.release(); output_=read_pipe.release();
}
Process::~Process() {
    if(job_) CloseHandle(job_);
    if(process_) CloseHandle(process_);
    if(output_) CloseHandle(output_);
}
Result Process::read(std::chrono::seconds timeout,const std::function<void(const std::string&)>& on_line) {
    auto deadline=std::chrono::steady_clock::now()+timeout;
    Result result{125,false,false,{}}; std::string pending; size_t total=0;
    std::array<char,8192> buffer{}; bool terminated=false;
    for(;;) {
        if(!terminated&&(cancelled_||std::chrono::steady_clock::now()>deadline)) {
            result.cancelled=cancelled_.load(); result.timed_out=!result.cancelled;
            TerminateJobObject(job_,124); terminated=true;
        }
        DWORD available=0;
        if(PeekNamedPipe(output_,nullptr,0,nullptr,&available,nullptr)&&available) {
            DWORD got=0; if(!ReadFile(output_,buffer.data(),static_cast<DWORD>(std::min<size_t>(available,buffer.size())),&got,nullptr)) break;
            total+=got;
            // Both transcript lines and total output are bounded, even for hostile tool output.
            if(total>32*1024*1024 || pending.size()+got>2*1024*1024) {
                TerminateJobObject(job_,125); throw std::runtime_error("Agent output exceeded the local UI limit; use shorter command output");
            }
            if(on_line) {
                pending.append(buffer.data(),got); size_t pos;
                while((pos=pending.find('\n'))!=std::string::npos) {
                    auto line=pending.substr(0,pos); if(!line.empty()&&line.back()=='\r') line.pop_back();
                    pending.erase(0,pos+1); on_line(line);
                }
            } else if(result.output.size()<1024*1024) result.output.append(buffer.data(),got);
            continue;
        }
        if(WaitForSingleObject(process_,0)==WAIT_OBJECT_0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    if(on_line&&!pending.empty()) on_line(pending);
    WaitForSingleObject(process_,5000); DWORD code=125; GetExitCodeProcess(process_,&code); result.code=static_cast<int>(code);
    return result;
}
}
