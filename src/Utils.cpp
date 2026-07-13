#include "stdafx.hpp"
#include "Utils.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

void Utils::CreateLogger()
{
    auto rootDir = GetRootDir();
    auto red4extDir = rootDir / L"red4ext";
    auto logsDir = red4extDir / L"logs";
    spdlog::filename_t logFile = (logsDir / L"input_loader.log");

    // Ensure the logs directory exists - basic_file_sink_mt will throw if it
    // doesn't, and we're called from DllMain where a throw fails the DLL
    // load entirely. RED4ext normally creates this, but be defensive.
    try
    {
        if (!std::filesystem::exists(logsDir))
        {
            std::filesystem::create_directories(logsDir);
        }
    }
    catch (const std::exception& ex)
    {
        // Can't log yet - the logger isn't constructed. Output to the
        // debugger as a last resort.
        OutputDebugStringA("InputLoader: failed to create logs directory: ");
        OutputDebugStringA(ex.what());
        OutputDebugStringA("\n");
    }
    catch (...)
    {
        OutputDebugStringA("InputLoader: unknown error creating logs directory\n");
    }

    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);

    spdlog::sinks_init_list sinks = {console, file};

    auto logger = std::make_shared<spdlog::logger>("", sinks);
    spdlog::set_default_logger(logger);

    logger->flush_on(spdlog::level::trace);
    spdlog::set_level(spdlog::level::trace);
}

std::filesystem::path Utils::GetRootDir()
{
    constexpr auto pathLength = MAX_PATH + 1;

    // Try to get the executable path until we can fit the length of the path.
    std::wstring filename;
    do
    {
        filename.resize(filename.size() + pathLength, '\0');

        auto length = GetModuleFileName(nullptr, filename.data(), static_cast<uint32_t>(filename.size()));
        if (length > 0)
        {
            // Resize it to the real, std::filesystem::path" will use the string's length instead of recounting it.
            filename.resize(length);
        }
    } while (GetLastError() == ERROR_INSUFFICIENT_BUFFER);

    if (filename.empty())
    {
        // GetModuleFileName failed before we ever got a usable path.
        // Returning an empty path would lead to confusing errors downstream
        // (every path operation would resolve relative to the CWD). Surface
        // the failure so the user can act on it.
        OutputDebugStringA("InputLoader: GetModuleFileName failed - returning empty root dir\n");
        return std::filesystem::path();
    }

    auto rootDir = std::filesystem::path(filename)
                       .parent_path()  // Resolve to "x64" directory.
                       .parent_path()  // Resolve to "bin" directory.
                       .parent_path(); // Resolve to game root directory.

    return rootDir;
}

std::wstring Utils::ToWString(const char* aText)
{
    if (!aText)
    {
        return std::wstring();
    }

    auto length = strlen(aText);
    if (length == 0)
    {
        return std::wstring();
    }

    std::wstring result(L"", length);
    // mbstowcs returns (size_t)-1 on invalid multi-byte sequences. Use
    // MultiByteToWideChar which is more forgiving on Windows and lets us
    // detect conversion failures explicitly.
    int converted = MultiByteToWideChar(CP_ACP, 0, aText,
                                        static_cast<int>(length),
                                        result.data(),
                                        static_cast<int>(length));
    if (converted <= 0)
    {
        // Fallback: mbstowcs with the original buffer (best-effort).
        mbstowcs(result.data(), aText, length);
    }

    return result;
}
