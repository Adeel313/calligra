add_definitions(-DTRANSLATION_DOMAIN=\"calligrafilters\")

kde_enable_exceptions()

if(SHOULD_BUILD_LIB_KOODF2)
    add_subdirectory( libodf2 )
endif()

if(SHOULD_BUILD_LIB_KOODFREADER)
    add_subdirectory(libodfreader)
endif()

if(SHOULD_BUILD_LIB_MSO)
    add_subdirectory(libmso)
endif()

if(SHOULD_BUILD_LIB_KOMSOOXML)
    add_subdirectory( libmsooxml )
endif()

if(TARGET komain)
    # Reference conversion tools are built explicitly by compatibility tests.
    add_subdirectory(libreoffice EXCLUDE_FROM_ALL)
endif()

add_subdirectory(words)
add_subdirectory(stage)
add_subdirectory(sheets)
add_subdirectory(odg)
add_subdirectory(karbon)
