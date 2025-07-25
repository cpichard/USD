set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usd)

pxr_python_bin(usddiff
    DEPENDENCIES
        tf
        sdf
)

pxr_install_test_dir(
    SRC testenv/testExitCodesForDiff
    DEST testUsdDiffExitCodesForDiff1
)

pxr_register_test(testUsdDiffExitCodesForDiff1
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a.usda a.usda --noeffect"
    EXPECTED_RETURN_CODE 0
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testExitCodesForDiff
    DEST testUsdDiffExitCodesForDiff2
)

pxr_register_test(testUsdDiffExitCodesForDiff2
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a.usda b.usda --noeffect"
    EXPECTED_RETURN_CODE 1
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffToolSelectionDefault
    DEST testUsdDiffToolSelectionDefault
)

pxr_register_test(testUsdDiffToolSelectionDefault
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a.usda b.usda --noeffect"
    STDOUT_REDIRECT out.txt
    DIFF_COMPARE out.txt
    EXPECTED_RETURN_CODE 1
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffToolVariousUsdFormats
    DEST testUsdDiffToolVariousFormats1
)
 
pxr_register_test(testUsdDiffToolVariousFormats1
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a.usda a.usd --noeffect"
    EXPECTED_RETURN_CODE 0
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffToolVariousUsdFormats
    DEST testUsdDiffToolVariousFormats2
)

pxr_register_test(testUsdDiffToolVariousFormats2
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a.usdc a.usd --noeffect"
    EXPECTED_RETURN_CODE 0
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffToolVariousUsdFormats
    DEST testUsdDiffToolVariousFormats3
)

if (PXR_BUILD_ALEMBIC_PLUGIN)
	pxr_register_test(testUsdDiffToolVariousFormats3
	    PYTHON
	    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a.abc a.abc --noeffect"
	    EXPECTED_RETURN_CODE 0
	    PRE_PATH
		${CMAKE_INSTALL_PREFIX}/bin
	)

	pxr_install_test_dir(
	    SRC testenv/testDiffToolVariousUsdFormats
	    DEST testUsdDiffToolVariousFormats4
	)

	pxr_register_test(testUsdDiffToolVariousFormats4
	    PYTHON
	    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a.abc a.txt --noeffect"
	    EXPECTED_RETURN_CODE 2 
	    PRE_PATH
		${CMAKE_INSTALL_PREFIX}/bin
	)

	pxr_install_test_dir(
	    SRC testenv/testDiffToolVariousUsdFormats
	    DEST testUsdDiffToolVariousFormats5
	)

	pxr_register_test(testUsdDiffToolVariousFormats5
	    PYTHON
	    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a.txt a.abc --noeffect"
	    EXPECTED_RETURN_CODE 2 
	    PRE_PATH
		${CMAKE_INSTALL_PREFIX}/bin
        )
else()
    message(STATUS "Skipping alembic-based usddiff tests because PXR_BUILD_ALEMBIC_PLUGIN=OFF")
endif()

if (PXR_BUILD_DRACO_PLUGIN)
    # Checks that USD diff tool successfully recognizes and loads Draco files.
    pxr_install_test_dir(
        SRC testenv/testDiffToolDracoFormat
        DEST testUsdDiffWithDracoFiles
    )

    pxr_register_test(testUsdDiffWithDracoFiles
    PYTHON
        COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff --noeffect CubeWithGenericPrimvars.drc CubeWithGenericPrimvars.drc"
        EXPECTED_RETURN_CODE 0
        PRE_PATH
            ${CMAKE_INSTALL_PREFIX}/bin
    )

    # Checks that the content of Draco file compares equally to the
    # corresponding USDA file.
    #
    # XXX: As of Draco 1.4.0 this test fails on ARM64 macOS builds
    # with floating point precision errors. Disabling this test for
    # macOS in the interim.
    if (NOT APPLE)
        pxr_install_test_dir(
            SRC testenv/testDiffToolDracoFormat
            DEST testUsdDiffWithUsdaAndDracoFiles
        )

        pxr_register_test(testUsdDiffWithUsdaAndDracoFiles
        PYTHON
            COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff --noeffect CubeWithGenericPrimvars.drc CubeWithGenericPrimvars.usda"
            EXPECTED_RETURN_CODE 0
            PRE_PATH
                ${CMAKE_INSTALL_PREFIX}/bin
        )
    endif()

    # Checks that USD diff tool can load USDA file that refer to Draco file.
    # Note that the files are flattened in order to pull in the content of
    # referred Draco files and not just their references.
    pxr_install_test_dir(
        SRC testenv/testDiffToolDracoFormat
        DEST testUsdDiffWithReferredDracoFiles
    )

    pxr_register_test(testUsdDiffWithReferredDracoFiles
    PYTHON
        COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff --noeffect --flatten CubeWithGenericPrimvarsCompressed.usda CubeWithGenericPrimvarsCompressed.usda"
        EXPECTED_RETURN_CODE 0
        PRE_PATH
            ${CMAKE_INSTALL_PREFIX}/bin
    )

    # Checks that USD diff tool can load USDZ file that includes Draco files.
    # Note that the files are flattened in order to pull in the content of
    # referred Draco files and not just their references.
    pxr_install_test_dir(
	SRC testenv/testDiffToolDracoFormat
        DEST testUsdDiffWithDracoUsdz
    )

    pxr_register_test(testUsdDiffWithDracoUsdz
    PYTHON
        COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff --noeffect --flatten ComplexSetCompressed.usdz ComplexSetCompressed.usdz"
        EXPECTED_RETURN_CODE 0
        PRE_PATH
            ${CMAKE_INSTALL_PREFIX}/bin
    )
else()
    message(STATUS "Skipping Draco-based usddiff tests because PXR_BUILD_DRACO_PLUGIN=OFF")
endif()

pxr_install_test_dir(
    SRC testenv/testDiffToolComposedResults
    DEST testUsdDiffComposedResults1
)

pxr_register_test(testUsdDiffComposedResults1
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a.usd b.usd --noeffect"
    EXPECTED_RETURN_CODE 1 
    STDOUT_REDIRECT layer_diff.txt
    DIFF_COMPARE layer_diff.txt
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffToolComposedResults
    DEST testUsdDiffComposedResults2
)

pxr_register_test(testUsdDiffComposedResults2
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a.usd b.usd --noeffect --flatten"
    EXPECTED_RETURN_CODE 1 
    STDOUT_REDIRECT composed_diff.txt
    DIFF_COMPARE composed_diff.txt
    CLEAN_OUTPUT "(?:[A-Za-z]:)?\\/.*\\/"
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffWithDirectories
    DEST testUsdDiffWithDirectories1
)

pxr_register_test(testUsdDiffWithDirectories1
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff --noeffect a/ a/"
    EXPECTED_RETURN_CODE 0
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)
 
pxr_install_test_dir(
    SRC testenv/testDiffWithDirectories
    DEST testUsdDiffWithDirectories2
)

pxr_register_test(testUsdDiffWithDirectories2
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff --noeffect a/ b/"
    EXPECTED_RETURN_CODE 1 
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffWithDirectories
    DEST testUsdDiffWithDirectories3
)

pxr_register_test(testUsdDiffWithDirectories3
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff --noeffect . a/"
    EXPECTED_RETURN_CODE 0
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffWithDirectories
    DEST testUsdDiffWithDirectories4
)

pxr_register_test(testUsdDiffWithDirectories4
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff --noeffect a.usda a/"
    EXPECTED_RETURN_CODE 0
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffWithDirectories
    DEST testUsdDiffWithDirectories5
)

pxr_register_test(testUsdDiffWithDirectories5
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff --noeffect a.usda b/"
    EXPECTED_RETURN_CODE 1
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffWithDirectories
    DEST testUsdDiffWithDirectories6
)

pxr_register_test(testUsdDiffWithDirectories6
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff --noeffect b/ a.usda"
    EXPECTED_RETURN_CODE 1
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffWithDirectories
    DEST testUsdDiffWithDirectories7
)

pxr_register_test(testUsdDiffWithDirectories7
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff --noeffect a.usda b.usda b/"
    EXPECTED_RETURN_CODE 1
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffWithDirectories
    DEST testUsdDiffWithDirectories8
)

pxr_register_test(testUsdDiffWithDirectories8
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff --noeffect b/ a.usda b.usda"
    EXPECTED_RETURN_CODE 1
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffToolIterateUsdz
    DEST testDiffToolIterateUsdz1
)

pxr_register_test(testDiffToolIterateUsdz1
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a.usdz a.usdz"
    EXPECTED_RETURN_CODE 0
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffToolIterateUsdz
    DEST testDiffToolIterateUsdz2
)

pxr_register_test(testDiffToolIterateUsdz2
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a.usdz b.usdz"
    EXPECTED_RETURN_CODE 1
    STDOUT_REDIRECT iterate_usd.txt
    DIFF_COMPARE iterate_usd.txt
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffToolIterateUsdz
    DEST testDiffToolIterateUsdz3
)

pxr_register_test(testDiffToolIterateUsdz3
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff imagea.usdz imagea.usdz"
    EXPECTED_RETURN_CODE 0
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffToolIterateUsdz
    DEST testDiffToolIterateUsdz4
)

pxr_register_test(testDiffToolIterateUsdz4
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff imagea.usdz imageb.usdz"
    EXPECTED_RETURN_CODE 1
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)

pxr_install_test_dir(
    SRC testenv/testDiffToolIterateUsdz
    DEST testDiffToolIterateUsdz5
)

pxr_register_test(testDiffToolIterateUsdz5
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usddiff a_crate.usdz b_crate.usdz"
    EXPECTED_RETURN_CODE 1
    STDOUT_REDIRECT iterate_usd_crate.txt
    DIFF_COMPARE iterate_usd_crate.txt
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)
