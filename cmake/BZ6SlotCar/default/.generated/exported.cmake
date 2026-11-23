set(DEPENDENT_MP_BIN2HEXBZ6SlotCar_default_yWqUIJoV "c:/Program Files/Microchip/xc32/v6.00/bin/xc32-bin2hex.exe")
set(DEPENDENT_DEPENDENT_TARGET_ELFBZ6SlotCar_default_yWqUIJoV ${CMAKE_CURRENT_LIST_DIR}/../../../../out/BZ6SlotCar/default.elf)
set(DEPENDENT_TARGET_DIRBZ6SlotCar_default_yWqUIJoV ${CMAKE_CURRENT_LIST_DIR}/../../../../out/BZ6SlotCar)
set(DEPENDENT_BYPRODUCTSBZ6SlotCar_default_yWqUIJoV ${DEPENDENT_TARGET_DIRBZ6SlotCar_default_yWqUIJoV}/${sourceFileNameBZ6SlotCar_default_yWqUIJoV}.c)
add_custom_command(
    OUTPUT ${DEPENDENT_TARGET_DIRBZ6SlotCar_default_yWqUIJoV}/${sourceFileNameBZ6SlotCar_default_yWqUIJoV}.c
    COMMAND ${DEPENDENT_MP_BIN2HEXBZ6SlotCar_default_yWqUIJoV} --image ${DEPENDENT_DEPENDENT_TARGET_ELFBZ6SlotCar_default_yWqUIJoV} --image-generated-c ${sourceFileNameBZ6SlotCar_default_yWqUIJoV}.c --image-generated-h ${sourceFileNameBZ6SlotCar_default_yWqUIJoV}.h --image-copy-mode ${modeBZ6SlotCar_default_yWqUIJoV} --image-offset ${addressBZ6SlotCar_default_yWqUIJoV} 
    WORKING_DIRECTORY ${DEPENDENT_TARGET_DIRBZ6SlotCar_default_yWqUIJoV}
    DEPENDS ${DEPENDENT_DEPENDENT_TARGET_ELFBZ6SlotCar_default_yWqUIJoV})
add_custom_target(
    dependent_produced_source_artifactBZ6SlotCar_default_yWqUIJoV 
    DEPENDS ${DEPENDENT_TARGET_DIRBZ6SlotCar_default_yWqUIJoV}/${sourceFileNameBZ6SlotCar_default_yWqUIJoV}.c
    )
