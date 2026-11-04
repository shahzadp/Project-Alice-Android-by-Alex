# cmake/AliceHostTools.cmake
#
# ParserGenerator, DCONGENERATOR, DCONINTERFACEGEN, DataContainer-Lua and
# DataContainer_OOS_Reporter are run DURING the build (via
# add_custom_command) to generate .hpp files that the rest of the code depends on. That
# means they must be HOST binaries -- if CMake compiles them
# with the NDK toolchain (cross-compiling), the resulting binary is ARM and does not
# run on the PC/host to generate the file, stalling the whole build.
#
# Flow for Android (or any cross-compile):
#   1) Normal native build first, only for those 5 tools:
#        cmake --preset x64-debug-linux-clang
#        cmake --build --preset x64-debug-linux-clang --target \
#            ParserGenerator DCONGENERATOR DCONINTERFACEGEN \
#            DataContainer-Lua DataContainer_OOS_Reporter
#   2) Copy the 5 generated binaries to a separate, "flattened" folder, e.g.:
#        mkdir -p native-tools
#        find out/build/x64-debug-linux-clang -type f \
#            \( -name ParserGenerator -o -name DCONGENERATOR -o -name DCONINTERFACEGEN \
#               -o -name DataContainer-Lua -o -name DataContainer_OOS_Reporter \) \
#            -exec cp {} native-tools/ \;
#   3) Configure the Android preset pointing to that folder:
#        cmake --preset android-arm64 -DALICE_NATIVE_TOOLS_DIR=$(pwd)/native-tools
#
# alice_import_host_tool(<target-name>) creates an IMPORTED target with that name
# pointing to the already compiled binary -- the existing add_custom_command/add_dependencies
# that reference that name keep working with no change at all,
# because CMake resolves by target name, no matter whether it is IMPORTED or not.

set(ALICE_NATIVE_TOOLS_DIR "" CACHE PATH
	"Folder with the pre-compiled host binaries of ParserGenerator/DCONGENERATOR/DCONINTERFACEGEN/DataContainer-Lua/DataContainer_OOS_Reporter (needed when cross-compiling, e.g: Android)")

function(alice_import_host_tool TOOL_NAME)
	if(TARGET ${TOOL_NAME})
		return() # already exists (the normal native build took care of it) -- nothing to do
	endif()

	if(NOT ALICE_NATIVE_TOOLS_DIR)
		message(FATAL_ERROR
			"Cross-compiling (CMAKE_CROSSCOMPILING=ON) but ALICE_NATIVE_TOOLS_DIR was not defined.\n"
			"'${TOOL_NAME}' runs DURING the build to generate code and must be a HOST "
			"binary, not ${CMAKE_SYSTEM_NAME}${CMAKE_ANDROID_ARCH_ABI}.\n"
			"Build it natively first (desktop preset) and point -DALICE_NATIVE_TOOLS_DIR=<folder>. "
			"See the comment at the top of cmake/AliceHostTools.cmake for the step by step.")
	endif()

	find_program(ALICE_${TOOL_NAME}_PATH
		NAMES ${TOOL_NAME}
		PATHS "${ALICE_NATIVE_TOOLS_DIR}"
		NO_DEFAULT_PATH)

	if(NOT ALICE_${TOOL_NAME}_PATH)
		message(FATAL_ERROR
			"Could not find a host binary named '${TOOL_NAME}' inside "
			"ALICE_NATIVE_TOOLS_DIR='${ALICE_NATIVE_TOOLS_DIR}'. Build it natively first "
			"(see the comment at the top of cmake/AliceHostTools.cmake).")
	endif()

	add_executable(${TOOL_NAME} IMPORTED GLOBAL)
	set_target_properties(${TOOL_NAME} PROPERTIES IMPORTED_LOCATION "${ALICE_${TOOL_NAME}_PATH}")
	message(STATUS "${TOOL_NAME}: using the pre-compiled host binary at ${ALICE_${TOOL_NAME}_PATH}")
endfunction()
