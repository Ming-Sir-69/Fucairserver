Fucairserver 0.2.2 Preview provides a Windows x64 installer and portable package
for AirPlay mirroring, Mac extended desktop and audio reception.

- Native install/uninstall entry, Start menu and optional desktop shortcut.
- Local-subnet firewall rules restricted to private and domain networks.
- Graceful upgrade while running, stable application identity, persistent settings and pairing.
- Recording defaults to the user's Videos folder; the app does not write layout files into Program Files.
- Source and runtime dependencies are fixed by revision/hash; corresponding source, a CycloneDX SBOM and SHA-256 checksums accompany the binaries.

This release is accepted on Windows 11 x64. Windows 10 22H2 x64 remains a
compatibility target and has not been tested on a Windows 10 desktop.
CI runners additionally exercise Windows Server installer lifecycle.

This preview is unsigned by an identity-validated publisher certificate. Public
packages omit the workstation's private icon and use the standard Windows
application icon. Miracast reception remains experimental; Google Cast and
multi-view are not supported.

Install the `*-setup.exe` asset, or extract the Windows ZIP. Update by running
the new installer. Uninstalling preserves settings, pairing identity and recordings.
