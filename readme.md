# Cyberpunk 2077 Input Loader

This plugin looks for all `r6/input/*.xml` files and merges them with the appropriate input config file from `r6/config/` and saves results to `r6/cache/`. A configuration file is written to `engine/config/platform/pc/input_loader.ini`, which tells the game to load the merged .xml files.

## Requirements

* [RED4ext](https://github.com/WopsS/RED4ext) 1.27.0+
* To build from source:
  * Visual Studio 2022 (Build Tools or Community) with the C++ workload
  * CMake 3.5+ (4.x is fine)
  * Ninja (recommended)
  * Git

## Usage (for end users)

1. Install [RED4ext](https://github.com/WopsS/RED4ext)
2. Download the latest `input_loader_vX.X.X.zip` from [releases](https://github.com/jackhumbert/cyberpunk2077-input-loader/releases/latest)
3. Extract the zip into your Cyberpunk 2077 install root (the folder containing `bin\x64\Cyberpunk2077.exe`)
4. Place a mod's custom .xml file in `r6/input/`
5. Start the game

A log file will be written to `red4ext/logs/input_loader.log` every start-up.

## Building from source

### Quick way

Open the **"x64 Native Tools Command Prompt for VS 2022"** from your Start Menu, then:

```cmd
cd path\to\cyberpunk2077-input-loader-fork
git submodule update --init --recursive
build.bat
package.bat
```

After `build.bat` finishes you'll have:

| Path | Contents |
|---|---|
| `build\input_loader.dll` | The compiled plugin |
| `game_dir\` | Ready-to-install file tree (matches the game's layout) |
| `game_dir_debug\` | Same as `game_dir\` but with `.pdb` instead of `.dll` |

After `package.bat` finishes you'll have:

| Path | Contents |
|---|---|
| `dist\input_loader_vX.X.X.zip` | End-user install archive |
| `dist\input_loader_vX.X.X_pdb.zip` | Debug symbols for crash reports |

### Manual way (if you don't want to use build.bat)

```cmd
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CI_BUILD=ON -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build --config RelWithDebInfo
cmake --install build --config RelWithDebInfo
```

### Installing directly to the game

After building, you can skip the packaging step and install straight to your game directory:

```cmd
tools\install.bat
:: or with a custom game path:
tools\install.bat "D:\games\Cyberpunk 2077"
```

## Dynamic loading with a RED4ext plugin

Instead of placing .xml files in `r6/input/`, you can dynamically add an input file from a RED4ext plugin with a path relative to its folder (located at `red4ext/plugins/<plugin_name>/inputs.xml`):

```cpp
#include <InputLoader.hpp>

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::PluginHandle aHandle, 
                                        RED4ext::EMainReason aReason, 
                                        const RED4ext::Sdk *aSdk)
{
    switch (aReason) {
        case RED4ext::EMainReason::Load: {
            InputLoader::Add(aHandle, "inputs.xml");
        }
    }
    return true;
}
```

This will prevent the .xml file from being merged if the RED4ext plugin doesn't load (when the game's version doesn't match, etc).

## Node type

Only children of `<bindings>` are supported currently, but they can all be in the same .xml file (to encourage a mod to have a single .xml file). Depending on the node type, the block will be added to the new `inputContexts.xml` or `inputUserMappings.xml` automatically:

    inputUserMappings.xml:
    * mapping
    * buttonGroup
    * pairedAxes
    * preset

    inputContexts.xml:
    * blend
    * context
    * hold
    * multitap
    * repeat
    * toggle
    * acceptedEvents

## Node attributes

You can add `append="true"` to a node to avoid overwriting. This is the recommended method for adding functionality, as multiple mods will be able to do this without conflicts. See below for an example.

## Example .xml file

```xml
<?xml version="1.0"?>
<bindings>
    <!-- Defines a custom context with a custom Action, mapped to UseConsumable_Button -->
    <context name="MyCustomContext" >
        <action name="ShakeAroundABit" map="UseConsumable_Button" />
    </context>

    <!-- Adds the custom context to VehicleDrive without overwriting existing definitions -->
    <context name="VehicleDrive" append="true">
        <include name="MyCustomContext" />
    </context>

</bindings>
```

## Crash debugging

Input Loader writes a detailed audit trail to `red4ext/logs/input_loader.log` on every game launch. If the game crashes or behaves unexpectedly (broken inputs, controls not responding), check this log first:

- **Per-mod summary lines** show how many nodes each mod added / replaced / appended.
- **`~ replaced existing` lines** indicate mods that overrode existing game inputs - often the cause of crashes.
- **A final summary block** at the end of the log lists totals for each operation type.
- The generated `r6/cache/inputContexts.xml` and `r6/cache/inputUserMappings.xml` files start with a provenance comment showing how many mods were merged.

## Uninstallation

You can run `red4ext/plugins/input_loader_uninstall.bat` (if generated by `configure_uninstall`) or simply remove the following files/directories:

- `red4ext/plugins/input_loader/`
- `r6/cache/inputContexts.xml`
- `r6/cache/inputUserMappings.xml`
- `engine/config/platform/pc/input_loader.ini`
