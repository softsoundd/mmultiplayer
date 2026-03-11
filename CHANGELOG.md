# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.5.0] - 2025-12-18

### Added

- Improve Softtimer support
- Add support for ReShade, DXVK, etc.

### Fixed

- Fixed crashing when toggling Softimer
- Fixed crashes and deadlocks

## [2.4.0] - 2025-06-05

### Added

- Added Support for Softimer. Thanks to @CodeNameMeteor!
- Add Save/Load option for recordings and dolly paths. Recordings are saved in `%AppData%`. Thanks to @AdyStudios!
  - **Known Issue:** You have to load recordings before the map.
  - **Known Issue:** When loading a recording the camera rotation is messed up until changed.
- Add a toggle for the camera path display
- Add Dolly keybinds for: Record, Marker, Jump Frame, Play, Pause , FoV and Roll

### Fixed

- Fix crash when removing the hotkey for "Sidestep Beamer Left/Right"

### Changed

- Internal code cleanup. Thanks to @Toyro98 and @Jvp2001 for the help!
- Update the link in the error message to reflect new repository ownership
- Removed x64 configurations because the game is in 32bit and there's no need for 64bit configs

## [2.3.2] - 2024-08-02

### Fixed

- The file mmultiplayer-settings.json is now stored in `%appdata%\MMultiplayer` as previous method didn't work with OneDrive

## [2.3.1] - 2024-07-21

### Fixed

- The file mmultiplayer-settings.json is now stored in `%userprofile%\Documents\EA Games\Mirror's Edge`
- Seeding wasn't setup correctly in chaos
- Toggling chaos effects was using the wrong function and now works as expected

## [2.3.0] - 2024-07-19

### Added

- Chaos tab

### Fixed

- The file mmultiplayer.settings are no longer stored in temp folder and now persists where the game's exe is
- The size and position of the menu is now properly setup when you play on new version for the first time
- Debug console can now be closed without it closing the game (when closed down correctly)

### Changed

- Renamed mmultiplayer.settings to mmultiplayer-settings.json
- Multiplayer menu now shows at startup which can be turned off

## [2.2.0] - 2024-03-25

### Added

- Customizable speedometer with way more options than ever before
- You can now hide the overlay again in trainer which was removed in 2.1.0
- Misc tab
  - Disable auto lockon on AI
  - Color Scale
  - Tunnel Vision

### Fixed

- Faith being unable to move after teleporting in a few movement states
- Being unable to load or unload sublevels if names were similar and if you rapidly loaded and unloaded sublevels
- Being unable to wallrun after climbing a pipe and teleporting away from it

### Changed

- Updated Dear ImGui to 1.90.2 from 1.73
- Updated Nlohmann Json to 3.11.2 from 3.7.3
- The default style of the speedometer
- Settings now supporting multiple keys instead of one (not compatible with older versions)

### Removed

- 4 selectable models added in 2.1.0 (caused mmultiplayer to crash)

## [2.1.3] - 2023-10-14

### Added

- 2 more stuff to the speedometer in the "Show Extra Info"
  - SZ = Stored Z of LastJumpLocation.Z
  - SZD = Delta of Location.Z - LastJumpLocation.Z

### Fixed

- Faith being unable to move after teleporting while grabbing or rolling

## [2.1.2] - 2023-07-16

### Fixed

- Installer broken error handling (add additional logging)
- Windows 11 Defender support

## [2.1.1] - 2023-07-15

### Added

- Troubleshooting section in CONTRIBUTING.md

### Changed

- User can no longer configure install directory. Install is set to default install location (C:\Program Files\Mirror's Edge Multiplayer)

### Removed

- Swat Sniper and Riot Cop as selectable models (both caused crash during loading)

### Fixed

- Fixed installer not creating a Windows Security exclusion

## [2.1.0] - 2023-07-12

### Added

- Tag Addon
- Misc Addon
  - Autoroll
  - No consequtive wallruns limit
  - No wallrun challenge
  - No wallclimb challenge
  - No health regeneration
  - Permanent reaction time
  - Permanent game speed
  - One hit knock down
- Sidestepbeamer
- Reset button next to bindable keys
- Button to reset the trainer save and checkpoint
- Button to refill reaction time energy
- Toggable option to show your top height in player info
- 4 more selectable models
  - Ropeburn
  - Riot Cop
  - Swat
  - Swat Sniper

### Fixed

- Creating a save and reloading level and teleporting would cause faith to autoroll
- Creating a save no longer saves bools about the input (elevator state)
- Dying and teleporting showed a black screen
- Arms not being reset to default postion when teleporting
- Sound not being reset when teleporting during reaction time
- Reaction time energy not being saved using trainer
- Teleporting while sliding didn't stop the sliding sound
  
### Changed

- Styling in tabs to add more space between stuff (except dolly tab)  
- Flying speed to reset after being toggled
- Player info to be more like speedometer
- Default flying speed to be 2.0f instead of 3.0f

### Removed

- Player tab
- Option to toggle the overlay in trainer
