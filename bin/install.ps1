[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$serviceName = if ([string]::IsNullOrWhiteSpace($env:VMLOADER_SERVICE_NAME)) {
    'vmloader'
}
else {
    $env:VMLOADER_SERVICE_NAME
}
$sourcePath = if ([string]::IsNullOrWhiteSpace($env:VMLOADER_SOURCE_DRIVER_FILEPATH)) {
    Join-Path $PSScriptRoot 'VmLoader.sys'
}
else {
    $env:VMLOADER_SOURCE_DRIVER_FILEPATH
}
$driverFileName = if ([string]::IsNullOrWhiteSpace($env:VMLOADER_DRIVER_FILENAME)) {
    'VmLoader.sys'
}
else {
    $env:VMLOADER_DRIVER_FILENAME
}

if ([System.IO.Path]::GetFileName($driverFileName) -cne $driverFileName) {
    throw 'VMLOADER_DRIVER_FILENAME must be a file name without a path.'
}

if ($serviceName -notmatch '^[A-Za-z0-9_.-]+$') {
    throw 'VMLOADER_SERVICE_NAME must contain only letters, numbers, periods, underscores, or hyphens.'
}

$destinationPath = Join-Path "$env:SystemRoot\System32\drivers" $driverFileName
$nativeDestinationPath = "\??\$destinationPath"

if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "Driver file is missing: $sourcePath"
}

$currentIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$currentPrincipal = [Security.Principal.WindowsPrincipal]::new($currentIdentity)
if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host 'Requesting administrator privileges...'
    $elevatedProcess = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
        -ArgumentList '-NoProfile', '-File', "`"$PSCommandPath`"" `
        -Verb RunAs `
        -Wait `
        -PassThru
    exit $elevatedProcess.ExitCode
}

& sc.exe query $serviceName *> $null
$serviceExists = $LASTEXITCODE -eq 0

if ($serviceExists) {
    & sc.exe stop $serviceName *> $null
}

Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force

if ($serviceExists) {
    & sc.exe config $serviceName binPath= $nativeDestinationPath type= kernel start= system
}
else {
    & sc.exe create $serviceName binPath= $nativeDestinationPath type= kernel start= system
}

if ($LASTEXITCODE -ne 0) {
    throw "Failed to configure the $serviceName service."
}

$dynDataDirectory = [System.IO.Path]::GetFullPath($PSScriptRoot)
$parametersRegistryPath = "HKLM:\SYSTEM\CurrentControlSet\Services\$serviceName\Parameters"
New-Item -Path $parametersRegistryPath -Force | Out-Null
New-ItemProperty -Path $parametersRegistryPath -Name 'DynDataDirectory' -PropertyType String -Value $dynDataDirectory -Force | Out-Null

& sc.exe start $serviceName
if ($LASTEXITCODE -ne 0) {
    throw "Failed to load the $serviceName driver."
}

Write-Host "Installed and loaded $serviceName from $destinationPath"
