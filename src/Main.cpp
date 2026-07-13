#include <algorithm>
#include <string>
#include <vector>
#include <mutex>

#include <RED4ext/RED4ext.hpp>

#include <pugixml.hpp>

#include "Utils.hpp"
#include "stdafx.hpp"
#include <InputLoader.hpp>

namespace InputLoader {

// Forward declaration so the default argument lives in a declaration (good
// practice) instead of only in the definition. The signature is unchanged for
// ABI/Source compatibility.
pugi::xml_document LoadDocument(std::filesystem::path path,
                                bool *status = nullptr);

pugi::xml_document LoadDocument(std::filesystem::path path,
                                bool *status) {

  pugi::xml_document document;
  if (path.is_relative()) {
    path = Utils::GetRootDir() / path;
  }
  std::string documentPath = (path).string();
  pugi::xml_parse_result result = document.load_file(documentPath.c_str());

  if (!result) {
    spdlog::error("XML document parsed with errors: {}", documentPath);
    spdlog::error("Error description: {}", result.description());
    spdlog::error("Error offset: {}", result.offset);
    if (status)
      *status = false;
    return document;
  }

  // spdlog::info("Loaded document: {}", documentPath);

  if (status)
    *status = true;
  return document;
}

// https://stackoverflow.com/questions/20303821/how-to-check-if-string-is-in-array-of-strings
bool in_array(const std::string &value, const std::vector<std::string> &array) {
  return std::find(array.begin(), array.end(), value) != array.end();
}

std::vector<std::string> valid_inputUserMappings = {
  "mapping", 
  "buttonGroup",
  "pairedAxes", 
  "preset"
};

std::vector<std::string> valid_inputContexts = {
  "blend",  
  "context", 
  "hold",          
  "multitap",
  "repeat", 
  "toggle",  
  "acceptedEvents"
};

pugi::xml_document inputContextsOriginal;
pugi::xml_document inputUserMappingsOriginal;

// Mutex protecting `document_paths` - the public Add() entrypoint may be
// invoked concurrently by different RED4ext plugins from their own threads.
static std::mutex document_paths_mutex;
static std::vector<std::filesystem::path> document_paths;

RED4EXT_C_EXPORT void Add(RED4ext::PluginHandle aHandle, const wchar_t * str) {
  // Defensive: a misbehaving plugin may pass nullptr.
  if (!str) {
    spdlog::error("InputLoader::Add() called with a null path; ignoring.");
    return;
  }

  std::filesystem::path path(str);
  if (path.is_relative()) {
    // Use the wide-character API to preserve Unicode paths and avoid the
    // 512-byte truncation bug the previous ANSI buffer suffered from.
    // We dynamically resize, mirroring the approach in Utils::GetRootDir().
    std::wstring dllFilePath;
    constexpr size_t pathChunk = MAX_PATH + 1;
    do {
      dllFilePath.resize(dllFilePath.size() + pathChunk, L'\0');
      auto length = GetModuleFileNameW(aHandle, dllFilePath.data(),
                                       static_cast<uint32_t>(dllFilePath.size()));
      if (length > 0) {
        dllFilePath.resize(length);
      }
    } while (GetLastError() == ERROR_INSUFFICIENT_BUFFER);

    if (dllFilePath.empty()) {
      spdlog::error("InputLoader::Add() - GetModuleFileNameW failed (errno={:#x}); ignoring path '{}'",
                    GetLastError(), path.string());
      return;
    }

    std::filesystem::path dllPath(dllFilePath);
    path = dllPath.parent_path() / path;
  }
  spdlog::info(L"Will load document: {}", path.c_str());

  std::lock_guard<std::mutex> guard(document_paths_mutex);
  document_paths.emplace_back(path);
}

void MergeDocument(std::filesystem::path path) {
  // inputUserMappings.xml bindings children:
  // * mapping
  // * buttonGroup
  // * pairedAxes
  // * preset

  // inputContexts.xml bindings children:
  // * blend
  // * context
  // * hold
  // * multitap
  // * repeat
  // * toggle
  // * acceptedEvents

  // uiInputActions.xml input_actions children:
  // * filter

  // inputDeadzones.xml deadzones children:
  // * radialDeadzone
  // * angularDeadzone

  // pugi::xml_document modDocument =
  // LoadDocument("r6/input/flight_control.xml");
  bool loadOk = true;
  pugi::xml_document modDocument = LoadDocument(path, &loadOk);
  spdlog::info(L"Loading document: {}", path.c_str());

  if (!loadOk) {
    // LoadDocument already logged the parse error details; just bail out so
    // we don't silently produce a partial merge.
    spdlog::error(L"Skipping merge for '{}' due to parse/load errors", path.c_str());
    return;
  }

  pugi::xml_node modBindings = modDocument.child("bindings");
  if (!modBindings) {
    spdlog::warn(L"Document '{}' has no <bindings> root; nothing to merge", path.c_str());
    return;
  }

  // process bindings
  for (pugi::xml_node modNode : modBindings.children()) {
    spdlog::info("* Processing mod input block: {}", modNode.name());
    pugi::xml_node existing;
    pugi::xml_document *document;
    if (in_array(modNode.name(), valid_inputContexts)) {
      existing =
          inputContextsOriginal.child("bindings")
              .find_child_by_attribute(modNode.name(), "name",
                                       modNode.attribute("name").as_string());
      document = &inputContextsOriginal;
    } else if (in_array(modNode.name(), valid_inputUserMappings)) {
      existing =
          inputUserMappingsOriginal.child("bindings")
              .find_child_by_attribute(modNode.name(), "name",
                                       modNode.attribute("name").as_string());
      document = &inputUserMappingsOriginal;
    } else {
      spdlog::warn("* <bindings> child '{}' not valid", modNode.name());
      continue;
    }

    pugi::xml_node targetBindings = document->child("bindings");
    if (!targetBindings) {
      // Original document is malformed (no <bindings> root). Recreate it so
      // we don't lose the mod's data, and warn loudly.
      spdlog::warn("Target document had no <bindings> root; creating one.");
      targetBindings = document->append_child("bindings");
    }

    if (existing) {
      if (modNode.attribute("append").as_bool()) {
        for (pugi::xml_node modNodeChild : modNode.children()) {
          existing.append_copy(modNodeChild);
        }
      } else {
        targetBindings.remove_child(existing);
        targetBindings.append_copy(modNode);
      }
    } else {
      targetBindings.append_copy(modNode);
    }
  }
}

void LoadOriginals() {
  spdlog::info("Loading original input configs for merging");

  bool contextsOk = true;
  inputContextsOriginal =
      LoadDocument("r6/config/inputContexts.xml", &contextsOk);
  if (!contextsOk) {
    // No bundled backup exists for inputContexts.xml (only inputUserMappings
    // ships one). Log loudly so the user knows the merge will be incomplete.
    spdlog::error("Failed to load r6/config/inputContexts.xml - merged "
                  "output will be incomplete. Please verify the file exists "
                  "and is valid XML.");
  }

  // malformed XML in 1.6, so we need to load the supplied .xml if this fails
  bool mappingsOk = true;
  inputUserMappingsOriginal =
      LoadDocument("r6/config/inputUserMappings.xml", &mappingsOk);
  if (!mappingsOk) {
    spdlog::info("The above is a normal error in 1.6+ - loading backup "
                 "inputUserMappings.xml");
    inputUserMappingsOriginal =
        LoadDocument("red4ext/plugins/input_loader/inputUserMappings.xml");
  }
}

// Case-insensitive comparison for file extensions. NTFS preserves the stored
// case, so a stray `.XML` mod file would otherwise be silently skipped by a
// plain `== ".xml"` check.
static bool HasXmlExtension(const std::filesystem::path &p) {
  std::string ext = p.extension().string();
  if (ext.size() != 4)
    return false;
  return (ext[0] == '.') &&
         (ext[1] == 'x' || ext[1] == 'X') &&
         (ext[2] == 'm' || ext[2] == 'M') &&
         (ext[3] == 'l' || ext[3] == 'L');
}

bool LoadInputConfigs(RED4ext::CGameApplication *) {
  // block mostly copied from
  // https://github.com/WopsS/TweakDBext/blob/master/src/Hooks.cpp
  auto inputDir = Utils::GetRootDir() / "r6/input";
  try {
    if (!std::filesystem::exists(inputDir)) {
      std::filesystem::create_directories(inputDir);
    }
    // load xml documents in r6/input
    spdlog::info("Loading input configs from r6/input");
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(inputDir)) {
      // Skip directories, symlinks, sockets, etc. Only merge regular files.
      // This prevents following symlink loops and attempting to parse
      // directory entries as XML.
      std::error_code ec;
      if (!entry.is_regular_file(ec)) {
        continue;
      }
      const auto &path = entry.path();
      if (!HasXmlExtension(path)) {
        continue;
      }
      try {
        MergeDocument(path);
      } catch (const std::exception &ex) {
        spdlog::error(L"An exception occurred while trying to load '{}'",
                      path.c_str());
        spdlog::error("Exception: {}", ex.what());
      } catch (...) {
        spdlog::error(L"An unknown error occurred while trying to load '{}'",
                      path.c_str());
      }
    }

    // Snapshot the dynamic paths under the mutex so we don't race with a
    // late Add() call while iterating. (Late calls will simply be missed,
    // matching previous behaviour, but at least we won't UB.)
    std::vector<std::filesystem::path> dynamicPaths;
    {
      std::lock_guard<std::mutex> guard(document_paths_mutex);
      dynamicPaths = document_paths;
    }

    spdlog::info("Loading input configs from dynamically added paths");
    for (const auto &path : dynamicPaths) {
      try {
        MergeDocument(path);
      } catch (const std::exception &ex) {
        spdlog::error(L"An exception occurred while trying to load '{}'",
                      path.c_str());
        spdlog::error("Exception: {}", ex.what());
      } catch (...) {
        spdlog::error(L"An unknown error occurred while trying to load '{}'",
                      path.c_str());
      }
    }
  } catch (const std::exception &ex) {
    spdlog::error(L"An exception occurred while reading the directory '{}'",
                  inputDir.c_str());
    spdlog::error("Exception: {}", ex.what());
  } catch (...) {
    spdlog::error(L"An unknown error occurred while reading the directory '{}'",
                  inputDir.c_str());
  }

  // Ensure the cache directory exists - save_file() silently fails if it
  // doesn't, which previously caused the game to start with an empty input
  // configuration (broken controls).
  auto cacheDir = Utils::GetRootDir() / "r6/cache";
  try {
    if (!std::filesystem::exists(cacheDir)) {
      std::filesystem::create_directories(cacheDir);
    }
  } catch (const std::exception &ex) {
    spdlog::error("Failed to create cache directory '{}': {}",
                  cacheDir.string(), ex.what());
  } catch (...) {
    spdlog::error("Unknown error creating cache directory '{}'",
                  cacheDir.string());
  }

  // save files
  auto contextsCachePath = Utils::GetRootDir() / "r6/cache/inputContexts.xml";
  if (!inputContextsOriginal.save_file(contextsCachePath.string().c_str())) {
    spdlog::error("Failed to save merged inputContexts to '{}'",
                  contextsCachePath.string());
  } else {
    spdlog::info("Merged inputContexts saved to 'r6/cache/inputContexts.xml'");
  }

  auto mappingsCachePath =
      Utils::GetRootDir() / "r6/cache/inputUserMappings.xml";
  if (!inputUserMappingsOriginal.save_file(
          mappingsCachePath.string().c_str())) {
    spdlog::error("Failed to save merged inputUserMappings to '{}'",
                  mappingsCachePath.string());
  } else {
    spdlog::info(
        "Merged inputUserMappings saved to 'r6/cache/inputUserMappings.xml'");
  }

  return true;
}

} // namespace InputLoader

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
  switch (fdwReason) {
  case DLL_PROCESS_ATTACH:
    Utils::CreateLogger();
    spdlog::info("Starting up Input Loader {}", MOD_VERSION_STR);
    break;
  }
  return TRUE;
}

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::PluginHandle aHandle,
                                        RED4ext::EMainReason aReason,
                                        const RED4ext::Sdk *aSdk) {
  switch (aReason) {
  case RED4ext::EMainReason::Load: {
    // DisableThreadLibraryCalls(aHandle);

    spdlog::info("Connected to RED4ext - registering load callback");
    RED4ext::GameState initState;
    initState.OnEnter = nullptr;
    initState.OnUpdate = nullptr;
    initState.OnExit = &InputLoader::LoadInputConfigs;

    aSdk->gameStates->Add(aHandle, RED4ext::EGameStateType::BaseInitialization,
                          &initState);

    InputLoader::LoadOriginals();
    break;
  }
  case RED4ext::EMainReason::Unload: {
    spdlog::info("Shutting down");
    spdlog::shutdown();
    break;
  }
  }

  return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::PluginInfo *aInfo) {
  aInfo->name = L"Input Loader";
  aInfo->author = L"Jack Humbert";
  aInfo->version =
      RED4EXT_SEMVER(MOD_VERSION_MAJOR, MOD_VERSION_MINOR, MOD_VERSION_PATCH);
  aInfo->runtime = RED4EXT_RUNTIME_INDEPENDENT;
  aInfo->sdk = RED4EXT_SDK_LATEST;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports() {
  return RED4EXT_API_VERSION_LATEST;
}
