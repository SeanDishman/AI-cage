#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

namespace localui {
std::wstring wide(const std::string& value);
std::string utf8(const std::wstring& value);
std::string random_id();
struct Result { int code; bool timed_out; bool cancelled; std::string output; };
class Process {
public:
    Process(const std::wstring& executable, const std::vector<std::string>& arguments);
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    Result read(std::chrono::seconds timeout, const std::function<void(const std::string&)>& on_line = {});
    void cancel() { cancelled_ = true; }
private:
    HANDLE process_ = nullptr, job_ = nullptr, output_ = nullptr;
    std::atomic<bool> cancelled_{false};
};
}
