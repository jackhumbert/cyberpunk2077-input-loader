#include <iostream>
#include <algorithm>
#include <string_view>

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/IScriptable.hpp>
#include <RED4ext/RTTITypes.hpp>
#include <RED4ext/Scripting/Natives/Generated/red/ResourceReferenceScriptToken.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/data/VehicleTPPCameraParams_Record.hpp>
#include <RED4ext/Scripting/Natives/Generated/ink/ImageWidget.hpp>
#include <RED4ext/Scripting/Natives/Generated/ink/TextureAtlas.hpp>
#include <RED4ext/Scripting/Natives/Generated/ink/EBlurDimension.hpp>
#include <RED4ext/Scripting/Natives/Generated/ink/BoxBlurEffect.hpp>
#include <RED4ext/InstanceType.hpp>
#include <RED4ext/Scripting/Natives/Generated/ink/IEffect.hpp>

#include <pugixml.hpp>

#include "Utils.hpp"
#include "stdafx.hpp"

pugi::xml_document LoadDocument(std::filesystem::path path, bool * status = nullptr) {

    pugi::xml_document document;
    std::string documentPath = (Utils::GetRootDir() / path).string();
    pugi::xml_parse_result result = document.load_file(documentPath.c_str());

    if (!result)
    {
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

pugi::xml_document inputContextsOriginal;
pugi::xml_document inputUserMappingsOriginal;

pugi::xml_document* find_document(pugi::xml_node const& node)
{
  constexpr std::array user_nodes = {
    "mapping", "buttonGroup", "pairedAxes", "preset",
  };
  constexpr std::array context_nodes = {
    "blend", "context", "hold", "multitap", "repeat", "toggle", "acceptedEvents",
  };

  std::string_view name = node.name();

  if (std::ranges::find(user_nodes, name) != user_nodes.end())
  {
    return &inputUserMappingsOriginal;
  }
  else if (std::ranges::find(context_nodes, name) != context_nodes.end())
  {
    return &inputContextsOriginal;
  }
  else
  {
    return nullptr;
  }
}

const char* find_unique_key(pugi::xml_node const& node)
{
  std::string_view name = node.name();

  if (name == "multitap" || name == "repeat" || name == "hold")
  {
    return "action";
  }
  else
  {
    return "name";
  }
}

void MergeModDocument(std::filesystem::path path)
{
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

    //pugi::xml_document modDocument = LoadDocument("r6/input/flight_control.xml");
    pugi::xml_document modDocument = LoadDocument(path.c_str());

    // process bindings
    for (pugi::xml_node modNode : modDocument.child("bindings").children())
    {
        spdlog::info("Processing mod input block: {}", modNode.name());
        auto document = find_document(modNode);
        if (!document)
        {
            spdlog::warn("<bindings> child '{}' not valid", modNode.name());
            continue;
        }
        auto key = find_unique_key(modNode);
        auto existing = document->child("bindings")
            .find_child_by_attribute(modNode.name(), key, modNode.attribute(key).as_string());

        if (existing)
        {
            if (modNode.attribute("append").as_bool()) {
                for (pugi::xml_node modNodeChild : modNode.children())
                {
                    existing.append_copy(modNodeChild);
                }
            }
            else
            {
                document->child("bindings").remove_child(existing);
                document->child("bindings").append_copy(modNode);
            }
        }
        else
        {
            document->child("bindings").append_copy(modNode);
        }
    }

}

void LoadInputConfigs() {

    // block mostly copied from https://github.com/WopsS/TweakDBext/blob/master/src/Hooks.cpp
    auto inputDir = Utils::GetRootDir() / "r6/input";
    try
    {
        if (!std::filesystem::exists(inputDir))
        {
            std::filesystem::create_directories(inputDir);
        }

        inputContextsOriginal = LoadDocument("r6/config/inputContexts.xml");
        // malformed XML in 1.6, so we need to load the supplied .xml if this fails
        bool fixed = false;
        inputUserMappingsOriginal = LoadDocument("r6/config/inputUserMappings.xml", &fixed);
        if (!fixed) {
          spdlog::info("Loading backup inputUserMappings.xml");
          inputUserMappingsOriginal = LoadDocument("red4ext/plugins/input_loader/inputUserMappings.xml");
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(inputDir))
        {
            const auto& path = entry.path();
            if (entry.path().extension() == ".xml")
            {
                try
                {
                    MergeModDocument(path);
                }
                catch (const std::exception& ex)
                {
                    spdlog::error("An exception occured while trying to load '{}'", (void*)path.c_str());
                    //spdlog::error(ex.what());
                }
                catch (...)
                {
                    spdlog::error("An unknown error occured while trying to load '{}'", (void*)path.c_str());
                }
            }
        }
    }
    catch (const std::exception& ex)
    {
        spdlog::error("An exception occured while reading the directory '{}'", (void*)inputDir.c_str());
        //spdlog::error(ex.what());
    }
    catch (...)
    {
        spdlog::error("An unknown error occured while reading the directory '{}'", (void*)inputDir.c_str());
    }

    // save files
    inputContextsOriginal.save_file((Utils::GetRootDir() / "r6/cache/inputContexts.xml").string().c_str());
    inputUserMappingsOriginal.save_file((Utils::GetRootDir() / "r6/cache/inputUserMappings.xml").string().c_str());

    // save/resave our config so the game grabs them
    std::string iniFilePath = (Utils::GetRootDir() / "engine/config/platform/pc/input_loader.ini").string();
    std::string fileContents = "[Player/Input]\nInputContextFile = \"cache\\inputContexts.xml\"\nInputMappingFile = \"cache\\inputUserMappings.xml\"";
    std::ofstream fw(iniFilePath.c_str(), std::ofstream::out);
    if (!fw.is_open())
        spdlog::error("Could not open the ini file to write to");
    fw << fileContents;
    fw.close();

    return;
}

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::PluginHandle aHandle, RED4ext::EMainReason aReason, const RED4ext::Sdk* aSdk)
{
    switch (aReason) {
        case RED4ext::EMainReason::Load: {
            //DisableThreadLibraryCalls(aHandle);

            Utils::CreateLogger();
            spdlog::info("Starting up Input Loader v0.0.7");
            LoadInputConfigs();
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

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::PluginInfo* aInfo)
{
    aInfo->name = L"Input Loader";
    aInfo->author = L"Jack Humbert";
    aInfo->version = RED4EXT_SEMVER(0, 0, 7);
    aInfo->runtime = RED4EXT_RUNTIME_LATEST;
    aInfo->sdk = RED4EXT_SDK_LATEST;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_LATEST;
}
