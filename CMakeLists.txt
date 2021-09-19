cmake_minimum_required(VERSION 3.13)    #< Highest, used by sub-projects (libheif)

project(FreeImageSidecar VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS ON)               #< Default to shared library

#
# find libheif
#

find_package(Libheif REQUIRED)

#
# find FreeImage, the KISS way - let the user point to header and library
#

find_path(
		FREEIMAGE_INCLUDE_DIR
		NAMES FreeImage.h
		PATHS
      ${FREEIMAGE_HEADER_DIR}
		DOC "FreeImage.h folder")

find_library(
  FREEIMAGE_LIBRARY
  NAMES freeimage, FreeImage
  PATHS
    ${FREEIMAGE_LIBRARY_DIR}
  DOC "FreeImage library folder")

if(NOT FREEIMAGE_INCLUDE_DIR)
  message(FATAL_ERROR "FreeImage.h not found in:\n FREEIMAGE_HEADER_DIR (${FREEIMAGE_HEADER_DIR})")
endif()

if(NOT FREEIMAGE_LIBRARY)
  message(FATAL_ERROR "FreeImage library not found in:\n FREEIMAGE_LIBRARY_DIR (${FREEIMAGE_LIBRARY_DIR})")
endif()

#
# (optional) 
# find lcms2, the KISS way - either look into INSTALL_PREFIX or let the user point to header and library
#

find_path(
		LCMS_INCLUDE_DIR
		NAMES lcms2.h
		PATHS
      "${LCMS_HEADER_DIR}" "${CMAKE_INSTALL_PREFIX}/include"
		DOC "lcms2.h folder")

find_library(
  LCMS_LIBRARY
  NAMES lcms2
  PATHS
    "${CMAKE_INSTALL_PREFIX}/lib" "${LCMS_LIBRARY_DIR}"
  DOC "lcms2 library folder")

if(NOT LCMS_INCLUDE_DIR)
  message("lcms2.h not found in:\n LCMS_HEADER_DIR (${LCMS_HEADER_DIR})\n CMAKE_INSTALL_PREFIX/include (${CMAKE_INSTALL_PREFIX})")
endif()

if(NOT LCMS_LIBRARY)
  message("lcms2 library not found in:\n LCMS_LIBRARY_DIR (${LCMS_LIBRARY_DIR})\n CMAKE_INSTALL_PREFIX/lib (${CMAKE_INSTALL_PREFIX}) ")
endif()

add_library(pluginheif OBJECT 
  "src/PluginHEIF.cpp"
)

if(NOT LCMS_INCLUDE_DIR OR NOT LCMS_LIBRARY)
  message("\n NCLX to ICC color profile conversion will be unavailable.")
else()
  target_compile_definitions(pluginheif PRIVATE FISIDECAR_HAS_LCMS)
endif()

target_include_directories(pluginheif PUBLIC ${FREEIMAGE_INCLUDE_DIR})
target_link_libraries(pluginheif PUBLIC ${FREEIMAGE_LIBRARY} heif)

add_library(fisidecar  
  $<TARGET_OBJECTS:pluginheif> 
  "src/FISidecar.cpp"
) 

if(${BUILD_SHARED_LIBS})
 target_compile_definitions(fisidecar PUBLIC FREEIMAGE_EXPORTS)
 set_target_properties(fisidecar PROPERTIES 
   CXX_VISIBILITY_PRESET hidden 
   POSITION_INDEPENDENT_CODE ON 
   VISIBILITY_INLINES_HIDDEN ON)
else()
 target_compile_definitions(fisidecar PUBLIC FREEIMAGE_LIB)
endif()

target_include_directories(fisidecar PRIVATE ${CMAKE_SOURCE_DIR}/src ${FREEIMAGE_INCLUDE_DIR} ${LCMS_INCLUDE_DIR})
target_link_libraries(fisidecar PRIVATE ${FREEIMAGE_LIBRARY} ${LCMS_LIBRARY} heif)

message("------------------------------------------------")
