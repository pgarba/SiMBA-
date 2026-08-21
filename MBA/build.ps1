# Build the standalone MBA (GAMBA native port) core + mba_cli with MSVC.
#
# The MBA core uses llvm::APInt (128-bit) for the Node constant field, so it
# needs the LLVM headers (c:\libs\include) and links LLVMSupport (+ ntdll for a
# Windows symbol pulled in by ErrorHandling). No other LLVM component is needed.
#
# Usage:  powershell -File MBA\build.ps1
param(
    [string]$VC = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
    [string]$LLVMInc = "c:\libs\include",
    [string]$LLVMSupport = "c:\libs\lib\LLVMSupport.lib",
    [string]$Ntdll = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64\ntdll.lib",
    [string]$Config = "x64"
)

$ErrorActionPreference = "Stop"
$mba = $PSScriptRoot
$inc = Join-Path $PSScriptRoot "..\include"   # repo include/ (splitmix64.h)
$out = Join-Path $mba "build"
New-Item -ItemType Directory -Force -Path $out | Out-Null

# Verify.cpp carries the fast-check / Z3-proof verification. Without MBA_HAS_Z3
# (not defined here) its proveEquivalent is a no-op, so no Z3 library is needed.
$sources = @("Node.cpp", "Parser.cpp", "Batch.cpp", "RefineA.cpp", "RefineB.cpp",
            "RefineC.cpp", "RefineD.cpp", "Expand.cpp", "Substitute.cpp", "Bitwise.cpp",
            "Implicant.cpp", "Dnf.cpp", "BitwiseFactory.cpp", "LinearSimplifier.cpp",
            "GeneralSimplifier.cpp", "Verify.cpp", "mba_cli.cpp") | ForEach-Object { Join-Path $mba $_ }

$cl = "cl /nologo /std:c++17 /EHsc /O2 /MD /I `"$mba`" /I `"$inc`" /I `"$LLVMInc`""
$cmd = "call `"$VC`" $Config >nul 2>&1 && $cl " + ($sources -join " ") +
       " /Fe`"$out\mba_cli.exe`" /Fo`"$out\\`" `"$LLVMSupport`" `"$Ntdll`""

Write-Host "Compiling MBA core with MSVC ($Config) ..."
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { Write-Error "compile failed"; exit 1 }
Write-Host "Built: $out\mba_cli.exe"
