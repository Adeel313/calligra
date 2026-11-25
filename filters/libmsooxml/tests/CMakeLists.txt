ecm_add_test(MsooXmlImportTest.cpp
    TEST_NAME MsooXmlImportTest
    NAME_PREFIX "filters-libmsooxml-"
    LINK_LIBRARIES komsooxml Qt6::Test)
target_include_directories(MsooXmlImportTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/..)
