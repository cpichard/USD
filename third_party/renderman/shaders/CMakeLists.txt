if(NOT TARGET hdPrman)
  message(STATUS "Skipping RenderMan shaders, hdPrman required")
  return()
endif()

find_program(OSLC oslc HINTS ${RENDERMAN_BINARY_DIR})

function(prman_osl SHADER_NAME)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_NAME}.osl")
        set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    elseif(EXISTS "${PROJECT_SOURCE_DIR}/pxr/usd/plugin/usdShaders/shaders/${SHADER_NAME}.osl")
        set(SHADER_DIR "${PROJECT_SOURCE_DIR}/pxr/usd/plugin/usdShaders/shaders")
    else()
        message(WARNING "Could not find ${SHADER_NAME}.osl.")
        return()
    endif()

    set(SHADER_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}.oso)

    if(RENDERMAN_OSL_INCLUDE_DIR)
        set(OSLC_INCLUDE_FLAGS "-I${RENDERMAN_OSL_INCLUDE_DIR}")
    endif()

    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${OSLC} ${OSLC_INCLUDE_FLAGS} -o ${SHADER_OUTPUT} ${SHADER_NAME}.osl
        WORKING_DIRECTORY ${SHADER_DIR}
        MAIN_DEPENDENCY ${SHADER_DIR}/${SHADER_NAME}.osl
        DEPENDS ${OSLC}
        VERBATIM
    )
    add_custom_target(${SHADER_NAME}_target DEPENDS ${SHADER_OUTPUT})
    add_dependencies(hdPrman ${SHADER_NAME}_target)
	if(NOT RENDERMAN_OSL_INSTALL_DIR)
		set(RENDERMAN_OSL_INSTALL_DIR plugin/usd/resources/shaders)
	endif()
	install(PROGRAMS ${SHADER_OUTPUT} DESTINATION ${RENDERMAN_OSL_INSTALL_DIR})
endfunction()

prman_osl(UsdPreviewSurfaceParameters)
prman_osl(UsdTransformColor)
prman_osl(SphericalHarmonicsToColor)
prman_osl(UsdUVTexture)
prman_osl(UsdPrimvarReader_float)
prman_osl(UsdPrimvarReader_float2)
prman_osl(UsdPrimvarReader_float3)
prman_osl(UsdPrimvarReader_float4)
prman_osl(UsdPrimvarReader_int)
prman_osl(UsdPrimvarReader_string)
prman_osl(UsdTransform2d)
prman_osl(UsdVerticalFlip)
if(PXR_ENABLE_MATERIALX_SUPPORT)
    prman_osl(StandardSurfaceParameters)
    prman_osl(DisplacementParameters)
endif()
