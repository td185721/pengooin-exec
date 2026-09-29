file(GLOB_RECURSE PAYLOAD_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dllmain.cpp"
)
add_library(payload SHARED ${PAYLOAD_SRC})
target_include_directories(payload PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(payload PRIVATE
    ntdll ws2_32 winhttp bcrypt crypt32 d3d11 dxgi user32 gdi32
    $<TARGET_NAME_IF_EXISTS:Luau.Compiler>
    $<TARGET_NAME_IF_EXISTS:Luau.Ast>
)
set_target_properties(payload PROPERTIES OUTPUT_NAME "r9k_payload" PREFIX "")

# custom entry: pengooin_dll_entry registers x64 unwind info before handing off
# to _DllMainCRTStartup. required for manual-mapped DLLs so CRT init exceptions
# unwind instead of silently killing the thread.
if(MSVC)
    set_target_properties(payload PROPERTIES LINK_FLAGS "/ENTRY:pengooin_dll_entry")
endif()
