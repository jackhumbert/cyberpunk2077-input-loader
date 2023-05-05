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
  std::string documentPath = (Utils::GetRootDir() / path).string();
  pugi::xml_parse_result result = document.load_file(documentPath.c_str());

  if (!result) {
    spdlog::error("XML document parsed with errors: {}", documentPath);
    spdlog::error("Error description: {}", result.description());
    spdlog::error("Error offset: {}", result.offset);
    if (status)
      *status = false;
    return document;
  }

  spdlog::info("Loaded document: {}", documentPath);

  if (status)
    *status = true;
  return document;
}

// https://stackoverflow.com/questions/20303821/how-to-check-if-string-is-in-array-of-strings
bool in_array(const std::string &value, const std::vector<std::string> &array) {
  return std::find(array.begin(), array.end(), value) != array.end();
}

std::vector<std::string> valid_inputUserMappings = {"mapping", "buttonGroup",
                                                    "pairedAxes", "preset"};

std::vector<std::string> valid_inputContexts = {
    "blend",  "context", "hold",          "multitap",
    "repeat", "toggle",  "acceptedEvents"};

pugi::xml_document inputContextsOriginal;
pugi::xml_document inputUserMappingsOriginal;

static std::vector<std::filesystem::path> document_paths;

void Add(std::filesystem::path path) {
    spdlog::info(L"Will load document: {}", path.c_str()); 
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
  pugi::xml_document modDocument = LoadDocument(path.c_str());

  // process bindings
  for (pugi::xml_node modNode : modDocument.child("bindings").children()) {
    spdlog::info("Processing mod input block: {}", modNode.name());
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
      spdlog::warn("<bindings> child '{}' not valid", modNode.name());
      continue;
    }
    if (existing) {
      if (modNode.attribute("append").as_bool()) {
        for (pugi::xml_node modNodeChild : modNode.children()) {
          existing.append_copy(modNodeChild);
        }
      } else {
        document->child("bindings").remove_child(existing);
        document->child("bindings").append_copy(modNode);
      }
    } else {
      document->child("bindings").append_copy(modNode);
    }
  }
}

void LoadOriginals() {
  spdlog::info("Loading original input Ccnfigs for merging");

  inputContextsOriginal = LoadDocument("r6/config/inputContexts.xml");
  // malformed XML in 1.6, so we need to load the supplied .xml if this fails
  bool fixed = false;
  inputUserMappingsOriginal =
      LoadDocument("r6/config/inputUserMappings.xml", &fixed);
  if (!fixed) {
    spdlog::info("Loading backup inputUserMappings.xml");
    inputUserMappingsOriginal =
        LoadDocument("red4ext/plugins/input_loader/inputUserMappings.xml");
  }
}

bool LoadInputConfigs(RED4ext::CGameApplication *) {
  spdlog::info("Loading input configs from mods");
  // block mostly copied from
  // https://github.com/WopsS/TweakDBext/blob/master/src/Hooks.cpp
  auto inputDir = Utils::GetRootDir() / "r6/input";
  try {
    if (!std::filesystem::exists(inputDir)) {
      std::filesystem::create_directories(inputDir);
    }
    // load xml documents in r6/input
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(inputDir)) {
      const auto &path = entry.path();
      if (entry.path().extension() == ".xml") {
        try {
          MergeDocument(path);
        } catch (const std::exception &ex) {
          spdlog::error("An exception occured while trying to load '{}'",
                        (void *)path.c_str());
          // spdlog::error(ex.what());
        } catch (...) {
          spdlog::error("An unknown error occured while trying to load '{}'",
                        (void *)path.c_str());
        }
      }
    }
    for (const auto &path : document_paths) {
      try {
        MergeDocument(path);
      } catch (const std::exception &ex) {
        spdlog::error("An exception occured while trying to load '{}'",
                      (void *)path.c_str());
        // spdlog::error(ex.what());
      } catch (...) {
        spdlog::error("An unknown error occured while trying to load '{}'",
                      (void *)path.c_str());
      }
    }
  } catch (const std::exception &ex) {
    spdlog::error("An exception occured while reading the directory '{}'",
                  (void *)inputDir.c_str());
    // spdlog::error(ex.what());
  } catch (...) {
    spdlog::error("An unknown error occured while reading the directory '{}'",
                  (void *)inputDir.c_str());
  }

  // save files
  inputContextsOriginal.save_file(
      (Utils::GetRootDir() / "r6/cache/inputContexts.xml").string().c_str());
  inputUserMappingsOriginal.save_file(
      (Utils::GetRootDir() / "r6/cache/inputUserMappings.xml")
          .string()
          .c_str());

  // save/resave our config so the game grabs them
  std::string iniFilePath =
      (Utils::GetRootDir() / "engine/config/platform/pc/input_loader.ini")
          .string();
  std::string fileContents = R"([Player/Input]
InputContextFile = "cache\inputContexts.xml"
InputMappingFile = "cache\inputUserMappings.xml")";
  std::ofstream fw(iniFilePath.c_str(), std::ofstream::out);
  if (!fw.is_open())
    spdlog::error("Could not open the ini file to write to");
  fw << fileContents;
  fw.close();

  return true;
}

} // namespace InputLoader

BOOL WINAPI DllMain(HINSTANCE hinstDLL, // handle to DLL module
                    DWORD fdwReason,    // reason for calling function
                    LPVOID lpvReserved) // reserved
{
  // Perform actions based on the reason for calling.
  switch (fdwReason) {
  case DLL_PROCESS_ATTACH:
    // Initialize once for each new process.
    // Return FALSE to fail DLL load.
    Utils::CreateLogger();
    break;

  case DLL_THREAD_ATTACH:
    // Do thread-specific initialization.
    break;

  case DLL_THREAD_DETACH:
    // Do thread-specific cleanup.
    break;

  case DLL_PROCESS_DETACH:

    if (lpvReserved != nullptr) {
      break; // do not do cleanup if process termination scenario
    }

    // Perform any necessary cleanup.
    break;
  }
  return TRUE; // Successful DLL_PROCESS_ATTACH.
}

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::PluginHandle aHandle,
                                        RED4ext::EMainReason aReason,
                                        const RED4ext::Sdk *aSdk) {
  switch (aReason) {
  case RED4ext::EMainReason::Load: {
    // DisableThreadLibraryCalls(aHandle);

    spdlog::info("Starting up Input Loader {}", MOD_VERSION_STR);
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
