# Compatibility and release acceptance

| Platform | Target | Current verification |
| --- | --- | --- |
| Windows 10 22H2, x64, build 19045 | Supported target | Desktop installation and sender tests required before stable |
| Windows 11 x64 | Current acceptance scope | Local installer lifecycle, source regressions and a fresh GitHub source rebuild passed |
| Windows Server 2022 / 2025 x64 | CI environment | Automated build, unit tests and installer lifecycle |
| Windows ARM64 or 32-bit | Outside this release | Installer refuses these architectures |

Do not treat a Windows Server runner or static import inspection as a test on
Windows 10. Windows 10 ended general support on October 14, 2025; ongoing OS
security servicing depends on Microsoft's applicable ESU/lifecycle terms.

The current release acceptance scope is Windows 11. Before extending verified
support to Windows 10, run the same installer acceptance on a Windows 10 22H2
desktop. For a stable release, also check installation cancel, UAC cancel, a standard user session,
offline installation, downgrade refusal, uninstall from Windows Settings,
high-DPI/multiple monitors, close-to-tray and Explorer restart.

On real devices, confirm AirPlay discovery, video plus audio, Mac extended
desktop, reconnect, recording to the user's Videos folder, sleep/resume and
network changes. The existing local receiver has already been confirmed by its
user to display Mac extended desktop with audio. Repeat those checks against
each candidate binary before extending that claim to a new release.

Miracast actual sender reception remains unverified. Google Cast and multi-view
are incomplete. These are excluded from supported-feature claims.
