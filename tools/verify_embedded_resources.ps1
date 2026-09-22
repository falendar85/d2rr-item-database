param([Parameter(Mandatory = $true)][string]$Dll)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ItemDatabaseResources {
    [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32", SetLastError = true)]
    public static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32", SetLastError = true)]
    public static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32")]
    public static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32")]
    public static extern bool FreeLibrary(IntPtr module);
}
'@

$module = [ItemDatabaseResources]::LoadLibraryEx((Resolve-Path -LiteralPath $Dll), [IntPtr]::Zero, 2)
if ($module -eq [IntPtr]::Zero) { throw 'Cannot open plugin DLL as a data file' }
try {
    foreach ($entry in @(@(2001, 'database.json'), @(2002, 'guides.json'))) {
        $resource = [ItemDatabaseResources]::FindResource($module, [IntPtr]$entry[0], [IntPtr]10)
        if ($resource -eq [IntPtr]::Zero) { throw "Missing embedded $($entry[1])" }
        $size = [ItemDatabaseResources]::SizeofResource($module, $resource)
        $loaded = [ItemDatabaseResources]::LoadResource($module, $resource)
        $address = [ItemDatabaseResources]::LockResource($loaded)
        $bytes = [byte[]]::new($size)
        [Runtime.InteropServices.Marshal]::Copy($address, $bytes, 0, $size)
        $expected = [IO.File]::ReadAllBytes((Join-Path $root "data/$($entry[1])"))
        $actualHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
        $expectedHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($expected))
        if ($actualHash -ne $expectedHash) { throw "Embedded $($entry[1]) does not match its source" }
        $json = [Text.Encoding]::UTF8.GetString($bytes) | ConvertFrom-Json
        Write-Output "Verified $($entry[1]): $size bytes, $($json.records.Count) records"
    }
}
finally {
    [ItemDatabaseResources]::FreeLibrary($module) | Out-Null
}
