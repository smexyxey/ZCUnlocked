# ZCUnlocked

**Mandalorian & Padawan Heroes + Mando Wardrobe for Star Wars: Zero Company**

Zero Company ships a pile of hero gear and armor that's fenced off to its story
characters. ZCUnlocked hands three slices of it to your own people.

A [UE4SS](https://www.nexusmods.com/starwarszerocompany/mods/9) C++ mod.

## What it unlocks

- **Padawan recruit** - make a regular humanoid a Padawan: Tel-Rea's class, spec,
  talent and lightsaber.
- **Mandalorian recruit** - or a Mandalorian: Cly's class, the Warrior spec, the
  jetpack talent and a blaster kit.
- **Mandalorian wardrobe** - the Man001, Man002 and Cly armor sets drop into the
  human dress-up screen as normal, color-tweakable pieces - helms, chest, arms,
  legs, boots.
- **Freely swappable** - the game normally bolts a hero's specialization, talent
  and weapon in place for good (try changing a real Padawan's lightsaber - you
  can't). Here they stay changeable like a normal recruit's, so you can put a kit
  on and take it back off whenever you like.

## How it behaves

Nothing gets brute-forced. To open a part up it watches the game's own
eligibility checks and, when one of these specific parts is turned away,
quietly gives that character the missing identity tag and asks the game to
look again - the game still makes the call. To keep things swappable it lifts
only the "this can't be changed" lock the game stamps on hero specs, talents
and weapons. Everything else stays as the game intends, and there's no
console, no keybinds, and nothing that touches your saves.

## What you'll need

- Zero Company on Steam, build **24874058** - it's pinned to that build, so on
  anything else it simply does nothing rather than misbehaving.
- [UE4SS for Zero Company](https://www.nexusmods.com/starwarszerocompany/mods/9).

## Building from source

```
compile.bat
```

Requires the MSVC Build Tools (the script pulls in `vcvars64.bat`). Produces
`main.dll`. Drop it into `<game>\SWZeroCompany\Binaries\Win64\ue4ss\Mods\ZCUnlocked\dlls\main.dll`
alongside an empty `enabled.txt` in the `ZCUnlocked` folder to turn the mod on.

## Running on Linux (Steam Proton)

There's no Linux build to make - this is a Windows `main.dll` and UE4SS loads it
inside the game process. Under Proton the game and UE4SS are already running as
Windows code in a Wine prefix, so the same `main.dll` loads there natively. The
only Linux-specific part is getting UE4SS itself to inject.

1. **Game + UE4SS first.** Install Zero Company, force a Proton version under the
   game's Properties -> Compatibility, and launch once so the prefix is built.
   Then install [UE4SS](https://www.nexusmods.com/starwarszerocompany/mods/9)
   with its normal Windows steps - same files, same relative paths.
2. **Let the UE4SS loader load.** UE4SS ships as a proxy DLL (often `dwmapi.dll`,
   sometimes `xinput1_3.dll` / `d3d11.dll` / `dinput8.dll` - check which one
   yours uses). Wine won't prefer a dropped-in DLL unless told to, so add a DLL
   override in the game's Steam launch options, swapping in your proxy's name:

   ```
   WINEDLLOVERRIDES="dwmapi=n,b" %command%
   ```

   (`protontricks <appid>` can set the same override instead.)
3. **Drop the mod in** at the same relative path as on Windows - forward slashes,
   same structure:

   ```
   <game>/SWZeroCompany/Binaries/Win64/ue4ss/Mods/ZCUnlocked/dlls/main.dll
   ```

   plus an empty `enabled.txt` in the `ZCUnlocked` folder. The game usually lives
   under `~/.steam/steam/steamapps/common/SWZeroCompany/` (or another library or
   SD-card path on a Steam Deck).
4. **Get a Windows `main.dll`.** `compile.bat` is MSVC-only, so build on Windows,
   reuse a prebuilt `main.dll`, or cross-compile on Linux with MinGW-w64:

   ```
   x86_64-w64-mingw32-g++ -shared -std=c++20 -O2 -static -o main.dll dllmain.cpp
   ```

   Expect to iron out a few MSVC-isms (some `<intrin.h>` intrinsics, RTTI-off) if
   MinGW complains; `clang-cl` targeting `x86_64-pc-windows-msvc` is the closest
   match to `compile.bat`.
5. **Check it took.** Launch through Steam and read the one-line note at
   `.../ue4ss/Mods/ZCUnlocked/dlls/ZCUnlocked.log`.

The same build pin applies (24874058), and if nothing shows up in-game it's
almost always UE4SS injection under Proton, not the mod - back up your save
either way.

## Finding it in-game

- Armor sits in the normal outfit screen for human characters and colors like
  anything else.
- The Padawan and Mandalorian classes show up as options while you're building
  a humanoid.
- Man001/Man002 body armor works on any human; Cly's own beskar and helmets
  only appear on a female "Cly" character.

## Heads-up

- Pinned to build 24874058 - a game patch will mean a rebuild.
- It leaves a one-line worked/didn't note at
  `...\ue4ss\Mods\ZCUnlocked\dlls\ZCUnlocked.log` if you ever want to check.
- Back up your save before adding or removing it.

## Thanks

This stands on two earlier mods, and credit's due:

- **Sternab** - figured out the Mandalorian wardrobe side
  ([ZeroCompanyMandoWardrobe](https://github.com/Sternab/ZeroCompanyMandoWardrobe)).
- **ItsNotM3** - the recruitable-hero idea.

---

Unofficial fan project, not tied to Bit Reactor, EA, Lucasfilm, Disney, or
UE4SS. Star Wars and everything around it belongs to its owners.
