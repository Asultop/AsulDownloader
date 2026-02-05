# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "AsulDownloader_autogen"
  "CMakeFiles\\AsulDownloader_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\AsulDownloader_autogen.dir\\ParseCache.txt"
  )
endif()
