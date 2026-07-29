@echo off
title VMware Hardened Loader Installation Script
:init
 setlocal DisableDelayedExpansion
 set cmdInvoke=1
 set winSysFolder=System32
 set "batchPath=%~dpnx0"
 rem this works also from cmd shell, other than %~0
 for %%k in (%0) do set batchName=%%~nk
 set "vbsGetPrivileges=%temp%\OEgetPriv_%batchName%.vbs"
 setlocal EnableDelayedExpansion

:checkPrivileges
  NET FILE 1>NUL 2>NUL
  if '%errorlevel%' == '0' ( goto gotPrivileges ) else ( goto getPrivileges )

:getPrivileges
  if '%1'=='ELEV' (echo ELEV & shift /1 & goto gotPrivileges)
  ECHO.
  ECHO **************************************
  ECHO Invoking UAC for Privilege Escalation
  ECHO **************************************

  ECHO Set UAC = CreateObject^("Shell.Application"^) > "%vbsGetPrivileges%"
  ECHO args = "ELEV " >> "%vbsGetPrivileges%"
  ECHO For Each strArg in WScript.Arguments >> "%vbsGetPrivileges%"
  ECHO args = args ^& strArg ^& " "  >> "%vbsGetPrivileges%"
  ECHO Next >> "%vbsGetPrivileges%"
  
  if '%cmdInvoke%'=='1' goto InvokeCmd 

  ECHO UAC.ShellExecute "!batchPath!", args, "", "runas", 1 >> "%vbsGetPrivileges%"
  goto ExecElevation

:InvokeCmd
  ECHO args = "/c """ + "!batchPath!" + """ " + args >> "%vbsGetPrivileges%"
  ECHO UAC.ShellExecute "%SystemRoot%\%winSysFolder%\cmd.exe", args, "", "runas", 1 >> "%vbsGetPrivileges%"

:ExecElevation
 "%SystemRoot%\%winSysFolder%\WScript.exe" "%vbsGetPrivileges%" %*
 exit /B

:gotPrivileges
 setlocal & cd /d %~dp0
 if '%1'=='ELEV' (del "%vbsGetPrivileges%" 1>nul 2>nul  &  shift /1)

 ::::::::::::::::::::::::::::
 ::START
 ::::::::::::::::::::::::::::

if not exist "%~dp0vmloader_resolver.exe" (
  echo vmloader_resolver.exe is missing.
  pause
  exit /B 1
)

sc query vmloader 1>nul 2>nul
if not errorlevel 1 sc stop vmloader 1>nul 2>nul

copy /Y "%~dp0vmloader.sys" "C:\vmloader.sys"
if errorlevel 1 goto installFailed

sc query vmloader 1>nul 2>nul
if errorlevel 1 (
  sc create vmloader binPath= "\??\c:\vmloader.sys" type= "kernel" start= "system"
) else (
  sc config vmloader binPath= "\??\c:\vmloader.sys" type= "kernel" start= "system"
)
if errorlevel 1 goto installFailed

"%~dp0vmloader_resolver.exe"
if errorlevel 1 (
  echo Kernel symbol resolution failed. The driver was not started.
  pause
  exit /B 1
)

sc start vmloader
if errorlevel 1 goto installFailed
reg delete "HKLM\HARDWARE\ACPI\DSDT\PTLTD_" /f
echo Press any key to restart...
pause > nul
shutdown -r -t 00 -f
exit /B 0

:installFailed
echo Installation failed. Review the error above.
pause
exit /B 1
