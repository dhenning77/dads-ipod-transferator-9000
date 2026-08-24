# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles/dads-ipod-transferator-9000-helper_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/dads-ipod-transferator-9000-helper_autogen.dir/ParseCache.txt"
  "CMakeFiles/dads-ipod-transferator-9000_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/dads-ipod-transferator-9000_autogen.dir/ParseCache.txt"
  "dads-ipod-transferator-9000-helper_autogen"
  "dads-ipod-transferator-9000_autogen"
  )
endif()
