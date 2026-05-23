#!/bin/bash

# Example
# $ VERSION=0.0.1 ./package.sh

if [[ -z "${VERSION}" ]]
then
    echo 'the VERSION environment variable must be set'
    exit 1
fi

set -eux

buildDir='build'
go run resources/installer/xor/main.go Release/Launcher.exe > resources/installer/squibbles/launcher.buh
go run resources/installer/xor/main.go Release/mmultiplayer.dll > resources/installer/squibbles/dll.buh
go build -o ${buildDir}/app/squibbles.exe resources/installer/squibbles/main.go

cp resources/installer/installer.iss "${buildDir}/"

cp -r resources/installer/images "${buildDir}/"

# TODO: Add license for Windows installer.
# '/DLicenseFileOverride=license_en_US.txt'
innosetupArgs=$(cat <<-END
/DApplicationFilesPath=app
/DOutputPath=.
/DAppNameOverride=Mirror's Edge Multiplayer
/DAppPublisherOverride=Buh
/DAppURLOverride=https://github.com/softsoundd/mmultiplayer
/DAppExeNameOverride=Launcher.exe
/DAppVersionOverride=${VERSION}
/DOutputBaseFilenameOverride=mirrors-edge-multiplayer-${VERSION}
installer.iss
END
)

innosetupArgs="\"${innosetupArgs//$'\n'/\" \"}\""

batFilePath="${buildDir}/package.bat"
echo "ISCC.exe ${innosetupArgs}" > "${batFilePath}"
(cd "${buildDir}" && cmd //c "${batFilePath##*/}")
