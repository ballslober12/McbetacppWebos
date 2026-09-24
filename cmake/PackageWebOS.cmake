# webOS packaging, modelled on moonlight-tv's cmake/PackageWebOS.cmake.
#
#   cmake --build <dir>            # build
#   cmake --build <dir> --target webos-package   # or: cd <dir> && cpack
#
# CPack installs into a staging directory, cmake/StripWebOS.cmake strips the
# staged ELF files, and cmake/AresPackage.cmake runs ares-package and (when
# webosbrew-gen-manifest is available) writes the Homebrew Channel manifest.

# CPACK_PRE_BUILD_SCRIPTS and COMMAND_ERROR_IS_FATAL need CMake 3.19.
if(CMAKE_VERSION VERSION_LESS 3.19)
	message(FATAL_ERROR "webOS packaging needs CMake 3.19 or newer")
endif()

# appinfo.json is generated from deploy/webos/appinfo.json; the only
# substitution is the version, taken from project(VERSION).
configure_file(deploy/webos/appinfo.json "${CMAKE_CURRENT_BINARY_DIR}/appinfo.json" @ONLY)
file(READ "${CMAKE_CURRENT_BINARY_DIR}/appinfo.json" _appinfo)
string(JSON WEBOS_APPINFO_ID GET "${_appinfo}" id)
set(WEBOS_PACKAGE_ARCH "arm")

# App root: executable, appinfo.json, icon, game resources.
install(TARGETS McBetaCpp RUNTIME DESTINATION .)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/appinfo.json" icon.png DESTINATION .)
install(DIRECTORY resource/ DESTINATION resource)

# lib/: only the SDL2 runtime, as one real file named by its SONAME
# (the executable's RPATH is $ORIGIN/lib).
get_target_property(_sdl2_location SDL2::SDL2 IMPORTED_LOCATION_RELEASE)
get_target_property(_sdl2_soname SDL2::SDL2 IMPORTED_SONAME_RELEASE)
if(NOT _sdl2_location OR NOT _sdl2_soname)
	message(FATAL_ERROR "SDL2::SDL2 has no RELEASE location/SONAME")
endif()
install(FILES "${_sdl2_location}" DESTINATION lib RENAME "${_sdl2_soname}")

set(CPACK_PACKAGE_NAME "${WEBOS_APPINFO_ID}")
set(CPACK_GENERATOR "External")
set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/AresPackage.cmake")
set(CPACK_EXTERNAL_ENABLE_STAGING TRUE)
set(CPACK_MONOLITHIC_INSTALL TRUE)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")
# Only names the staging directory; ares-package names the IPK itself from
# appinfo.json (see AresPackage.cmake).
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}_${WEBOS_PACKAGE_ARCH}")
set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_SOURCE_DIR}/cmake/StripWebOS.cmake")
# Stripping is done by StripWebOS.cmake so the bundled SDL2 is covered too.
set(CPACK_STRIP_FILES FALSE)

configure_file("${CMAKE_SOURCE_DIR}/cmake/CPackConfig.webOS.cmake.in" CPackConfig.webOS.cmake @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.webOS.cmake")

add_custom_target(webos-package COMMAND "${CMAKE_CPACK_COMMAND}" DEPENDS McBetaCpp
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" VERBATIM)

find_program(WEBOSBREW_IPK_VERIFY webosbrew-ipk-verify)
if(WEBOSBREW_IPK_VERIFY)
	add_custom_target(webos-verify
		COMMAND sh -c "exec \"$0\" -d ${CPACK_PACKAGE_NAME}_*_${WEBOS_PACKAGE_ARCH}.ipk" "${WEBOSBREW_IPK_VERIFY}"
		WORKING_DIRECTORY "${CPACK_PACKAGE_DIRECTORY}"
		DEPENDS webos-package VERBATIM)
endif()

include(CPack)
