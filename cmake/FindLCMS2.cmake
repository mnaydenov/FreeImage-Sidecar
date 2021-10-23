find_path(
  LCMS2_INCLUDE_DIR 
  NAMES lcms2.h
  DOC "lcms2.h folder"
  )

find_library(
  LCMS2_LIBRARY 
  NAMES lcms2 
  DOC "lcms2 library folder"
  )

mark_as_advanced(LCMS2_INCLUDE_DIR LCMS2_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(lcms2 DEFAULT_MSG LCMS2_LIBRARY LCMS2_INCLUDE_DIR)

if(LCMS2_FOUND)
  set(LCMS2_INCLUDE_DIRS ${LCMS2_INCLUDE_DIR})
  set(LCMS2_LIBRARIES ${LCMS2_LIBRARY} )
endif()