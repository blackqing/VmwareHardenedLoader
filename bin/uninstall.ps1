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

$driverFileName = if ([string]::IsNullOrWhiteSpace($env:VMLOADER_DRIVER_FILENAME)) {
    'vmloader.sys'
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
    $stopExitCode = $LASTEXITCODE
    if ($stopExitCode -notin @(0, 1062)) {
        throw "Failed to stop the $serviceName driver."
    }

    $stopDeadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $queryOutput = & sc.exe query $serviceName 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to query the $serviceName driver state."
        }

        $stateLine = $queryOutput | Where-Object { $_ -match '^\s*STATE\s*:\s*\d+\s+(\S+)' } | Select-Object -First 1
        if ($null -eq $stateLine) {
            throw "Unable to determine the $serviceName driver state."
        }

        if ($stateLine -match '^\s*STATE\s*:\s*\d+\s+STOPPED') {
            break
        }

        if ([DateTime]::UtcNow -ge $stopDeadline) {
            throw "Timed out waiting for the $serviceName driver to stop."
        }

        Start-Sleep -Seconds 1
    } while ($true)

    & sc.exe delete $serviceName
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to remove the $serviceName service."
    }
}

$removedDriverFile = $false
if (Test-Path -LiteralPath $destinationPath -PathType Leaf) {
    Remove-Item -LiteralPath $destinationPath -Force
    $removedDriverFile = $true
}

if ($serviceExists -or $removedDriverFile) {
    Write-Host "Uninstalled $serviceName and removed $destinationPath"
}
else {
    Write-Host "$serviceName is already uninstalled."
}
