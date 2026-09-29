add_executable(injector
    main.cpp
    manual_map.cpp
    pe.cpp
)
target_include_directories(injector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(injector PRIVATE ntdll)
set_target_properties(injector PROPERTIES OUTPUT_NAME "r9k")
