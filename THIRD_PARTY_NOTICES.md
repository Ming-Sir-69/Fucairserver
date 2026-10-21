# Fucairserver source build

Fucairserver is an independent receiver built from the open-source projects
listed in `profiles/source-lock.json` and the source in this workspace. It is
not recovered AirServer source. Eric Mingle 69 / REA are customization credits.

The AirPlay executable is based on AirPlay-Windows (GPL-3.0), which uses code
and protocol implementations from UxPlay, RPiPlay and playfair. The original
upstream notices, including the origin and rights limitations of the bundled
FairPlay handshake byte sequences, are preserved alongside this file. Dear
ImGui is MIT licensed. The Miracast host uses Windows APIs and its own source;
its separate source notices list the MIT reference sample.

This Windows package uses the MSYS2 UCRT64 FFmpeg build **9.0.2-1**, whose
package metadata declares **GPL-3.0-or-later**. The upstream AirPlay-Windows
notice describes its default LGPL-only FFmpeg configuration; that description
does not describe the FFmpeg binary included here. The actual runtime package
versions, declared licenses and copied license files are recorded in
`licenses/runtime-packages.json`. The complete local source workspace contains
the receiver source, fixed revisions, build scripts and customization patch.

The private workstation icon originated from the user's local AirServer
installation. It is excluded from this repository, public source archives and
public binaries. Public builds use the standard Windows application icon until
a separately licensed product icon is available. No proprietary AirServer
executable or receiver DLL is included. No endorsement by AirServer or Apple
is claimed.
