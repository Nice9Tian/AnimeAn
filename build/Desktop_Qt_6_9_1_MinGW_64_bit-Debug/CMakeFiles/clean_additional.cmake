# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "AnimeAn_autogen"
  "CMakeFiles\\AnimeAn_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\AnimeAn_autogen.dir\\ParseCache.txt"
  )
endif()
