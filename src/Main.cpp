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

// -----------------------------------------------------------------------------
// Merge statistics - tracks per-mod contribution to the merged cache files.
// Logged at the end of LoadInputConfigs so users debugging crashes can see
// which mod touched what. This is the audit trail that was previously missing.
// -----------------------------------------------------------------------------
struct MergeStats {
  size_t modsProcessed = 0;
  size_t modsSkipped = 0;
  size_t nodesAdded = 0;       // New nodes appended (no existing match)
  size_t nodesReplaced = 0;   // Existing node removed, mod node put in its place
  size_t nodesAppended = 0;    // Existing node's children extended via append="true"
  size_t nodesSkipped = 0;     // Unknown node type, skipped
  size_t errors = 0;           // Caught exceptions or parse failures
};
static MergeStats g_mergeStats;

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
    //
    // GetModuleFileNameW does NOT clear the last error on success, so a stale
    // ERROR_INSUFFICIENT_BUFFER from a previous iteration could persist and
    // loop forever. Reset the last error before each call, and decide whether
    // to grow using the return value (== bufferSize-1 means possible
    // truncation) plus a fresh last-error check. Capture the error code
    // immediately on failure so the log message is accurate.
    std::wstring dllFilePath;
    constexpr size_t pathChunk = MAX_PATH + 1;
    constexpr size_t kMaxResizeIterations = 16;
    bool resolved = false;
    DWORD lastErr = ERROR_SUCCESS;

    for (size_t iter = 0; iter < kMaxResizeIterations; ++iter) {
      dllFilePath.resize(dllFilePath.size() + pathChunk, L'\0');

      ::SetLastError(ERROR_SUCCESS);
      auto length = GetModuleFileNameW(aHandle, dllFilePath.data(),
                                       static_cast<uint32_t>(dllFilePath.size()));

      if (length == 0) {
        lastErr = ::GetLastError();
        spdlog::error("InputLoader::Add() - GetModuleFileNameW failed (errno={:#x}); ignoring path '{}'",
                      lastErr, path.string());
        return;
      }

      // GetModuleFileNameW returns chars written, NOT including NUL. If
      // length < bufferSize-1, the path fit. If length == bufferSize-1, it
      // *may* have been truncated - confirm with a fresh last-error check.
      if (length < dllFilePath.size() - 1) {
        dllFilePath.resize(length);
        resolved = true;
        break;
      }

      lastErr = ::GetLastError();
      if (lastErr != ERROR_INSUFFICIENT_BUFFER) {
        // Path is exactly bufferSize-1 chars - not truncation. Done.
        dllFilePath.resize(length);
        resolved = true;
        break;
      }
      // else: confirmed truncation, loop and grow.
    }

    if (!resolved) {
      spdlog::error("InputLoader::Add() - GetModuleFileNameW exhausted resize iterations (last errno={:#x}); ignoring path '{}'",
                    lastErr, path.string());
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

  // Per-mod counters - logged at the end so a suspicious mod (one that
  // replaces many existing nodes) is visible in the log.
  size_t modAdded = 0, modReplaced = 0, modAppended = 0, modSkipped = 0;

  bool loadOk = true;
  pugi::xml_document modDocument = LoadDocument(path, &loadOk);
  spdlog::info(L"Loading document: {}", path.c_str());

  if (!loadOk) {
    // LoadDocument already logged the parse error details; just bail out so
    // we don't silently produce a partial merge.
    spdlog::error(L"Skipping merge for '{}' due to parse/load errors", path.c_str());
    ++g_mergeStats.modsSkipped;
    ++g_mergeStats.errors;
    return;
  }

  pugi::xml_node modBindings = modDocument.child("bindings");
  if (!modBindings) {
    spdlog::warn(L"Document '{}' has no <bindings> root; nothing to merge", path.c_str());
    ++g_mergeStats.modsSkipped;
    return;
  }

  // process bindings
  for (pugi::xml_node modNode : modBindings.children()) {
    const auto modNodeName = modNode.name();
    const auto modNodeAttr = modNode.attribute("name").as_string();
    spdlog::info("* Processing mod input block: {} name='{}'", modNodeName, modNodeAttr);
    pugi::xml_node existing;
    pugi::xml_document *document;
    if (in_array(modNodeName, valid_inputContexts)) {
      existing =
          inputContextsOriginal.child("bindings")
              .find_child_by_attribute(modNodeName, "name", modNodeAttr);
      document = &inputContextsOriginal;
    } else if (in_array(modNodeName, valid_inputUserMappings)) {
      existing =
          inputUserMappingsOriginal.child("bindings")
              .find_child_by_attribute(modNodeName, "name", modNodeAttr);
      document = &inputUserMappingsOriginal;
    } else {
      spdlog::warn("* <bindings> child '{}' name='{}' is not a valid input node type; skipping",
                   modNodeName, modNodeAttr);
      ++modSkipped;
      ++g_mergeStats.nodesSkipped;
      continue;
    }

    pugi::xml_node targetBindings = document->child("bindings");
    if (!targetBindings) {
      // Original document is malformed (no <bindings> root). Recreate it so
      // we don't lose the mod's data, and warn loudly.
      spdlog::warn("Target document had no <bindings> root; creating one.");
      targetBindings = document->append_child("bindings");
    }

    // Wrap the actual mutation in try/catch - pugixml's append_copy /
    // remove_child can throw std::bad_alloc on truly pathological input
    // (huge copy graphs, deep recursion). Catching here lets us attribute the
    // failure to this specific mod node rather than letting it propagate to
    // the outer per-file handler, which only knows the file path.
    try {
      if (existing) {
        if (modNode.attribute("append").as_bool()) {
          size_t appendedChildren = 0;
          for (pugi::xml_node modNodeChild : modNode.children()) {
            existing.append_copy(modNodeChild);
            ++appendedChildren;
          }
          spdlog::info("  + appended {} children to existing '{}'",
                       appendedChildren, modNodeAttr);
          ++modAppended;
          ++g_mergeStats.nodesAppended;
        } else {
          targetBindings.remove_child(existing);
          targetBindings.append_copy(modNode);
          spdlog::info("  ~ replaced existing '{}' (was: '{}', now from: '{}')",
                       modNodeAttr, path.filename().string(), path.filename().string());
          ++modReplaced;
          ++g_mergeStats.nodesReplaced;
        }
      } else {
        targetBindings.append_copy(modNode);
        spdlog::info("  + added new '{}'", modNodeAttr);
        ++modAdded;
        ++g_mergeStats.nodesAdded;
      }
    } catch (const std::exception &ex) {
      spdlog::error("  ! failed to merge node '{}' from '{}': {}",
                    modNodeAttr, path.filename().string(), ex.what());
      ++g_mergeStats.errors;
      // Continue to next node - one bad node shouldn't abort the whole mod.
    } catch (...) {
      spdlog::error("  ! unknown failure merging node '{}' from '{}'",
                    modNodeAttr, path.filename().string());
      ++g_mergeStats.errors;
    }
  }

  ++g_mergeStats.modsProcessed;
  spdlog::info("  [mod summary] {}: added={}, replaced={}, appended={}, skipped={}",
               path.filename().string(), modAdded, modReplaced, modAppended, modSkipped);
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

// Save a merged pugixml document with a provenance comment prepended, so
// users debugging crashes can tell at a glance that this file was generated
// by input_loader (with how many mods / errors) rather than being a vanilla
// game file. Also logs the actual errno on file open failure, since pugixml's
// save_file() returns only a bool with no error detail.
//
// We prepend the comment manually because pugixml's format flags don't
// include a way to inject a custom top-level comment alongside the XML
// declaration. The simplest reliable approach: open a std::ofstream, write
// the comment, then call document.save() with the stream as the writer.
static bool SaveDocumentWithAudit(const pugi::xml_document &doc,
                                  const std::filesystem::path &path,
                                  const char *docName) {
  std::ofstream out(path, std::ios::out | std::ios::trunc | std::ios::binary);
  if (!out.is_open()) {
    const auto err = errno;
    spdlog::error("Failed to open '{}' for writing (errno={} '{}')",
                  path.string(), err, std::strerror(err));
    return false;
  }

  // Provenance comment - written before the XML declaration so the file
  // still parses as valid XML (XML allows comments after the declaration,
  // but having the audit comment OUTSIDE the document tree is cleanest).
  out << "<!--\n";
  out << "  This file was generated by Cyberpunk 2077 Input Loader v"
      << MOD_VERSION_STR << ".\n";
  out << "  Source: " << docName << " (merged with user mods)\n";
  out << "  Generated at: " << __DATE__ << " " << __TIME__ << "\n";
  out << "  Mods processed: " << g_mergeStats.modsProcessed << "\n";
  out << "  Nodes added: " << g_mergeStats.nodesAdded
      << ", replaced: " << g_mergeStats.nodesReplaced
      << ", appended: " << g_mergeStats.nodesAppended << "\n";
  out << "  Errors: " << g_mergeStats.errors << "\n";
  out << "  Do not edit by hand - changes will be overwritten on next game launch.\n";
  out << "-->\n";

  // Save the document without its own XML declaration (we already wrote the
  // provenance comment) - actually pugixml requires the XML declaration to
  // come first if present, so we let pugixml emit it after our comment.
  // The format_no_declaration flag would skip it; we want it included.
  if (!doc.save(out)) {
    spdlog::error("pugixml failed to serialize document to '{}'",
                  path.string());
    return false;
  }

  out.flush();
  if (!out.good()) {
    const auto err = errno;
    spdlog::error("Write error while saving '{}' (errno={} '{}')",
                  path.string(), err, std::strerror(err));
    return false;
  }

  return true;
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

  // save files. We don't use save_file() because pugixml's save_file can
  // fail silently on permission errors; instead, open the file ourselves
  // (which gives us a real errno on failure) and use save() with a writer.
  // We also prepend a provenance comment so users debugging crashes can tell
  // at a glance that this file was generated by input_loader (and how many
  // mods contributed to it) rather than being a vanilla game file.
  auto contextsCachePath = Utils::GetRootDir() / "r6/cache/inputContexts.xml";
  bool contextsSaved = SaveDocumentWithAudit(
      inputContextsOriginal, contextsCachePath, "inputContexts");
  if (contextsSaved) {
    spdlog::info("Merged inputContexts saved to '{}'", contextsCachePath.string());
  } else {
    ++g_mergeStats.errors;
  }

  auto mappingsCachePath =
      Utils::GetRootDir() / "r6/cache/inputUserMappings.xml";
  bool mappingsSaved = SaveDocumentWithAudit(
      inputUserMappingsOriginal, mappingsCachePath, "inputUserMappings");
  if (mappingsSaved) {
    spdlog::info("Merged inputUserMappings saved to '{}'", mappingsCachePath.string());
  } else {
    ++g_mergeStats.errors;
  }

  // Final summary - this is the audit trail. Users debugging a crash can
  // read this block to see exactly which mods were merged and how many
  // nodes each operation touched. If a crash happens later in the game's
  // input system, this log is the starting point for figuring out which
  // mod caused it.
  spdlog::info("========================================================");
  spdlog::info("Input Loader merge summary:");
  spdlog::info("  Mods processed: {}", g_mergeStats.modsProcessed);
  spdlog::info("  Mods skipped (parse/structure errors): {}", g_mergeStats.modsSkipped);
  spdlog::info("  Nodes added (new): {}", g_mergeStats.nodesAdded);
  spdlog::info("  Nodes replaced (overwrote existing): {}", g_mergeStats.nodesReplaced);
  spdlog::info("  Nodes appended (extend via append=\"true\"): {}", g_mergeStats.nodesAppended);
  spdlog::info("  Nodes skipped (unknown type): {}", g_mergeStats.nodesSkipped);
  spdlog::info("  Errors: {}", g_mergeStats.errors);
  if (g_mergeStats.errors > 0 || g_mergeStats.modsSkipped > 0) {
    spdlog::warn("  ** Some mods failed to merge cleanly - see log above for details. **");
    spdlog::warn("  ** The game may still load, but inputs from failed mods will be missing. **");
  }
  if (g_mergeStats.nodesReplaced > 0) {
    spdlog::warn("  ** {} existing node(s) were replaced - if the game crashes or behaves **", g_mergeStats.nodesReplaced);
    spdlog::warn("  ** unexpectedly, look for 'replaced existing' lines above to identify **");
    spdlog::warn("  ** which mod overrode which input. **");
  }
  spdlog::info("========================================================");

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
