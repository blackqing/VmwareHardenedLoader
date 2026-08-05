[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$certificateSubject = 'CN=VmLoader Test Signing'
$certificateFriendlyName = 'VmLoader Test Signing Certificate'
$driverPath = Join-Path $PSScriptRoot 'VmLoader.sys'
$codeSigningEkuOid = '1.3.6.1.5.5.7.3.3'

function Test-Administrator {
    $currentIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $currentPrincipal = [Security.Principal.WindowsPrincipal]::new($currentIdentity)
    return $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-CodeSigningCertificate {
    param(
        [Parameter(Mandatory)]
        [System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate
    )

    if (-not $Certificate.HasPrivateKey -or
        $Certificate.NotBefore -gt [DateTime]::Now -or
        $Certificate.NotAfter -le [DateTime]::Now.AddDays(30)) {
        return $false
    }

    $enhancedKeyUsage = $Certificate.Extensions |
        Where-Object { $_.Oid.Value -eq '2.5.29.37' } |
        Select-Object -First 1

    if ($null -eq $enhancedKeyUsage) {
        return $false
    }

    return $null -ne ($enhancedKeyUsage.EnhancedKeyUsages |
        Where-Object { $_.Value -eq $codeSigningEkuOid } |
        Select-Object -First 1)
}

function Get-TestSigningCertificate {
    $certificate = Get-ChildItem -Path 'Cert:\LocalMachine\My' |
        Where-Object {
            $_.Subject -eq $certificateSubject -and
            (Test-CodeSigningCertificate -Certificate $_)
        } |
        Sort-Object -Property NotAfter -Descending |
        Select-Object -First 1

    if ($null -ne $certificate) {
        Write-Host "Using existing test-signing certificate: $($certificate.Thumbprint)"
        return $certificate
    }

    Write-Host 'Creating a VmLoader test-signing certificate...'
    $certificate = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $certificateSubject `
        -FriendlyName $certificateFriendlyName `
        -CertStoreLocation 'Cert:\LocalMachine\My' `
        -KeyAlgorithm RSA `
        -KeyLength 3072 `
        -HashAlgorithm SHA256 `
        -KeyExportPolicy NonExportable `
        -NotAfter ([DateTime]::Now.AddYears(5))

    Write-Host "Created test-signing certificate: $($certificate.Thumbprint)"
    return $certificate
}

function Install-CertificateTrust {
    param(
        [Parameter(Mandatory)]
        [System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate
    )

    $missingStores = @()
    foreach ($storeName in @('Root', 'TrustedPublisher')) {
        $storePath = "Cert:\LocalMachine\$storeName"
        $trustedCertificate = Get-ChildItem -Path $storePath |
            Where-Object { $_.Thumbprint -eq $Certificate.Thumbprint } |
            Select-Object -First 1

        if ($null -eq $trustedCertificate) {
            $missingStores += $storePath
        }
    }

    if ($missingStores.Count -eq 0) {
        return
    }

    $temporaryCertificatePath = Join-Path ([System.IO.Path]::GetTempPath()) `
        ("VmLoader-{0}.cer" -f [Guid]::NewGuid().ToString('N'))

    try {
        Export-Certificate -Cert $Certificate -FilePath $temporaryCertificatePath -Force | Out-Null

        foreach ($storePath in $missingStores) {
            Import-Certificate -FilePath $temporaryCertificatePath -CertStoreLocation $storePath | Out-Null
            Write-Host "Trusted certificate in $storePath"
        }
    }
    finally {
        if (Test-Path -LiteralPath $temporaryCertificatePath -PathType Leaf) {
            Remove-Item -LiteralPath $temporaryCertificatePath -Force
        }
    }
}

function Find-SignTool {
    $signToolCommand = Get-Command -Name 'signtool.exe' -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $signToolCommand) {
        return $signToolCommand.Source
    }

    $candidatePaths = @()
    if (-not [string]::IsNullOrWhiteSpace($env:WindowsSdkVerBinPath)) {
        $candidatePaths += Join-Path $env:WindowsSdkVerBinPath 'x64\signtool.exe'
    }
    if (-not [string]::IsNullOrWhiteSpace($env:WindowsSdkBinPath)) {
        $candidatePaths += Join-Path $env:WindowsSdkBinPath 'x64\signtool.exe'
    }

    $programFilesX86 = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $sdkBinPath = Join-Path $programFilesX86 'Windows Kits\10\bin'
        if (Test-Path -LiteralPath $sdkBinPath -PathType Container) {
            $versionDirectories = Get-ChildItem -LiteralPath $sdkBinPath -Directory |
                Where-Object { $_.Name -match '^\d+(\.\d+){3}$' } |
                Sort-Object { [Version]$_.Name } -Descending

            foreach ($versionDirectory in $versionDirectories) {
                $candidatePaths += Join-Path $versionDirectory.FullName 'x64\signtool.exe'
            }
        }
    }

    foreach ($candidatePath in $candidatePaths) {
        if (Test-Path -LiteralPath $candidatePath -PathType Leaf) {
            return $candidatePath
        }
    }

    throw 'signtool.exe was not found. Install the Windows 10/11 SDK or run this script from a Visual Studio Developer PowerShell.'
}

if (-not (Test-Path -LiteralPath $driverPath -PathType Leaf)) {
    throw "Driver file is missing: $driverPath"
}

if (-not (Test-Administrator)) {
    Write-Host 'Requesting administrator privileges...'
    $elevatedProcess = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
        -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"" `
        -Verb RunAs `
        -Wait `
        -PassThru
    exit $elevatedProcess.ExitCode
}

$certificate = Get-TestSigningCertificate
Install-CertificateTrust -Certificate $certificate
$signToolPath = Find-SignTool

Write-Host "Signing $driverPath"
& $signToolPath sign /v /fd SHA256 /s My /sm /sha1 $certificate.Thumbprint $driverPath
if ($LASTEXITCODE -ne 0) {
    throw "signtool.exe failed to sign $driverPath"
}

& $signToolPath verify /v /pa $driverPath
if ($LASTEXITCODE -ne 0) {
    throw "signtool.exe could not verify the signature on $driverPath"
}

Write-Host "Test-signed and verified $driverPath"
Write-Warning 'Windows test-signing mode must be enabled before loading the driver. Run "bcdedit /set testsigning on" as administrator and restart Windows if needed.'
