cmake_minimum_required(VERSION 3.21)
project(cheetahClip VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2 Network Concurrent)

qt_standard_project_setup(REQUIRES 6.5)

if(WIN32)
    add_compile_definitions(
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        QT_NO_CAST_FROM_ASCII
        QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    )
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_options($<$<CONFIG:Release>:/O2>)
endif()

set(APP_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/settings/Settings.cpp
    src/settings/SettingsController.cpp
    src/i18n/I18n.cpp
    src/tray/TrayIcon.cpp
    src/hotkey/HotkeyManager.cpp
    src/autostart/Autostart.cpp
    src/capture/CaptureController.cpp
    src/capture/CaptureOverlay.cpp
    src/capture/ClipboardImage.cpp
    src/platform/UiaCore.cpp
    src/ai/WinInput.cpp
    src/ai/UiaComposer.cpp
    src/ai/BrowserNav.cpp
    src/ai/AiSender.cpp
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${APP_SOURCES})

qt_add_executable(cheetahClip WIN32
    ${APP_SOURCES}
    resources/app.rc
)
set_target_properties(cheetahClip PROPERTIES OUTPUT_NAME Cheet)

qt_add_qml_module(cheetahClip
    URI CheetahClip
    VERSION 1.0
    QML_FILES
        qml/SettingsWindow.qml
        qml/components/RoundedCard.qml
        qml/components/ToggleSwitch.qml
        qml/components/DarkField.qml
        qml/components/RoundButton.qml
        qml/components/DarkCombo.qml
    RESOURCES
        resources/icons/icon.png
        resources/icons/icon.ico
        resources/icons/tools/rect.png
        resources/icons/tools/ellipse.png
        resources/icons/tools/arrow.png
        resources/icons/tools/pen.png
        resources/icons/tools/text.png
        resources/icons/tools/undo.png
        resources/icons/tools/cancel.png
        resources/icons/tools/ok.png
)

target_include_directories(cheetahClip PRIVATE src)

target_link_libraries(cheetahClip PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Concurrent
)

if(WIN32)
    target_link_libraries(cheetahClip PRIVATE user32 shell32 ole32 oleaut32 oleacc dwmapi)
endif()

install(TARGETS cheetahClip
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
