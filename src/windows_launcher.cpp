#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static std::wstring quote(const std::wstring& arg){
    if(!arg.empty()&&arg.find_first_of(L" \t\n\v\"")==std::wstring::npos)return arg;
    std::wstring r=L"\"";size_t slashes=0;
    for(wchar_t c:arg){if(c==L'\\'){slashes++;continue;}if(c==L'"'){r.append(slashes*2+1,L'\\');r+=c;}else{r.append(slashes,L'\\');r+=c;}slashes=0;}
    r.append(slashes*2,L'\\');return r+L'"';
}
int wmain(int argc,wchar_t** argv){
    wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);
    auto root=std::filesystem::path(path).parent_path();
    if(root.filename()==L"build"||root.filename()==L"Release"||root.filename()==L"Debug")root=root.parent_path();
    if(!std::filesystem::exists(root/L"CMakeLists.txt")&&std::filesystem::exists(root.parent_path()/L"CMakeLists.txt"))root=root.parent_path();
    auto linux=root.generic_wstring();
    if(linux.size()<3||linux[1]!=L':'){std::cerr<<"Place AICage on a local Windows drive.\n";return 2;}
    wchar_t drive=towlower(linux[0]);linux=L"/mnt/"+std::wstring(1,drive)+linux.substr(2);
    std::wstring distro=L"ShannonDocker";
    std::wifstream config(root/L"wsl-distro.txt");if(config){std::getline(config,distro);if(!distro.empty()&&distro.back()==L'\r')distro.pop_back();}
    wchar_t windows[32768];GetWindowsDirectoryW(windows,32768);
    std::wstring wsl=std::wstring(windows)+L"\\System32\\wsl.exe";
    std::vector<std::wstring> args{wsl,L"--distribution",distro,L"--user",L"root",L"--exec",linux+L"/build-linux/aicage"};
    if(argc==1)args.push_back(L"menu");else for(int i=1;i<argc;i++)args.push_back(argv[i]);
    std::wstring command;for(const auto& a:args){if(!command.empty())command+=L' ';command+=quote(a);}
    STARTUPINFOW si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};
    if(!CreateProcessW(wsl.c_str(),command.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&si,&pi)){std::cerr<<"Cannot launch WSL. Run scripts/build.ps1 first. Error "<<GetLastError()<<"\n";return 2;}
    CloseHandle(pi.hThread);WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);return static_cast<int>(code);
}
