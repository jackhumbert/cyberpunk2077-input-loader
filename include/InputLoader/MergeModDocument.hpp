#pragma once

#include <cstdint>
#include <filesystem>

#ifdef DLLDIR_EX
   #define DLLDIR  __declspec(dllexport)   // export DLL information
#else
   #define DLLDIR  __declspec(dllimport)   // import DLL information
#endif 

namespace InputLoader {

DLLDIR void MergeModDocument(std::filesystem::path path);

}