@echo off
set /p CAMERA_HOST=Camera address [talos-camera.local]: 
if "%CAMERA_HOST%"=="" set CAMERA_HOST=talos-camera.local
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-camera-stream.ps1" -CameraHost "%CAMERA_HOST%"
pause
