include("${CMAKE_CURRENT_LIST_DIR}/rule.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/file.cmake")

set(BZ6SlotCar_default_library_list )

set(OUTPUT_SUFFIX elf)
# Handle files with suffix s, for group default-XC32
if(BZ6SlotCar_default_default_XC32_FILE_TYPE_assemble)
add_library(BZ6SlotCar_default_default_XC32_assemble OBJECT ${BZ6SlotCar_default_default_XC32_FILE_TYPE_assemble})
    BZ6SlotCar_default_default_XC32_assemble_rule(BZ6SlotCar_default_default_XC32_assemble)
    list(APPEND BZ6SlotCar_default_library_list "$<TARGET_OBJECTS:BZ6SlotCar_default_default_XC32_assemble>")

endif()

# Handle files with suffix S, for group default-XC32
if(BZ6SlotCar_default_default_XC32_FILE_TYPE_assembleWithPreprocess)
add_library(BZ6SlotCar_default_default_XC32_assembleWithPreprocess OBJECT ${BZ6SlotCar_default_default_XC32_FILE_TYPE_assembleWithPreprocess})
    BZ6SlotCar_default_default_XC32_assembleWithPreprocess_rule(BZ6SlotCar_default_default_XC32_assembleWithPreprocess)
    list(APPEND BZ6SlotCar_default_library_list "$<TARGET_OBJECTS:BZ6SlotCar_default_default_XC32_assembleWithPreprocess>")

endif()

# Handle files with suffix [cC], for group default-XC32
if(BZ6SlotCar_default_default_XC32_FILE_TYPE_compile)
add_library(BZ6SlotCar_default_default_XC32_compile OBJECT ${BZ6SlotCar_default_default_XC32_FILE_TYPE_compile})
    BZ6SlotCar_default_default_XC32_compile_rule(BZ6SlotCar_default_default_XC32_compile)
    list(APPEND BZ6SlotCar_default_library_list "$<TARGET_OBJECTS:BZ6SlotCar_default_default_XC32_compile>")

endif()

# Handle files with suffix cpp, for group default-XC32
if(BZ6SlotCar_default_default_XC32_FILE_TYPE_compile_cpp)
add_library(BZ6SlotCar_default_default_XC32_compile_cpp OBJECT ${BZ6SlotCar_default_default_XC32_FILE_TYPE_compile_cpp})
    BZ6SlotCar_default_default_XC32_compile_cpp_rule(BZ6SlotCar_default_default_XC32_compile_cpp)
    list(APPEND BZ6SlotCar_default_library_list "$<TARGET_OBJECTS:BZ6SlotCar_default_default_XC32_compile_cpp>")

endif()

# Handle files with suffix [cC], for group default-XC32
if(BZ6SlotCar_default_default_XC32_FILE_TYPE_dependentObject)
add_library(BZ6SlotCar_default_default_XC32_dependentObject OBJECT ${BZ6SlotCar_default_default_XC32_FILE_TYPE_dependentObject})
    BZ6SlotCar_default_default_XC32_dependentObject_rule(BZ6SlotCar_default_default_XC32_dependentObject)
    list(APPEND BZ6SlotCar_default_library_list "$<TARGET_OBJECTS:BZ6SlotCar_default_default_XC32_dependentObject>")

endif()


# Main target for this project
add_executable(BZ6SlotCar_default_image_yWqUIJoV ${BZ6SlotCar_default_library_list})

set_target_properties(BZ6SlotCar_default_image_yWqUIJoV PROPERTIES
    OUTPUT_NAME "default"
    SUFFIX ".elf"
    RUNTIME_OUTPUT_DIRECTORY "${BZ6SlotCar_default_output_dir}")
target_link_libraries(BZ6SlotCar_default_image_yWqUIJoV PRIVATE ${BZ6SlotCar_default_default_XC32_FILE_TYPE_link})
# Add the link options from the rule file.
BZ6SlotCar_default_link_rule( BZ6SlotCar_default_image_yWqUIJoV)

# Add bin2hex target for converting built file to a .hex file.
string(REGEX REPLACE [.]elf$ .hex BZ6SlotCar_default_image_name_hex ${BZ6SlotCar_default_image_name})
add_custom_target(BZ6SlotCar_default_Bin2Hex ALL
    COMMAND ${MP_BIN2HEX} \"${BZ6SlotCar_default_output_dir}/${BZ6SlotCar_default_image_name}\"
    DEPENDS "${BZ6SlotCar_default_output_dir}/${BZ6SlotCar_default_image_name}"
    BYPRODUCTS ${BZ6SlotCar_default_output_dir}/${BZ6SlotCar_default_image_name_hex}
    COMMENT "Convert built file to .hex")
add_dependencies(BZ6SlotCar_default_Bin2Hex BZ6SlotCar_default_image_yWqUIJoV)




