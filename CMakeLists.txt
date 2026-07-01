cmake_minimum_required(VERSION 3.21)

project(RTAssignment LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_XCODE_GENERATE_SCHEME ON)

file(GLOB RT_HEADERS CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/RTAssignment/*.h"
)

set(RT_SOURCES
	RTAssignment/Main.cpp
	${RT_HEADERS}
)

if(APPLE)
	enable_language(OBJCXX)
	list(APPEND RT_SOURCES RTAssignment/GamesEngineeringBase_mac.mm)
endif()

add_executable(RTAssignment ${RT_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/RTAssignment"
	PREFIX "RTAssignment"
	FILES ${RT_SOURCES}
)

target_include_directories(RTAssignment PRIVATE RTAssignment)

if(MSVC)
	target_compile_definitions(RTAssignment PRIVATE NOMINMAX)
	target_link_libraries(RTAssignment PRIVATE
		D3D11
		D3DCompiler
		WindowsCodecs
		xinput
	)
	if(EXISTS "${CMAKE_SOURCE_DIR}/oidn-2.4.1.x64.windows/include/OpenImageDenoise/oidn.hpp")
		target_include_directories(RTAssignment PRIVATE oidn-2.4.1.x64.windows/include)
		target_link_directories(RTAssignment PRIVATE oidn-2.4.1.x64.windows/lib)
		target_link_libraries(RTAssignment PRIVATE OpenImageDenoise OpenImageDenoise_core)
	endif()
endif()

if(APPLE)
	target_link_libraries(RTAssignment PRIVATE
		"-framework Cocoa"
		"-framework CoreGraphics"
		"-framework ImageIO"
		"-framework Metal"
		"-framework MetalKit"
		"-framework QuartzCore"
	)
	set_target_properties(RTAssignment PROPERTIES
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_BUNDLE_NAME "RTAssignment"
		MACOSX_BUNDLE_GUI_IDENTIFIER "com.gamesengineering.rtassignment"
		XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.gamesengineering.rtassignment"
		XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	)
endif()
