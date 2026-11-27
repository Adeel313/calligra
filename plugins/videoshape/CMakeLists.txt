add_definitions(-DTRANSLATION_DOMAIN=\"calligra_shape_video\")

include_directories(
    ${FLAKE_INCLUDES}
    ${KOWIDGETS_INCLUDES}
)

set (videoshape_LIB_SRCS
    VideoDebug.cpp
    VideoData.cpp
    VideoCollection.cpp
    VideoShape.cpp
    VideoShapeFactory.cpp
    VideoEventAction.cpp
    Plugin.cpp
    VideoShapeConfigWidget.cpp
    FullScreenPlayer.cpp
    VideoTool.cpp
    VideoToolFactory.cpp
    ChangeVideoCommand.cpp
    SelectVideoWidget.cpp
)

if(TARGET Phonon::phonon4qt6experimental AND PHONON_FOUND_EXPERIMENTAL)
    list(APPEND videoshape_LIB_SRCS VideoThumbnailer.cpp)
endif()

ki18n_wrap_ui(videoshape_LIB_SRCS forms/VideoToolWidget.ui)
add_library(calligra_shape_video MODULE ${videoshape_LIB_SRCS})

target_link_libraries(calligra_shape_video
    flake
    kowidgets
    KF6::KIOFileWidgets
    Qt6::MultimediaWidgets
)
if(TARGET Phonon::phonon4qt6experimental AND PHONON_FOUND_EXPERIMENTAL)
    target_compile_definitions(calligra_shape_video PRIVATE SHOULD_BUILD_THUMBNAIL)
    target_link_libraries(calligra_shape_video Phonon::phonon4qt6 Phonon::phonon4qt6experimental)
endif()

install(TARGETS calligra_shape_video DESTINATION ${KDE_INSTALL_PLUGINDIR}/calligra/shapes)
