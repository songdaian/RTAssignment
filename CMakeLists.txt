cmake_minimum_required(VERSION 3.21)

project(RTAssignment LANGUAGES CXX)

include(CTest)

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

if(BUILD_TESTING)
	add_executable(VolumeTests RTAssignment/VolumeTests.cpp)
	target_include_directories(VolumeTests PRIVATE RTAssignment)
	add_test(NAME volumetric_sampling COMMAND VolumeTests)
endif()

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
