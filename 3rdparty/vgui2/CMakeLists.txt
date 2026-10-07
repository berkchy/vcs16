cmake_minimum_required(VERSION 3.10)

# Valve VGUI2 (GoldSource) static library for the CS client.
# Mirrors the source selection and defines of xash3d-fwgs 3rdparty/vgui2.

project(vgui2client)

file(GLOB VGUI2_CONTROLS_SRC "core/vgui_controls/*.cpp")
file(GLOB VGUI2_TIER1_SRC "src/tier1/*.cpp")
file(GLOB VGUI2_TIER2_SRC "src/tier2/*.cpp")
file(GLOB VGUI2_RUNTIME_SRC "runtime/*.cpp")

# Sources that don't belong to a GoldSource build (editor/perforce-only
# panels, or files outside the SDK module lists).
set(VGUI2_EXCLUDE
	CircularProgressBar.cpp
	consoledialog.cpp
	CvarToggleCheckButton.cpp
	cvarToggleCheckButton.cpp
	cvartogglecheckbutton.cpp
	FileOpenStateMachine.cpp
	PerforceFileExplorer.cpp
	PerforceFileList.cpp
	perforcefilelistframe.cpp
	RotatingProgressBar.cpp
	ScalableImagePanel.cpp
	subrectimage.cpp
	fileio.cpp
	undiff.cpp
	interface.cpp
	NetAdr.cpp
	# Dead weight with platform deps nothing references: reliabletimer
	# needs tier0's GetCPUInformation, uniqueid needs Rpcrt4 on Windows.
	reliabletimer.cpp
	uniqueid.cpp
)

# common/interface.cpp (built into the client) already provides
# InterfaceReg, CreateInterface and the Sys_* module helpers, so the full
# tier1/interface.cpp stays out (duplicate symbols). interface_client.cpp
# provides only the two-arg Sys_LoadModule that steam_api.cpp needs.
foreach(f ${VGUI2_EXCLUDE})
	list(REMOVE_ITEM VGUI2_CONTROLS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/core/vgui_controls/${f}")
	list(REMOVE_ITEM VGUI2_TIER1_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/tier1/${f}")
endforeach()

if(MSVC)
	# ucrt already provides qsort_s; keep only one definition.
	list(REMOVE_ITEM VGUI2_TIER1_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/tier1/qsort_s.cpp")
endif()

add_library(vgui2client SHARED
	${VGUI2_CONTROLS_SRC}
	${VGUI2_TIER1_SRC}
	${VGUI2_TIER2_SRC}
	${VGUI2_RUNTIME_SRC}
	core/src/vgui_key_translation.cpp
	public/filesystem_helpers.cpp
	public/vgui_controls/vgui_controls.cpp
	# Client-side glue (bootstrap + scoreboard panel). Built here, not in
	# the client library: this keeps Valve UTL classes (which collide with
	# the client's miniutl copies) inside one self-consistent module.
	../../cl_dll/VGUI/vgui2_client.cpp
	../../cl_dll/VGUI/cstrikeclientscoreboard.cpp
)

target_include_directories(vgui2client PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/public
	${CMAKE_CURRENT_SOURCE_DIR}/public/tier0
	${CMAKE_CURRENT_SOURCE_DIR}/public/tier1
	${CMAKE_CURRENT_SOURCE_DIR}/public/common
)

# engine/ for mobility_int.h used by the client glue above
target_include_directories(vgui2client PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../engine
)
target_include_directories(vgui2client PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../engine
)

set_target_properties(vgui2client PROPERTIES
	CXX_STANDARD 11
	POSITION_INDEPENDENT_CODE ON
	C_VISIBILITY_PRESET hidden
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
)

find_package(Threads REQUIRED)
target_link_libraries(vgui2client PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

install(TARGETS vgui2client
	DESTINATION "${GAME_DIR}/${CLIENT_INSTALL_DIR}/"
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
	    GROUP_READ GROUP_EXECUTE
		WORLD_READ WORLD_EXECUTE)

target_compile_definitions(vgui2client PRIVATE
	NO_MALLOC_OVERRIDE
	MATHLIB_HEADER_ONLY
	TIER1_STATIC_LIB
	STATIC_TIER0
	NO_VCR
)

if(MSVC)
	target_compile_definitions(vgui2client PRIVATE
		WIN32
		STEAM_API_NODLL
		_DLL_EXT=.dll
	)
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		target_compile_definitions(vgui2client PRIVATE WIN64 X64BITS)
	endif()
else()
	target_compile_definitions(vgui2client PRIVATE
		GNUC
		POSIX
	)
	if(APPLE)
		target_compile_definitions(vgui2client PRIVATE OSX _DLL_EXT=.dylib)
	else()
		target_compile_definitions(vgui2client PRIVATE LINUX _LINUX _DLL_EXT=.so)
	endif()
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		target_compile_definitions(vgui2client PRIVATE X64BITS)
	endif()

	# 32-bit x86 defaults to no SSE2, but the mathlib vector ops behind the
	# panel code are SSE intrinsics, and GCC refuses to inline the
	# always_inline ones without it ("target specific option mismatch").
	if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86)$")
		target_compile_options(vgui2client PRIVATE -msse2)
	endif()
endif()

# macOS ships the GNU iconv in libiconv, not in libSystem, so the UCS-2
# conversions in strtools.cpp need it named explicitly.
if(APPLE)
	target_link_libraries(vgui2client PRIVATE iconv)
endif()

# UTL strings/buffers duplicate the client's miniutl ones (same mangled
# names, different layouts). This target is a separate shared library so
# each module stays self-consistent; hidden visibility prevents the
# dynamic linker from interposing across the boundary.
