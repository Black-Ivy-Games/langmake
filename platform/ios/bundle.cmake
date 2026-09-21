# Application packaging only; SDK, identity and provisioning belong to the
# caller's Xcode environment and are never downloaded or selected implicitly.
function(lm_plist_escape input output)
    set(value "${input}")
    string(REPLACE "&" "&amp;" value "${value}")
    string(REPLACE "<" "&lt;" value "${value}")
    string(REPLACE ">" "&gt;" value "${value}")
    string(REPLACE "\"" "&quot;" value "${value}")
    set(${output} "${value}" PARENT_SCOPE)
endfunction()
lm_plist_escape("${LM_IOS_LABEL}" LM_IOS_PLIST_LABEL)
if(NOT DEFINED LM_IOS_VERSION)
    set(LM_IOS_VERSION "${PROJECT_VERSION}")
endif()
if(NOT DEFINED LM_IOS_BUILD_NUMBER)
    set(LM_IOS_BUILD_NUMBER 401)
endif()
set(LM_IOS_ICON_PLIST "")
if(EXISTS "${LM_IOS_CONTENT_DIRECTORY}/runtime/application/Assets.xcassets")
    set(LM_IOS_ICON_PLIST "<key>CFBundleIcons</key><dict><key>CFBundlePrimaryIcon</key><dict><key>CFBundleIconName</key><string>AppIcon</string></dict></dict>")
    set(icon_catalog "${LM_IOS_CONTENT_DIRECTORY}/runtime/application/Assets.xcassets")
    set_source_files_properties("${icon_catalog}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(langmake PRIVATE "${icon_catalog}")
    set_target_properties(langmake PROPERTIES XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
endif()
set(LM_IOS_ORIENTATIONS "")
if(LM_IOS_ORIENTATION STREQUAL "portrait" OR LM_IOS_ORIENTATION STREQUAL "sensor")
    string(APPEND LM_IOS_ORIENTATIONS "<string>UIInterfaceOrientationPortrait</string>")
endif()
if(LM_IOS_ORIENTATION STREQUAL "landscape" OR LM_IOS_ORIENTATION STREQUAL "sensor")
    string(APPEND LM_IOS_ORIENTATIONS "<string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string>")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/Info.plist.in" "${CMAKE_BINARY_DIR}/ios/Info.plist" @ONLY)
set_target_properties(langmake PROPERTIES
    # iOS places the executable at the bundle root, beside langmake/ content.
    # Using the same name for both makes the linker target a directory.
    OUTPUT_NAME "LangmakePlayer"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/ios/Info.plist"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${LM_IOS_BUNDLE_IDENTIFIER}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${LM_IOS_BUILD_NUMBER}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${LM_IOS_VERSION}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${LM_IOS_BUNDLE_IDENTIFIER}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_ENABLE_BITCODE NO
    XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST NO
    XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE NO
    XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES YES)
file(GLOB_RECURSE LM_IOS_CONTENT_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false "${LM_IOS_CONTENT_DIRECTORY}/*")
foreach(resource IN LISTS LM_IOS_CONTENT_FILES)
    file(RELATIVE_PATH relative "${LM_IOS_CONTENT_DIRECTORY}" "${resource}")
    get_filename_component(folder "${relative}" DIRECTORY)
    # CMake maps Resources to the root of an iOS app bundle.
    set_source_files_properties("${resource}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/langmake/${folder}")
    target_sources(langmake PRIVATE "${resource}")
endforeach()
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/LaunchScreen.storyboard" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(langmake PRIVATE "${CMAKE_CURRENT_LIST_DIR}/LaunchScreen.storyboard")
