# Development reference harness only. Native filters neither link nor invoke LibreOffice.
add_executable(calligraofficeconverter EXCLUDE_FROM_ALL OfficeConversionWorker.cpp)
target_link_libraries(calligraofficeconverter Qt6::Core Qt6::Xml KF6::Archive)

add_executable(office_roundtrip EXCLUDE_FROM_ALL tests/OfficeRoundTrip.cpp)
target_include_directories(office_roundtrip PRIVATE ${KOMAIN_INCLUDES})
target_link_libraries(office_roundtrip komain KF6::I18n KF6::CoreAddons)

if(BUILD_TESTING)
    add_executable(TestOdpTextCompatibility tests/TestOdpTextCompatibility.cpp)
    target_link_libraries(TestOdpTextCompatibility PRIVATE Qt6::Core Qt6::Xml)
    add_test(NAME office-OdpTextCompatibility COMMAND TestOdpTextCompatibility)
endif()
