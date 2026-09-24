# CPack External generator script: build the IPK from the staging directory
# and generate the Homebrew Channel manifest (with ipkHash) next to it.
#
# ares-package names the IPK from appinfo.json, so package into an empty
# scratch directory and take whatever single .ipk it produced.
set(_out "${CPACK_TOPLEVEL_DIRECTORY}/ares-package")
file(REMOVE_RECURSE "${_out}")
file(MAKE_DIRECTORY "${_out}")
execute_process(COMMAND ares-package "${CPACK_TEMPORARY_DIRECTORY}" -o "${_out}"
	--force-arch "${CPACK_WEBOS_PACKAGE_ARCH}"
	COMMAND_ERROR_IS_FATAL ANY
)
file(GLOB _ipks "${_out}/*.ipk")
list(LENGTH _ipks _count)
if(NOT _count EQUAL 1)
	message(FATAL_ERROR "Expected one IPK from ares-package, got: ${_ipks}")
endif()
get_filename_component(_ipk_name "${_ipks}" NAME)
set(_ipk "${CPACK_PACKAGE_DIRECTORY}/${_ipk_name}")
file(REMOVE "${_ipk}")
file(RENAME "${_ipks}" "${_ipk}")
message(STATUS "IPK: ${_ipk}")

find_program(GEN_MANIFEST webosbrew-gen-manifest)
if(NOT GEN_MANIFEST)
	message(STATUS "webosbrew-gen-manifest not found, skipping manifest generation")
	return()
endif()

# manifest.json is the name the apps-repo listing points at
# (releases/latest/download/manifest.json).
execute_process(COMMAND "${GEN_MANIFEST}"
	-p "${_ipk}"
	-o "${CPACK_PACKAGE_DIRECTORY}/manifest.json"
	-i "${CPACK_WEBOS_MANIFEST_ICON}"
	-l "${CPACK_WEBOS_MANIFEST_LINK}"
	COMMAND_ERROR_IS_FATAL ANY
)
