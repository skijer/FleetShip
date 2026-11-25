#include "SeedJob.h"

#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {
#ifdef _WIN32
std::wstring Quote(const std::wstring& text) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t character : text) {
        if (character == L'\\') {
            ++slashes;
        } else {
            result.append(character == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
            slashes = 0;
            result += character;
        }
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
#endif
} // namespace

SeedJob::~SeedJob() {
    Cancel();
}

bool SeedJob::Running() const {
#ifdef _WIN32
    return mProcess != nullptr;
#else
    return mPid >= 0;
#endif
}

void SeedJob::Start(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                    const std::filesystem::path& log) {
    if (Running()) {
        throw std::runtime_error("A seed is already being generated.");
    }
    if (!std::filesystem::is_regular_file(executable)) {
        throw std::runtime_error("Could not find Fleet's internal generator executable.");
    }
#ifdef _WIN32
    SECURITY_ATTRIBUTES security = { sizeof(security), nullptr, TRUE };
    HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Could not open the generation log.");
    }
    std::wstring command = Quote(executable.wstring());
    for (const auto& argument : arguments) {
        command += L" " + Quote(std::filesystem::path(argument).wstring());
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job == nullptr || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(output);
        if (job != nullptr) {
            CloseHandle(job);
        }
        throw std::runtime_error("Could not prepare the generator.");
    }
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION process = {};
    const bool created =
        CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                       nullptr, executable.parent_path().c_str(), &startup, &process);
    CloseHandle(output);
    if (!created || !AssignProcessToJobObject(job, process.hProcess)) {
        if (created) {
            TerminateProcess(process.hProcess, 1);
            CloseHandle(process.hProcess);
            CloseHandle(process.hThread);
        }
        CloseHandle(job);
        throw std::runtime_error("Could not start Fleet's generator.");
    }
    mProcess = process.hProcess;
    mJob = job;
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
#else
    std::vector<std::string> owned = { executable.string() };
    owned.insert(owned.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& value : owned) {
        argv.push_back(value.data());
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    const int result = posix_spawn(&mPid, executable.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (result != 0) {
        mPid = -1;
        throw std::runtime_error("Could not start Fleet's generator.");
    }
#endif
}

std::optional<int> SeedJob::Poll() {
    if (!Running()) {
        return std::nullopt;
    }
#ifdef _WIN32
    if (WaitForSingleObject(mProcess, 0) != WAIT_OBJECT_0) {
        return std::nullopt;
    }
    DWORD result = 1;
    GetExitCodeProcess(mProcess, &result);
    CloseHandle(mProcess);
    CloseHandle(mJob);
    mProcess = mJob = nullptr;
    return static_cast<int>(result);
#else
    int status = 0;
    if (waitpid(mPid, &status, WNOHANG) != mPid) {
        return std::nullopt;
    }
    mPid = -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

void SeedJob::Cancel() {
    if (!Running()) {
        return;
    }
#ifdef _WIN32
    TerminateJobObject(mJob, 1);
    WaitForSingleObject(mProcess, 5000);
    CloseHandle(mProcess);
    CloseHandle(mJob);
    mProcess = mJob = nullptr;
#else
    kill(mPid, SIGTERM);
    waitpid(mPid, nullptr, 0);
    mPid = -1;
#endif
}
