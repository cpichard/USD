if(NOT TARGET hdPrman)
  message(STATUS "Skipping RenderMan shaders, hdPrman required")
  return()
endif()

set(PXR_INSTALL_SUBDIR "plugin/usd/resources")

function(prman_osl SHADER_NAME SHADER_DIR)
    set(infile ${SHADER_DIR}/${SHADER_NAME}.osl)
    set(outfile ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}.oso)
    add_custom_command(
        OUTPUT ${outfile}
        COMMAND ${RENDERMAN_BINARY_DIR}/oslc -o ${outfile} ${infile}
        MAIN_DEPENDENCY ${infile}
        VERBATIM
    )
    add_custom_target(${SHADER_NAME}_target DEPENDS ${outfile})
    add_dependencies(hdPrman ${SHADER_NAME}_target)
    install(
        PROGRAMS ${outfile}
        DESTINATION ${PXR_INSTALL_SUBDIR}/shaders
    )
endfunction()

set(shader_dir "${PROJECT_SOURCE_DIR}/pxr/usd/plugin/usdShaders/shaders")
prman_osl(UsdPreviewSurfaceParameters ${CMAKE_CURRENT_SOURCE_DIR})
if (${PXR_ENABLE_MATERIALX_SUPPORT})
prman_osl(StandardSurfaceParameters ${CMAKE_CURRENT_SOURCE_DIR})
prman_osl(DisplacementParameters ${CMAKE_CURRENT_SOURCE_DIR})
endif()
prman_osl(UsdUVTexture ${shader_dir})
prman_osl(UsdPrimvarReader_float ${shader_dir})
prman_osl(UsdPrimvarReader_float2 ${shader_dir})
prman_osl(UsdPrimvarReader_float3 ${shader_dir})

