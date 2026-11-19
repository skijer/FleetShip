#include "SeedJob.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--child") {
        return argc == 5 && std::string(argv[2]) == "folder with spaces\\" &&
                       std::string(argv[3]) == "literal \"quotes\"" && std::string(argv[4]) == ""
                   ? 0
                   : 12;
    }
    if (argc > 1 && std::string(argv[1]) == "--wait") {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        return 0;
    }
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("fleet job test " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    try {
        SeedJob job;
        const auto executable = std::filesystem::absolute(argv[0]);
        job.Start(executable, { "--child", "folder with spaces\\", "literal \"quotes\"", "" },
                  directory / "result.log");
        std::optional<int> result;
        for (int i = 0; i < 500 && !(result = job.Poll()); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!result || *result != 0 || job.Running()) {
            throw std::runtime_error("worker arguments or completion failed");
        }
        job.Start(executable, { "--wait" }, directory / "cancel.log");
        job.Cancel();
        if (job.Running() || job.Poll()) {
            throw std::runtime_error("cancelled worker was retained");
        }
        bool rejected = false;
        try {
            job.Start(directory / "missing.exe", {}, directory / "missing.log");
        } catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) {
            throw std::runtime_error("missing generator was accepted");
        }
        std::filesystem::remove_all(directory);
        std::cout << "Fleet seed worker tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(directory);
        return 1;
    }
}
