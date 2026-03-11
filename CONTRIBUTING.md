# Contributing

## Building the Client and Launcher

- When working with VSCode, a build task is included
- Alternatively, run `./build.sh` using Git Bash. Requires DirectX June 2010 and MSBuild

## Building the Installer

- Run `VERSION=X.X.X ./package.sh` using Git Bash. Expects a version to be provided via an environment variable. Builds artifacts into `build/`. Requires Go and Inno Setup.

## Troubleshooting

- Installer logs are saved in `%localappdata%\Temp`. E.g., `Setup Log 2023-07-13 #010.txt` and `mirrors-edge-multiplayer-installer-helper-*.log`
- Run `Get-MpThreatDetection` in Windows PowerShell to get Windows Defender threat history to see if any of the multiplayer files were affected.
