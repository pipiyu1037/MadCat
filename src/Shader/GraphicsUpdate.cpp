#include "GraphicsUpdate.h"
#include <chrono>
#include <exception>
#include <fstream>
#include <iterator>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace madcat::shader
{
#ifdef _WIN32
namespace
{
struct Handle
{
    HANDLE value=nullptr;
    ~Handle() { if (value && value!=INVALID_HANDLE_VALUE) { CloseHandle(value); } }
};
// CreateProcess receives an argv command line directly; no shell expansion.
std::wstring quote(const std::wstring& argument)
{
    std::wstring result=L"\"";
    size_t slashes=0;
    for (const auto c : argument)
    {
        if (c==L'\\') { ++slashes; continue; }
        result.append(c==L'"' ? slashes*2+1 : slashes,L'\\');
        slashes=0; result+=c;
    }
    result.append(slashes*2,L'\\'); result+=L'"';
    return result;
}
std::expected<void,std::string> run(const GraphicsUpdateOptions& options,
    const std::filesystem::path& revision, bool compile)
{
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    Handle log{CreateFileW((revision/"update.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,
        &security,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if (log.value==INVALID_HANDLE_VALUE) { return std::unexpected("Cannot create Shader update log"); }
    Handle job{CreateJobObjectW(nullptr,nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
    { return std::unexpected("Cannot configure Shader compiler process lifetime"); }
    const std::vector<std::wstring> arguments{options.cmake.wstring(),
        L"-DREVISION="+revision.wstring(),L"-DSOURCE_ROOT="+options.sourceRoot.wstring(),
        L"-DPUBLISHED_ROOT="+options.publishedRoot.wstring(),compile ? L"-DCOMPILE=ON" : L"-DCOMPILE=OFF",
        L"-P",options.script.wstring()};
    std::wstring command;
    for (const auto& argument : arguments) { if (!command.empty()) { command+=L' '; } command+=quote(argument); }
    STARTUPINFOW startup{}; startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdOutput=startup.hStdError=log.value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(options.cmake.c_str(),command.data(),nullptr,nullptr,TRUE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup,&process))
    { return std::unexpected("Cannot start Shader update CMake process"); }
    Handle owner{process.hProcess}, thread{process.hThread};
    if (!AssignProcessToJobObject(job.value,owner.value))
    {
        TerminateProcess(owner.value,1); WaitForSingleObject(owner.value,5000);
        return std::unexpected("Cannot supervise Shader compiler process");
    }
    if (ResumeThread(thread.value)==DWORD(-1)) { return std::unexpected("Cannot resume Shader compiler process"); }
    if (WaitForSingleObject(owner.value,60000)!=WAIT_OBJECT_0)
    {
        TerminateJobObject(job.value,1); WaitForSingleObject(owner.value,5000);
        return std::unexpected("Shader update timed out (60 seconds)");
    }
    DWORD code=1;
    if (!GetExitCodeProcess(owner.value,&code) || code!=0)
    { return std::unexpected("Shader update rejected"); }
    return {};
}
}
#endif
std::expected<std::filesystem::path,std::string> prepareGraphicsUpdate(
    const GraphicsUpdateOptions& options, bool compile)
{
    try
    {
#ifdef _WIN32
        std::filesystem::create_directories(options.outputRoot);
        const auto root=std::filesystem::canonical(options.outputRoot);
        std::filesystem::path revision;
        const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
        for (unsigned attempt=0;attempt<100;++attempt)
        {
            revision=root/(std::to_string(GetCurrentProcessId())+"-"+std::to_string(stamp)+"-"+std::to_string(attempt));
            if (std::filesystem::create_directory(revision)) { break; }
            revision.clear();
        }
        if (revision.empty()) { return std::unexpected("Cannot reserve a fresh Shader revision directory"); }
        const auto result=run(options,revision,compile);
        if (!result || !std::filesystem::is_regular_file(revision/"ready"))
        {
            std::ifstream file(revision/"update.log",std::ios::binary);
            std::string diagnostic((std::istreambuf_iterator<char>(file)),{});
            if (diagnostic.size()>12000) { diagnostic=diagnostic.substr(diagnostic.size()-12000); }
            return std::unexpected((result ? "Incomplete Shader revision" : result.error())+
                "; log: "+(revision/"update.log").string()+"\n"+diagnostic);
        }
        return revision;
#else
        (void)options; (void)compile;
        return std::unexpected("Graphics Shader updates currently require Windows");
#endif
    }
    catch (const std::exception& error) { return std::unexpected(std::string("Shader update failed: ")+error.what()); }
}
}
