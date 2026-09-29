# injector/CMakeLists.txt — pengooin.exe watcher with embedded payload
# language: CMake, target: Windows 11 x64, MSVC
#
# emits a single-exe watcher that carries r9k_payload.dll as an RCDATA
# resource. add_custom_command runs write_embed_rc.cmake after the payload
# DLL has been built, generating an .rc that references the fresh binary by
# absolute path. RC gets forward-slash paths so its string parser doesn't
# eat backslashes.

enable_language(RC)

set(EMBED_RC ${CMAKE_CURRENT_BINARY_DIR}/embed_payload.rc)

add_custom_command(
    OUTPUT ${EMBED_RC}
    COMMAND ${CMAKE_COMMAND}
        -D PAYLOAD=$<TARGET_FILE:r9k_payload>
        -D OUT=${EMBED_RC}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/write_embed_rc.cmake
    DEPENDS r9k_payload ${CMAKE_CURRENT_SOURCE_DIR}/write_embed_rc.cmake
    COMMENT "embedding r9k_payload.dll into pengooin.exe"
    VERBATIM
)

add_executable(pengooin WIN32
    main.cpp
    manual_map.cpp
    pe.cpp
    ${EMBED_RC}
)
target_include_directories(pengooin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pengooin PRIVATE ntdll shell32 user32 advapi32 psapi)

add_dependencies(pengooin r9k_payload)

set_target_properties(pengooin PROPERTIES OUTPUT_NAME "pengooin")
