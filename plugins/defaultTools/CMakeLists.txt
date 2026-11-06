project( defaulttools )

add_definitions(-DTRANSLATION_DOMAIN=\"calligra-defaulttools\")

include_directories(
    ${TEXTLAYOUT_INCLUDES} # needed because ConnectionTool.cpp includes TextShape.h to get the id
    ${CMAKE_SOURCE_DIR}/libs/widgets
    ${FLAKE_INCLUDES}
    ${TEXTLAYOUT_INCLUDES}
)

add_subdirectory(pics)


########### Flake Plugin library ###############

set ( defaulttools_SRCS
    Plugin.cpp
    defaulttool/DefaultTool.cpp
    defaulttool/DefaultToolFactory.cpp
    defaulttool/DefaultToolWidget.cpp
    defaulttool/DefaultToolArrangeWidget.cpp
    defaulttool/DefaultToolTransformWidget.cpp
    defaulttool/ShapeMoveStrategy.cpp
    defaulttool/ShapeResizeStrategy.cpp
    defaulttool/ShapeRotateStrategy.cpp
    defaulttool/ShapeShearStrategy.cpp
    defaulttool/SelectionDecorator.cpp
    defaulttool/SelectionTransformCommand.cpp
    defaulttool/ChartResizeStrategy.cpp

    guidestool/GuidesTool.cpp
    guidestool/GuidesToolFactory.cpp
    guidestool/GuidesToolOptionWidget.cpp
    guidestool/InsertGuidesToolOptionWidget.cpp

    connectionTool/ConnectionTool.cpp
    connectionTool/ConnectionToolFactory.cpp
    connectionTool/AddConnectionPointCommand.cpp
    connectionTool/RemoveConnectionPointCommand.cpp
    connectionTool/ChangeConnectionPointCommand.cpp
    connectionTool/MoveConnectionPointStrategy.cpp
    connectionTool/ConnectionPointWidget.cpp
)

ki18n_wrap_ui(defaulttools_SRCS
    defaulttool/DefaultToolWidget.ui
    defaulttool/DefaultToolArrangeWidget.ui
    defaulttool/DefaultToolTransformWidget.ui
    guidestool/GuidesToolOptionWidget.ui
    guidestool/InsertGuidesToolOptionWidget.ui
    connectionTool/ConnectionPointWidget.ui
)

add_library(calligra_tool_defaults MODULE ${defaulttools_SRCS})
target_link_libraries(calligra_tool_defaults flake kowidgets)
install(TARGETS calligra_tool_defaults DESTINATION ${KDE_INSTALL_PLUGINDIR}/calligra/tools)

if(BUILD_TESTING)
    set(defaulttools_test_SRCS ${defaulttools_SRCS})
    list(REMOVE_ITEM defaulttools_test_SRCS Plugin.cpp)
    add_executable(TestSingleClickDrag tests/TestSingleClickDrag.cpp ${defaulttools_test_SRCS})
    target_include_directories(TestSingleClickDrag PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/libs/flake/tests)
    target_link_libraries(TestSingleClickDrag PRIVATE flake kowidgets)
    add_test(NAME defaulttools-SingleClickDrag COMMAND TestSingleClickDrag)
    set_tests_properties(defaulttools-SingleClickDrag PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
