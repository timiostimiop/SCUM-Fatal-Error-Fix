cmake_minimum_required(VERSION 3.20)

project(ScumAudioDiag LANGUAGES CXX)

add_library(ScumAudioDiag SHARED
    src/ScumAudioDiag.cpp
)

add_executable(ScumAudioDiagInjector
    src/Injector.cpp
)

target_compile_features(ScumAudioDiag PRIVATE cxx_std_17)
target_compile_features(ScumAudioDiagInjector PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(ScumAudioDiag PRIVATE /W4 /permissive- /EHsc-)
    target_compile_options(ScumAudioDiagInjector PRIVATE /W4 /permissive-)
    target_link_options(ScumAudioDiag PRIVATE /INCREMENTAL:NO)
    target_link_options(ScumAudioDiagInjector PRIVATE /INCREMENTAL:NO)
endif()

add_dependencies(ScumAudioDiagInjector ScumAudioDiag)
