include_directories(
    ${KOODF2_INCLUDES}
    ${CMAKE_SOURCE_DIR}/filters/libmsooxml
    ${CMAKE_SOURCE_DIR}/filters/libmso
    ${CMAKE_SOURCE_DIR}/filters/sheets/xlsx
    ${KOMAIN_INCLUDES}
)

########### next target ###############

if(TARGET kopageapp)
    add_executable(stage_pptx_roundtrip EXCLUDE_FROM_ALL tests/StagePptxRoundTrip.cpp)
    target_include_directories(stage_pptx_roundtrip PRIVATE ${KOPAGEAPP_INCLUDES})
    target_link_libraries(stage_pptx_roundtrip komain kopageapp KF6::I18n KF6::CoreAddons)
endif()

if(SHOULD_BUILD_FILTER_ODP_TO_PPTX)
    include_directories(${CMAKE_SOURCE_DIR}/plugins/chartshape)
    include_directories(${CMAKE_SOURCE_DIR}/stage/part)
    add_library(calligra_filter_odp2pptx MODULE PptxExport.cpp)
    target_link_libraries(calligra_filter_odp2pptx komain kopageapp calligrastageprivate chartshapecore)
    install(TARGETS calligra_filter_odp2pptx DESTINATION ${KDE_INSTALL_PLUGINDIR}/calligra/formatfilters)
endif()

if(SHOULD_BUILD_FILTER_PPTX_TO_ODP)
add_executable(test_native_pptx_preparation EXCLUDE_FROM_ALL tests/TestNativePptxPreparation.cpp)
target_link_libraries(test_native_pptx_preparation KF6::Archive Qt6::Xml)

set(pptx2odp_PART_SRCS
    PptxDebug.cpp
    PptxImport.cpp
    PptxXmlDocumentReader.cpp
    PptxXmlSlideReader.cpp
    PptxXmlCommentsReader.cpp
    PptxXmlCommentAuthorsReader.cpp
    ${CMAKE_SOURCE_DIR}/filters/sheets/xlsx/XlsxChartOdfWriter.cpp
    ${CMAKE_SOURCE_DIR}/filters/sheets/xlsx/XlsxXmlChartReader.cpp
)

add_library(calligra_filter_pptx2odp MODULE ${pptx2odp_PART_SRCS})

target_link_libraries(
    calligra_filter_pptx2odp
    KF6::Archive
    Qt6::Xml
    komsooxml
    mso
    koodf2
    koodf
    komain
)

install(TARGETS calligra_filter_pptx2odp DESTINATION ${KDE_INSTALL_PLUGINDIR}/calligra/formatfilters)
endif()

########### install files ###############

if(SHOULD_BUILD_OKULAR_GENERATOR_PPTX)
    kcoreaddons_add_plugin(okularGenerator_pptx_calligra
        INSTALL_NAMESPACE "okular_generators"
        SOURCES OkularPptxGeneratorPlugin.cpp
    )

    target_link_libraries( okularGenerator_pptx_calligra
        kookularGenerator_odp
    )

    install( PROGRAMS okularApplication_pptx_calligra.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
