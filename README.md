# SCUM menu crash patch

local patch for crash I hit when launching the game.

This is not meant for online play. Do not use it with BattlEye. It is only for launching the game directly through `SCUM.exe` ( you can append -dx12 if u need it ) with BattlEye disabled/not running.

## What it fixes

notes:

- `SCUM.exe+0x4B16410`: XAudio bad ptr
- `SCUM.exe+0x42DC090`: nullptr access multiple times `SCUM.exe+0x42DC09A`.

The dll patches those spots in memory. If the risky pointer is missing, it returns a safe failure/empty result instead of letting the game crash.

dll just patches those two places and stays quiet.

## Important

This cannot be used while playing online.

Use it only like this:

- BattlEye is not running.
- The game is started by running `SCUM.exe` directly.

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

The dll checks the bytes at the two target offsets before patching. If SCUM updates and those bytes change, the hook for that spot will not install.

This is a narrow local workaround for one crash path, not a general SCUM patch.
