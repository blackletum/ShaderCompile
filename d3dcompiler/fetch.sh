#!/usr/bin/env bash
# Where the committed d3dcompiler_47.dll came from, as a script that re-derives it.
# The build never runs this, the DLL is in the tree and SHA256SUMS pins it, this is the provenance.
# The CMakeLists.txt one level up compiles it into the binary.
# The root .dockerignore drops every binary under third_party/ except this one file, named in full there
#
# Microsoft does not host the DLL on its own, it ships inside the Windows SDK and inside every Firefox,
# and the Firefox 62.0.3 win64 installer is where winetricks takes it from, so the committed copy is that file,
# the same one the wine route used.
# Both the installer and the extracted DLL are pinned by SHA-256.
# THE TRAP: the hashes are the contract, not the URL.
# A different build of the DLL is a different compiler and changes every shader's bytecode,
# so if Mozilla ever moves the file, point the URL elsewhere and keep the hashes,
# and if the DLL ever has to change, update SHA256SUMS and recompile and re-check every shader in game.
#
# Needs curl and 7z, the installer is a 7-Zip self-extractor.
#
# USAGE
#     d3dcompiler/fetch.sh
#
# Runs from anywhere, leaves a matching d3dcompiler_47.dll alone and replaces a missing or mismatching one

set -euo pipefail
cd "$(dirname "$0")"

INSTALLER_URL="https://download-installer.cdn.mozilla.net/pub/firefox/releases/62.0.3/win64/ach/Firefox%20Setup%2062.0.3.exe"
INSTALLER_SHA256="721977f36c008af2b637aedd3f1b529f3cfed6feb10f68ebe17469acb1934986"
DLL_SHA256="0427b16ffb7d2f6f2d611cc9f0520f42c70bcbfbdc2fc0d47e8735fd1c79a308"

if [ -f d3dcompiler_47.dll ] && echo "$DLL_SHA256  d3dcompiler_47.dll" | sha256sum --check --status; then
    echo "d3dcompiler_47.dll already present and matches"
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "downloading the Firefox 62.0.3 installer"
curl --fail --silent --show-error --location --output "$work/firefox.exe" "$INSTALLER_URL"
echo "$INSTALLER_SHA256  $work/firefox.exe" | sha256sum --check --status || {
    echo "FATAL: installer hash mismatch, refusing to extract" >&2
    exit 1
}

7z e -y -o"$work" "$work/firefox.exe" core/d3dcompiler_47.dll > /dev/null
echo "$DLL_SHA256  $work/d3dcompiler_47.dll" | sha256sum --check --status || {
    echo "FATAL: extracted d3dcompiler_47.dll hash mismatch" >&2
    exit 1
}

mv "$work/d3dcompiler_47.dll" d3dcompiler_47.dll
echo "d3dcompiler_47.dll 10.0.15063.674 fetched and verified"
