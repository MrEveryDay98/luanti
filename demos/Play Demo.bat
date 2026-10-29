@echo off
rem Plays a VoxHack demo back. Drop a .nhvdemo file from this folder onto
rem this to play it; run it on its own to play the newest one here. Demos
rem are recorded with Options > Record demos (each game, from its start).
rem Space pauses, W and S speed it up and slow it down, Esc leaves.
rem A demo plays only in the version of VoxHack that recorded it.
setlocal
rem (this folder is the game's demos folder: the game is the one above it)
set "ENGINE=%~dp0.."
set "DEMO=%~1"
if "%DEMO%"=="" (
	for /f "delims=" %%f in ('dir /b /o-d "%~dp0*.nhvdemo" 2^>nul') do (
		if not defined DEMO set "DEMO=%~dp0%%f"
	)
)
if "%DEMO%"=="" (
	echo No demos recorded yet. Turn on Options ^> Record demos and play a game.
	pause
	exit /b 1
)
cd /d "%ENGINE%"
start "" bin\luanti.exe --go --nhv-demo "%DEMO%"
