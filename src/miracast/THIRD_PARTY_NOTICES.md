# Source and notices

This independent Win32 receiver does not use AirServer executable code or proprietary libraries. The optional workstation-only icon is excluded from public builds.

The Miracast receive and MediaSource-to-MediaPlayer flow was adapted from:

- KevinKennedy/MiracastReceiverSample, commit `d214b8b5d7aca9cc6d8e4307aac4d10e1d332495`, https://github.com/KevinKennedy/MiracastReceiverSample
- The upstream source and full MIT license are retained in `../../third_party/miracast-reference/`.

MIT License

Copyright (c) 2022 Kevin Kennedy

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

C++/WinRT headers were generated using the uninstalled, portable Microsoft.Windows.CppWinRT `3.0.260818.1` NuGet tool and this machine's Windows WinMetadata. C++/WinRT is MIT licensed, Copyright (c) Microsoft Corporation. Its full license is retained at `../../third_party/miracast-reference/research/cppwinrt-tool/LICENSE`.

The small desktop COM ABI declarations in `platform_interop.h` follow Microsoft's SDK declarations:

- https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/winrt/windows.ui.composition.interop.h
- https://learn.microsoft.com/en-us/windows/win32/api/dispatcherqueue/ns-dispatcherqueue-dispatcherqueueoptions
- https://learn.microsoft.com/en-us/windows/win32/api/dispatcherqueue/nf-dispatcherqueue-createdispatcherqueuecontroller

No code was copied from the GPL ImmersiveMiracast project; it was inspected only as an additional example of desktop `CreateSession(null)` use. This component's adaptation and dependencies therefore carry the MIT notices above; the overall distribution must also retain the separate AirPlay engine's notices.
