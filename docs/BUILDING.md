# Build and release

The receiver is GPL-3.0-only. Upstream revisions are fixed in
`profiles/source-lock.json`; `scripts/patches/native-receiver.patch` contains
the product changes to AirPlay-Windows. Keep all notices and corresponding
source when distributing binaries. A release source bundle contains this
repository, the pinned upstream trees and their build instructions. Runtime
library source archives are published alongside the installers.

Use Windows x64, PowerShell 7.4+, Git, Node.js 22+ and the official MSYS2 base.
Do not use an MSVC, MINGW64 or 32-bit package set with the UCRT64 binary lock.
No Visual Studio runtime, Bonjour installation or original AirServer install
is required by end users.

```powershell
$env:FUCAIRSERVER_MSYS_ROOT = 'C:\msys64'
# Optional process-local HTTP proxy, if your network needs one:
# $env:FUCAIRSERVER_PROXY = 'http://127.0.0.1:7897'
pwsh -NoProfile -File scripts/Initialize-Build.ps1 -InstallPackages -IncludeMiracast -IncludeInstaller
pwsh -NoProfile -File scripts/Build.ps1 -PublicBuild
pwsh -NoProfile -File scripts/Build-Miracast.ps1 -PublicBuild
```

MSYS2 packages, Inno Setup and C++/WinRT downloads are SHA-256 pinned.
The installer compiler's Authenticode signature is checked as well.
`Initialize-Build.ps1` validates all 147 UCRT64 package versions and restores
the exact source revisions. It does not change machine-wide proxy settings.
Omit `-PublicBuild` only for a private local build that may use the private icon.
Build the local and public configurations sequentially because resource version
headers are generated from `VERSION`.

```powershell
. scripts/Set-BuildEnvironment.ps1
& "$env:FUCAIRSERVER_MSYS_ROOT\ucrt64\bin\ctest.exe" --test-dir build/native-public --output-on-failure
node scripts/Package.mjs dist/packages/release-candidate --public
pwsh -NoProfile -File scripts/Build-Installer.ps1 -PayloadDir "$PWD\dist\packages\release-candidate"
pwsh -NoProfile -File scripts/Get-SourceArchives.ps1
```

Public packaging rejects private artwork and any original AirServer engine
identified by filename, complete binary hash or code-section hash. The packager
walks normal and delayed PE imports and collects only required runtime DLLs.
It fails on unresolved imports or unexpected architectures and writes a
CycloneDX 1.6 SBOM plus a version and binary hash manifest into the package.

`Get-SourceArchives.ps1` verifies every runtime source against the committed
source hash lock. Maintainers updating dependencies first run
`Update-DependencyLock.mjs`, then `Get-SourceArchives.ps1 -UpdateLock`; the latter
requires valid signatures from the trusted MSYS2 pacman keyring before it can
update hashes. Never bypass a hash or signature failure.

The installer uses a stable AppId and per-machine x64 installation. It supports
interactive and silent installation, Restart Manager, graceful receiver exit,
same-version repair and upgrade, and rejects downgrades. The application runs
as the invoking user; administrator rights are used for installation and
firewall changes. User settings, pairing identity and recordings survive
upgrades and uninstalls. The installer does not request a reboot or enable
startup automatically. Its firewall rules apply only to its own executable,
the local subnet and private/domain profiles. Public-network exceptions require
an explicit action in the receiver.

```powershell
# Only on a clean test machine: installs and uninstalls its own isolated app.
pwsh -NoProfile -File scripts/Test-Installer.ps1 -Installer path\to\setup.exe
```

Acceptance uses a fresh directory with Chinese characters and spaces, a clean
runtime PATH and an isolated profile. It checks actual RTSP responses and
process ownership, second-instance activation, persisted Unicode names,
pairing identity, an upgrade while running, Windows Apps registration,
shortcuts, scoped network rules and uninstall cleanup. Test-generated files
remain under ignored `build/`. The opt-in `FUCAIRSERVER_DATA_DIR` environment
variable selects an absolute portable profile directory; normal launches keep
using the current user's AppData.
The Start/Stop helpers prefer the registered installed application; use
`-WorkspaceBuild` to select the workspace's portable build for maintenance.

GitHub-hosted Windows Server runners exercise builds and installer lifecycle;
they do not establish real Windows 10/11 desktop, Wi-Fi driver or sender-device
coverage. Complete the desktop matrix in `docs/COMPATIBILITY.md` before
promoting a release from preview to stable.

For Authenticode, use an identity-validated signing certificate or Microsoft's
Artifact Signing service. Sign the app executables before packaging and the
installer after compilation using SHA-256 and an RFC 3161 timestamp, then
regenerate hashes. Certificates, private keys and passwords must stay outside
the repository. Unsigned builds remain clearly marked preview releases;
checksums and GitHub provenance do not replace a publisher certificate.
