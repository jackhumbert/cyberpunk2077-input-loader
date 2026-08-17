#include "stdafx.hpp"
#include "Utils.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// Max number of path-resize iterations before we give up. Guard against
// pathological loops where GetModuleFileName keeps returning truncation.
constexpr size_t kMaxPathResizeIterations = 16;

void Utils::CreateLogger()
{
    auto rootDir = GetRootDir();
    auto red4extDir = rootDir / L"red4ext";
    auto logsDir = red4extDir / L"logs";
    auto logFilePath = logsDir / L"input_loader.log";
    spdlog::filename_t logFile = logFilePath;  // implicit conversion to wstring on Windows

    // Set up a console-only logger first. This must NEVER throw - it is called
    // from DllMain, where an uncaught exception fails the entire DLL load.
    // If the file sink fails later, we still have a working logger.
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list{console});
    spdlog::set_default_logger(logger);
    logger->flush_on(spdlog::level::trace);
    spdlog::set_level(spdlog::level::trace);

    // Now try to add the file sink. The directory is created first because
    // basic_file_sink_mt will throw if it doesn't exist; either way, wrap
    // everything in try/catch so we keep the console-only logger on failure.
    bool fileSinkOk = false;
    try
    {
        if (!std::filesystem::exists(logsDir))
        {
            std::filesystem::create_directories(logsDir);
        }
        auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
        logger->sinks().push_back(file);
        fileSinkOk = true;
    }
    catch (const std::exception& ex)
    {
        // Logger exists now, so we can use it - but the file sink failed.
        // Also echo to the debugger in case spdlog itself is broken.
        spdlog::error("InputLoader: failed to create log file sink at '{}': {}",
                      logFilePath.string(), ex.what());
        OutputDebugStringA("InputLoader: failed to create log file sink: ");
        OutputDebugStringA(ex.what());
        OutputDebugStringA("\n");
    }
    catch (...)
    {
        spdlog::error("InputLoader: unknown error creating log file sink at '{}'",
                      logFilePath.string());
        OutputDebugStringA("InputLoader: unknown error creating log file sink\n");
    }

    if (fileSinkOk)
    {
        spdlog::info("InputLoader logger initialized (console + file: {})",
                     logFilePath.string());
    }
    else
    {
        spdlog::warn("InputLoader logger initialized (console only; file sink failed)");
    }
}

std::filesystem::path Utils::GetRootDir()
{
    constexpr auto pathLength = MAX_PATH + 1;

    // Try to get the executable path until we can fit the length of the path.
    //
    // GetModuleFileName does NOT clear the last error on success, so a stale
    // ERROR_INSUFFICIENT_BUFFER from a previous iteration could persist and
    // make us loop forever even after a successful call. Reset the last error
    // before each call and rely on the return value (== buffer size - 1 on
    // truncation) plus a fresh last-error check to decide whether to grow.
    std::wstring filename;
    for (size_t iter = 0; iter < kMaxPathResizeIterations; ++iter)
    {
        filename.resize(filename.size() + pathLength, L'\0');

        ::SetLastError(ERROR_SUCCESS);
        auto length = GetModuleFileName(nullptr, filename.data(),
                                        static_cast<uint32_t>(filename.size()));

        if (length == 0)
        {
            // Genuine failure - capture the error code immediately so it
            // isn't clobbered by subsequent calls.
            const auto err = ::GetLastError();
            OutputDebugStringA("InputLoader: GetModuleFileName failed (errno=");
            char buf[32] = {0};
            _ultoa(err, buf, 16);
            OutputDebugStringA(buf);
            OutputDebugStringA(") - returning empty root dir\n");
            return std::filesystem::path();
        }

        // GetModuleFileName returns the number of characters written, NOT
        // including the NUL. If `length == bufferSize - 1`, the path may have
        // been truncated; GetLastError() == ERROR_INSUFFICIENT_BUFFER confirms
        // it. If length < bufferSize - 1, the path fit and we're done.
        if (length < filename.size() - 1)
        {
            filename.resize(length);
            break;
        }

        // Buffer was exactly full - could be truncation. Confirm via last
        // error before growing.
        const auto err = ::GetLastError();
        if (err != ERROR_INSUFFICIENT_BUFFER)
        {
            // Not a truncation - the path happens to be exactly bufferSize-1
            // chars. We're done.
            filename.resize(length);
            break;
        }

        // Truncation confirmed - loop and grow the buffer.
    }

    if (filename.empty())
    {
        OutputDebugStringA("InputLoader: GetModuleFileName exhausted resize iterations - returning empty root dir\n");
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

    // Two-pass MultiByteToWideChar: first call asks for the required length,
    // second call does the actual conversion. This avoids the UB of
    // `std::wstring(L"", length)` (which copies `length` chars from a
    // 1-character literal) and avoids leaving embedded NULs when fewer
    // characters are produced than the input length.
    const int inputLen = static_cast<int>(length);

    int required = MultiByteToWideChar(CP_ACP, 0, aText, inputLen, nullptr, 0);
    if (required <= 0)
    {
        // Conversion failed (e.g., invalid multi-byte sequence). Fall back to
        // mbstowcs with a properly-sized buffer; mbstowcs stops at the input
        // NUL terminator and writes at most `length` wide chars.
        std::wstring fallback(length, L'\0');
        mbstowcs(fallback.data(), aText, length);
        // Trim at the first NUL in case mbstowcs produced fewer chars.
        const auto nulPos = fallback.find(L'\0');
        if (nulPos != std::wstring::npos)
        {
            fallback.resize(nulPos);
        }
        return fallback;
    }

    std::wstring result(required, L'\0');
    int converted = MultiByteToWideChar(CP_ACP, 0, aText, inputLen,
                                        result.data(), required);
    if (converted <= 0)
    {
        // Shouldn't happen since the sizing call succeeded, but be safe.
        std::wstring fallback(length, L'\0');
        mbstowcs(fallback.data(), aText, length);
        const auto nulPos = fallback.find(L'\0');
        if (nulPos != std::wstring::npos)
        {
            fallback.resize(nulPos);
        }
        return fallback;
    }

    // `converted` may be less than `required` in rare cases; resize to actual.
    result.resize(converted);
    return result;
}
