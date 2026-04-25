cmake_minimum_required(VERSION 3.21)

project(NeoLDG VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets SerialPort)

qt_standard_project_setup()

qt_add_executable(NeoLDG
    src/main.cpp
    src/LdgProtocol.cpp
    src/LdgProtocol.hpp
    src/LdgTunerController.cpp
    src/LdgTunerController.hpp
    src/MainWindow.cpp
    src/MainWindow.hpp
)

target_link_libraries(NeoLDG
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::SerialPort
)

set_target_properties(NeoLDG PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

install(TARGETS NeoLDG
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

install(FILES
    README.md
    LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/NeoLDG
)
