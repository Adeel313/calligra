if(SHOULD_BUILD_FILTER_ODT_TO_DOC)

include_directories(
    ${CMAKE_SOURCE_DIR}/filters/sheets/excel/export
    ${CMAKE_SOURCE_DIR}/filters/words/msword-odf/wv2/src
    ${KOODF2_INCLUDES}
)

set(Odt2Doc_PART_SRCS
    DocExport.cpp
    Word97Writer.cpp
    ../../sheets/excel/export/CFBWriter.cpp
)

add_library(calligra_filter_odt2doc MODULE ${Odt2Doc_PART_SRCS})
target_link_libraries(calligra_filter_odt2doc komain koodf koodf2 Qt6::Xml)
install(TARGETS calligra_filter_odt2doc DESTINATION ${KDE_INSTALL_PLUGINDIR}/calligra/formatfilters)

endif()
