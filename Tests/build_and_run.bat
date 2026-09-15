@echo off
rem Offline impulse-response test for the delay core. Not part of the plugin build.
rem Build the solution in VS first (it links against the Shared Code static lib),
rem then run this from anywhere.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
set ROOT=%~dp0..
set JUCE=C:\Users\Berse\OneDrive\Desktop\JUCE\modules
set LIB_PATH=%ROOT%\Builds\VisualStudio2026\x64\Debug\Shared Code\SquillysDelay.lib
if not exist "%LIB_PATH%" (
  echo Shared Code lib not found - build the solution in VS first.
  exit /b 1
)
cl /nologo /std:c++17 /EHsc /MDd /Zi /W3 /wd4100 ^
  /D_CRT_SECURE_NO_WARNINGS /DWIN32 /D_WINDOWS /DDEBUG /D_DEBUG ^
  /DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 /DJUCE_MODULE_AVAILABLE_juce_audio_devices=1 /DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 ^
  /DJUCE_MODULE_AVAILABLE_juce_audio_plugin_client=1 /DJUCE_MODULE_AVAILABLE_juce_audio_processors=1 /DJUCE_MODULE_AVAILABLE_juce_audio_processors_headless=1 ^
  /DJUCE_MODULE_AVAILABLE_juce_audio_utils=1 /DJUCE_MODULE_AVAILABLE_juce_core=1 /DJUCE_MODULE_AVAILABLE_juce_data_structures=1 ^
  /DJUCE_MODULE_AVAILABLE_juce_events=1 /DJUCE_MODULE_AVAILABLE_juce_graphics=1 /DJUCE_MODULE_AVAILABLE_juce_gui_basics=1 /DJUCE_MODULE_AVAILABLE_juce_gui_extra=1 ^
  /DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 /DJUCE_VST3_CAN_REPLACE_VST2=0 /DJUCE_STRICT_REFCOUNTEDPOINTER=1 ^
  /DJucePlugin_Build_VST=0 /DJucePlugin_Build_VST3=1 /DJucePlugin_Build_AU=0 /DJucePlugin_Build_AUv3=0 /DJucePlugin_Build_AAX=0 ^
  /DJucePlugin_Build_Standalone=1 /DJucePlugin_Build_Unity=0 /DJucePlugin_Build_LV2=0 /DJucePlugin_Enable_IAA=0 /DJucePlugin_Enable_ARA=0 ^
  /DJucePlugin_Name=\"SquillysDelay\" /DJucePlugin_IsSynth=0 /DJucePlugin_WantsMidiInput=0 /DJucePlugin_ProducesMidiOutput=0 /DJucePlugin_IsMidiEffect=0 ^
  /DJUCE_STANDALONE_APPLICATION=1 /DJUCE_SHARED_CODE=1 ^
  /I"%ROOT%\JuceLibraryCode" /I"%ROOT%\Source" /I"%JUCE%" /I"%JUCE%\juce_audio_processors_headless\format_types\VST3_SDK" ^
  DelayTest.cpp /link "%LIB_PATH%" ^
  ole32.lib oleaut32.lib shell32.lib comdlg32.lib user32.lib gdi32.lib advapi32.lib shlwapi.lib winmm.lib wininet.lib ws2_32.lib version.lib imm32.lib comctl32.lib uxtheme.lib dwmapi.lib ^
  /OUT:DelayTest.exe
if errorlevel 1 exit /b 1
.\DelayTest.exe
