# VolumeControlCLI/vccli
cmake_minimum_required(VERSION 3.20)

# Get headers & source files
file(GLOB HEADERS
	RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
	CONFIGURE_DEPENDS
	"*.h*"
)
file(GLOB SRCS
	RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
	CONFIGURE_DEPENDS
	"*.c*"
)

# Create resources:
set(vccli_rc_dir "${CMAKE_CURRENT_SOURCE_DIR}/rc")

include(VersionTag)
MAKE_VERSION_HEADER("${vccli_rc_dir}/version.h" vccli "${VolumeControlCLI_VERSION}")

if (WIN32) # Windows-specific resources:
	include(ResourceMaker)
	MAKE_STRINGRC_ICON(vccli_rc_icon "${vccli_rc_dir}/vccli.ico")
	MAKE_STRINGRC_VERSIONINFO(vccli_rc_versioninfo
		"${VolumeControlCLI_VERSION}"
		"GNU General Public License v3"
		"radj307"
		"Volume Control CLI"
		"Windows commandline utility that can mute/unmute/change the volume of specific processes."
	)
	set(vccli_rc_file "${vccli_rc_dir}/vccli.rc")
	MAKE_RESOURCE("${vccli_rc_file}" "${vccli_rc_icon}" "${vccli_rc_versioninfo}")
endif()



include(FetchContent)
FetchContent_Declare(
	doctest
	GIT_REPOSITORY https://github.com/doctest/doctest.git
	GIT_TAG v2.4.9
)
FetchContent_MakeAvailable(doctest)




# Create executable
add_executable(vccli "${SRCS}" "${vccli_rc_dir}/version.h" "${vccli_rc_file}")

set_property(TARGET vccli PROPERTY CXX_STANDARD 20)
set_property(TARGET vccli PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET vccli PROPERTY POSITION_INDEPENDENT_CODE ON)

if (MSVC)
	target_compile_options(vccli PRIVATE "/Zc:__cplusplus" "/Zc:preprocessor" "${vccli_DBGMODE}")
endif()

target_sources(vccli PRIVATE "${HEADERS}")

target_link_libraries(vccli PRIVATE TermAPI optlib doctest)

# Create an installation target:
include(PackageInstaller)
INSTALL_EXECUTABLE(vccli "${CMAKE_INSTALL_PREFIX}/")


if(BUILD_TESTING)
	include(CTest)
	enable_testing()
	target_compile_definitions(vccli PUBLIC DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
	include(doctestAddTests)
	doctest_discover_tests(vccli)
else()
	target_compile_definitions(vccli PUBLIC DOCTEST_CONFIG_DISABLE)
endif()

