# Included by the generated CMakeLists.txt; not overwritten when MPLAB
# regenerates the project.
#
# Embeds the PIC32CX-BZ6 boot metadata header (512 bytes at 0x01000000) into the
# ELF right after linking. The HEX generated afterwards by xc32-bin2hex, and
# programming/debugging the ELF from MPLAB, then include the header. Without it
# the BZ6 boot ROM does not start the application.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(BZ6_METADATA_SEQ_NUM "FFFFFFFE" CACHE STRING "Metadata sequence number (hex)")
set(BZ6_METADATA_AUTH_MTHD "0" CACHE STRING "Metadata authentication method (0 = none)")
set(BZ6_METADATA_FW_REV "01000000" CACHE STRING "Metadata firmware image revision (hex)")

get_filename_component(BZ6_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(BZ6_METADATA_SCRIPT "${BZ6_PROJECT_ROOT}/scripts/add_bz6_metadata.py")

get_property(BZ6_ALL_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(BZ6_TARGET ${BZ6_ALL_TARGETS})
    if(BZ6_TARGET MATCHES "_image_")
        add_custom_command(TARGET ${BZ6_TARGET} POST_BUILD
            COMMAND ${Python3_EXECUTABLE} "${BZ6_METADATA_SCRIPT}"
                    "${OBJCOPY}" "$<TARGET_FILE:${BZ6_TARGET}>"
                    ${BZ6_METADATA_SEQ_NUM} ${BZ6_METADATA_AUTH_MTHD} ${BZ6_METADATA_FW_REV}
            COMMENT "Embedding BZ6 boot metadata"
            VERBATIM)
    endif()
endforeach()
