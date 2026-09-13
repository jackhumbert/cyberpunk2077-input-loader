#include <iostream>

#include <RED4ext/RED4ext.hpp>

#include <pugixml.hpp>

#include "Utils.hpp"
#include "stdafx.hpp"
#include <InputLoader.hpp>

namespace InputLoader {
pugi::xml_document LoadDocument(std::filesystem::path path,
                                bool *status = nullptr) {

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

static std::vector<std::filesystem::path> document_paths;

// The game reads engine/config/platform/pc/input_loader.ini and loads the xmls
// it names from r6/. If the ini survives while the cache xmls are missing or
// empty (deleted cache, renamed r6/cache, partial uninstall) the game shows
// "PRESS [None] TO CONTINUE" and crashes on Space, so never leave that state.
static const std::filesystem::path iniPath =
    "engine/config/platform/pc/input_loader.ini";
static const std::filesystem::path cacheDir = "r6/cache";
static const std::filesystem::path cacheContextsPath =
    "r6/cache/inputContexts.xml";
static const std::filesystem::path cacheMappingsPath =
    "r6/cache/inputUserMappings.xml";
// Paths are relative to r6/. Same content as the input_loader.ini in the zip.
static const char *iniContent =
    "[Player/Input]\r\n"
    "InputContextFile = \"cache\\inputContexts.xml\"\r\n"
    "InputMappingFile = \"cache\\inputUserMappings.xml\"";

// A cache xml the game can load: present, non-empty, and parseable with a
// <bindings> root (a crash during save can leave a truncated file).
bool IsUsableFile(const std::filesystem::path &path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) ||
      std::filesystem::file_size(path, ec) == 0 || ec)
    return false;
  pugi::xml_document document;
  return document.load_file(path.string().c_str()) &&
         document.child("bindings");
}

bool HaveOriginals() {
  return inputContextsOriginal.child("bindings") &&
         inputUserMappingsOriginal.child("bindings");
}

void RemoveIni() {
  auto path = Utils::GetRootDir() / iniPath;
  std::error_code ec;
  if (std::filesystem::remove(path, ec)) {
    spdlog::error("Removed '{}' so the game falls back to its own r6/config "
                  "input xmls",
                  iniPath.string());
  } else if (ec) {
    spdlog::error("Could not remove '{}' ({}); the game may show PRESS [None] "
                  "TO CONTINUE until it is deleted by hand",
                  iniPath.string(), ec.message());
  }
}

// Writes the ini when both cache xmls are usable, removes it otherwise.
void SyncIni() {
  auto root = Utils::GetRootDir();
  if (!IsUsableFile(root / cacheContextsPath) ||
      !IsUsableFile(root / cacheMappingsPath)) {
    spdlog::error("r6/cache/inputContexts.xml or r6/cache/inputUserMappings.xml "
                  "is missing or empty");
    RemoveIni();
    return;
  }
  auto path = root / iniPath;
  std::string current;
  {
    std::ifstream in(path, std::ios::binary);
    current.assign(std::istreambuf_iterator<char>(in),
                   std::istreambuf_iterator<char>());
  }
  if (current == iniContent)
    return;
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << iniContent;
  if (out) {
    spdlog::info("Wrote '{}'", iniPath.string());
  } else {
    spdlog::error("Could not write '{}'; the game will use its own r6/config "
                  "input xmls",
                  iniPath.string());
  }
}

// Saves both documents to r6/cache and checks the result. Any failure
// neutralizes the ini so the game never points at broken files.
bool SaveCache(const char *what) {
  auto root = Utils::GetRootDir();
  std::error_code ec;
  std::filesystem::create_directories(root / cacheDir, ec);
  bool ok = inputContextsOriginal.save_file(
      (root / cacheContextsPath).string().c_str());
  ok = inputUserMappingsOriginal.save_file(
           (root / cacheMappingsPath).string().c_str()) &&
       ok;
  if (ok) {
    spdlog::info("{} input configs saved to 'r6/cache/inputContexts.xml' and "
                 "'r6/cache/inputUserMappings.xml'",
                 what);
  } else {
    spdlog::error("Failed to write {} input configs to r6/cache", what);
  }
  SyncIni();
  return ok;
}

// The attribute the game's own xmls use to identify each <bindings> child.
// <blend> has no key (only from/to/event) and is always appended.
const char *KeyAttribute(const std::string &name) {
  if (name == "hold" || name == "multitap" || name == "repeat" ||
      name == "toggle" || name == "acceptedEvents")
    return "action";
  if (name == "buttonGroup")
    return "id";
  if (name == "blend")
    return nullptr;
  return "name"; // context, mapping, pairedAxes, preset
}

pugi::xml_node FindExisting(pugi::xml_node bindings, pugi::xml_node modNode) {
  const char *key = KeyAttribute(modNode.name());
  if (!key)
    return {};
  pugi::xml_attribute id = modNode.attribute(key);
  if (!id) {
    spdlog::warn("* <{}> has no '{}' attribute, cannot match an existing entry",
                 modNode.name(), key);
    return {};
  }
  return bindings.find_child_by_attribute(modNode.name(), key, id.value());
}

std::string NodeLabel(pugi::xml_node node) {
  std::string label = node.name();
  const char *key = KeyAttribute(node.name());
  if (key && node.attribute(key))
    label += std::string(" ") + key + "=\"" + node.attribute(key).value() + "\"";
  return label;
}

void MergeDocument(std::filesystem::path path);

RED4EXT_C_EXPORT void Add(RED4ext::PluginHandle aHandle, const wchar_t * str) {
  std::filesystem::path path(str);
  if (path.is_relative()) {
    char dllFilePath[513] = {0};
    GetModuleFileNameA(aHandle, dllFilePath, 512);
    std::filesystem::path dllPath(dllFilePath);
    path = dllPath.parent_path() / path;
  }
  spdlog::info(L"Will load document: {}", path.c_str());
  document_paths.emplace_back(path);
  // Plugins that load after Input Loader register here after the merge at
  // Load already ran; merge and save right away so the cache is complete
  // before the game's state machine starts. Plugins that load earlier are
  // picked up by MergeAll.
  if (HaveOriginals()) {
    MergeDocument(path);
    SaveCache("Merged");
  }
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
  pugi::xml_document modDocument = LoadDocument(path);
  spdlog::info(L"Loading document: {}", path.c_str());

  // process bindings
  for (pugi::xml_node modNode : modDocument.child("bindings").children()) {
    spdlog::info("* Processing mod input block: {}", modNode.name());
    pugi::xml_document *document;
    if (in_array(modNode.name(), valid_inputContexts)) {
      document = &inputContextsOriginal;
    } else if (in_array(modNode.name(), valid_inputUserMappings)) {
      document = &inputUserMappingsOriginal;
    } else {
      spdlog::warn("* <bindings> child '{}' not valid", modNode.name());
      continue;
    }
    pugi::xml_node bindings = document->child("bindings");
    pugi::xml_node existing = FindExisting(bindings, modNode);
    if (existing) {
      if (modNode.attribute("append").as_bool()) {
        for (pugi::xml_node modNodeChild : modNode.children()) {
          existing.append_copy(modNodeChild);
        }
        spdlog::info("* Appended children to <{}>", NodeLabel(modNode));
      } else {
        bindings.remove_child(existing);
        bindings.append_copy(modNode);
        spdlog::info("* Replaced <{}>", NodeLabel(modNode));
      }
    } else {
      bindings.append_copy(modNode);
      spdlog::info("* Added <{}>", NodeLabel(modNode));
    }
  }
}

void LoadOriginals() {
  spdlog::info("Loading original input configs for merging");

  inputContextsOriginal = LoadDocument("r6/config/inputContexts.xml");
  // malformed XML in 1.6, so we need to load the supplied .xml if this fails
  bool fixed = false;
  inputUserMappingsOriginal =
      LoadDocument("r6/config/inputUserMappings.xml", &fixed);
  if (!fixed) {
    spdlog::info("The above is a normal error in 1.6+ - loading backup "
                 "inputUserMappings.xml");
    inputUserMappingsOriginal =
        LoadDocument("red4ext/plugins/input_loader/inputUserMappings.xml");
  }
}

// Loads the game's r6/config xmls, merges every r6/input xml and every
// registered document on top, and writes r6/cache plus the ini.
//
// Timing: RED4ext loads every plugin (and runs their Load) before the game's
// state machine starts, and its BaseInitialization OnEnter callbacks run only
// after the game's own CBaseInitializationState::OnEnter (which reads the
// options ini), OnExit about 50 s later. So plugin Load is the only point
// guaranteed to precede the game's read of input_loader.ini and the cache
// xmls; merging at the old OnExit callback was why a fresh install needed a
// second launch.
void MergeAll() {
  LoadOriginals();
  if (!HaveOriginals()) {
    spdlog::error("Could not load the game's input xmls from r6/config, not "
                  "generating anything");
    RemoveIni();
    return;
  }
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
      const auto &path = entry.path();
      if (entry.path().extension() == ".xml") {
        try {
          MergeDocument(path);
        } catch (const std::exception &ex) {
          spdlog::error(L"An exception occured while trying to load '{}'",
                        path.c_str());
          // spdlog::error(ex.what());
        } catch (...) {
          spdlog::error(L"An unknown error occured while trying to load '{}'",
                        path.c_str());
        }
      }
    }
    spdlog::info("Loading input configs from dynamically added paths");
    for (const auto &path : document_paths) {
      try {
        MergeDocument(path);
      } catch (const std::exception &ex) {
        spdlog::error(L"An exception occured while trying to load '{}'",
                      path.c_str());
        // spdlog::error(ex.what());
      } catch (...) {
        spdlog::error(L"An unknown error occured while trying to load '{}'",
                      path.c_str());
      }
    }
  } catch (const std::exception &ex) {
    spdlog::error(L"An exception occured while reading the directory '{}'",
                  inputDir.c_str());
    // spdlog::error(ex.what());
  } catch (...) {
    spdlog::error(L"An unknown error occured while reading the directory '{}'",
                  inputDir.c_str());
  }

  // save files
  SaveCache("Merged");
}

// Late refresh from a clean r6/config load. The game has most likely read
// r6/cache by now, so anything new here applies on the next launch.
bool RefreshInputConfigs(RED4ext::CGameApplication *) {
  spdlog::info("Refreshing the merged input configs at BaseInitialization exit");
  MergeAll();
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
  return true;
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
    initState.OnExit = &InputLoader::RefreshInputConfigs;

    aSdk->gameStates->Add(aHandle, RED4ext::EGameStateType::BaseInitialization,
                          &initState);

    // Synchronous: must be done before the game reads the ini and r6/cache.
    InputLoader::MergeAll();
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
