#pragma once

#include <cstdint>
#include <filesystem>
#include <RED4ext/RED4ext.hpp>

#ifdef DLLDIR_EX
   #define DLLDIR  __declspec(dllexport)   // export DLL information
#else
   #define DLLDIR  __declspec(dllimport)   // import DLL information
#endif 

namespace InputLoader {

DLLDIR void Add(RED4ext::PluginHandle aHandle, const wchar_t * str);

}