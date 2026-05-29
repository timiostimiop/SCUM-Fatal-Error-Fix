# SCUM menu crash patch

Small local patch for a SCUM crash I hit when launching the game without sound and then hovering/clicking buttons in the main menu.

This is not meant for online play. Do not use it with BattlEye. It is only for launching the game directly through `SCUM.exe` with BattlEye disabled/not running.

## What it fixes

The crash I saw had two bad paths:

- `SCUM.exe+0x4B16410`: XAudio open path can continue even when the XAudio pointer is not valid.
- `SCUM.exe+0x42DC090`: menu hover code can get called with a null source pointer and then crashes at `SCUM.exe+0x42DC09A`.

The DLL patches those spots in memory. If the risky pointer is missing, it returns a safe failure/empty result instead of letting the game crash.

There are no config files and no logs. The DLL just patches those two places and stays quiet.

## Important

This cannot be used while playing online.

Use it only like this:

- BattlEye is not running.
- The game is started by running `SCUM.exe` directly.
- You are using it to get around this local crash, not to join protected servers.

The injector also checks for common BattlEye processes and refuses to inject if it sees them.

## Build

Requirements:

- Windows
- Visual Studio C++ toolchain
- CMake

Build from the repo root:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Output files:

```text
build\Release\ScumAudioDiag.dll
build\Release\ScumAudioDiagInjector.exe
```

Keep both files in the same folder.

## Use

1. Start the injector:

```bat
ScumAudioDiagInjector.exe
```

2. Start SCUM by running `SCUM.exe` directly, not the BattlEye launcher.

Typical path:

```text
...\SteamLibrary\steamapps\common\SCUM\SCUM\Binaries\Win64\SCUM.exe
```

3. The injector waits for `SCUM.exe`, injects `ScumAudioDiag.dll`, prints `Injected.`, and exits.

If the game is already running, just run the injector and it should inject right away.

## Injector options

Use a custom DLL path:

```bat
ScumAudioDiagInjector.exe --dll C:\path\to\ScumAudioDiag.dll
```

Wait for a limited time:

```bat
ScumAudioDiagInjector.exe --timeout 30
```

Only try once:

```bat
ScumAudioDiagInjector.exe --once
```

## Notes

The DLL checks the bytes at the two target offsets before patching. If SCUM updates and those bytes change, the hook for that spot will not install.

This is a narrow local workaround for one crash path, not a general SCUM patch.
