# write_embed_rc.cmake — build-time helper. writes the RC file that embeds
# the freshly-linked payload DLL as an RCDATA resource.
#
# invoked from injector/CMakeLists.txt via add_custom_command:
#   -D PAYLOAD=<path-to-dll> -D OUT=<path-to-rc> -P write_embed_rc.cmake
#
# forward-slash the path so RC.exe's string parser doesn't eat backslashes as
# escape sequences.
file(TO_CMAKE_PATH "${PAYLOAD}" PAYLOAD_FWD)
file(WRITE "${OUT}"
"1001 RCDATA \"${PAYLOAD_FWD}\"\n")
