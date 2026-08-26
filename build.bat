@echo off
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "Game.sln" /p:Configuration=Release /p:Platform=Win32 /t:Build /v:minimal
