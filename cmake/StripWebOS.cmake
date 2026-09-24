# CPack pre-build script: strip every ELF file in the staging directory
# (the executable and lib/*.so*) with the toolchain's strip.
if(NOT CPACK_WEBOS_STRIP_COMMAND)
	message(FATAL_ERROR "CPACK_WEBOS_STRIP_COMMAND is not set")
endif()

file(GLOB _binaries "${CPACK_TEMPORARY_DIRECTORY}/McBetaCpp" "${CPACK_TEMPORARY_DIRECTORY}/lib/*.so*")
foreach(_binary IN LISTS _binaries)
	if(IS_SYMLINK "${_binary}")
		message(FATAL_ERROR "Unexpected symlink in package: ${_binary}")
	endif()
	message(STATUS "Strip ${_binary}")
	execute_process(COMMAND "${CPACK_WEBOS_STRIP_COMMAND}" --strip-all "${_binary}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
