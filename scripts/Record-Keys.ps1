[CmdletBinding()]
param(
    # Name for this recording, for example pc-keyboard or mac-keyboard.
    [Parameter(Mandatory)][string]$Label,
    [ValidateRange(5, 600)][int]$Seconds = 30,
    [string]$OutputDir
)
# Records every key event Windows sees for a short time: from this PC's own
# keyboard (phys) and from a Mac through edge control (inj). Used to compare
# keyboards and systems key by key. Runs only while started by hand.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class KeyProbe {
    [StructLayout(LayoutKind.Sequential)]
    struct KBDLLHOOKSTRUCT { public uint vkCode, scanCode, flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)]
    struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam, lParam; public uint time; public int x, y; }
    delegate IntPtr Proc(int code, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern IntPtr SetWindowsHookEx(int id, Proc proc, IntPtr mod, uint thread);
    [DllImport("user32.dll")] static extern bool UnhookWindowsHookEx(IntPtr hook);
    [DllImport("user32.dll")] static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool PeekMessage(out MSG msg, IntPtr hwnd, uint min, uint max, uint remove);
    [DllImport("user32.dll")] static extern int GetKeyNameText(int lParam, StringBuilder name, int size);
    [DllImport("kernel32.dll")] static extern IntPtr GetModuleHandle(string name);

    static readonly List<string> rows = new List<string>();
    static Proc keep;

    static IntPtr Hook(int code, IntPtr wParam, IntPtr lParam) {
        if (code >= 0) {
            var k = Marshal.PtrToStructure<KBDLLHOOKSTRUCT>(lParam);
            int msg = wParam.ToInt32();
            bool down = msg == 0x100 || msg == 0x104;
            bool ext = (k.flags & 1) != 0, injected = (k.flags & 0x10) != 0;
            var name = new StringBuilder(64);
            GetKeyNameText((int)((k.scanCode << 16) | (ext ? 1u << 24 : 0)), name, 64);
            rows.Add(string.Format("{0}\t{1}\t{2:X2}\t{3:X2}\t{4}\t{5}\t{6}", DateTime.Now.ToString("HH:mm:ss.fff"),
                down ? "down" : "up", k.vkCode, k.scanCode, ext ? "ext" : "", injected ? "inj" : "phys", name));
        }
        return CallNextHookEx(IntPtr.Zero, code, wParam, lParam);
    }

    public static List<string> Record(int seconds) {
        keep = Hook;
        IntPtr hook = SetWindowsHookEx(13, keep, GetModuleHandle(null), 0);
        var until = DateTime.Now.AddSeconds(seconds);
        MSG m;
        while (DateTime.Now < until) {
            while (PeekMessage(out m, IntPtr.Zero, 0, 0, 1)) { }
            System.Threading.Thread.Sleep(5);
        }
        UnhookWindowsHookEx(hook);
        return rows;
    }
}
'@
if (-not $OutputDir) { $OutputDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'build/keyprobe' }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$file = Join-Path $OutputDir ("pc-{0}.tsv" -f $Label)
Write-Host "Recording for $Seconds s. Press the keys now..."
$rows = [KeyProbe]::Record($Seconds)
@("time`tevent`tvk`tscan`text`tsource`tname") + $rows | Set-Content -Encoding utf8 -LiteralPath $file
Write-Host "$($rows.Count) events saved to $file"
