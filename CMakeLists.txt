cmake_minimum_required(VERSION 3.5)
project(wifibroadcast_bridge)

# Set the version
set(VERSION "0.9.12")

# Default the CMAKE_INSTALL_PREFIX
IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  SET(CMAKE_INSTALL_PREFIX "/" CACHE PATH "Change the default installation director (default=/)" FORCE)
ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

# Generate version information
ADD_CUSTOM_COMMAND(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.cc ${CMAKE_CURRENT_BINARY_DIR}/_version.cc
  COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/version.cmake
  )
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/version ${VERSION})

# Add the local cmake directory to the modules path
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# Define some default CFLAGS
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O3 -DNDEBUG -DBOOST_LOG_DYN_LINK")

# Add the local include directory to the include path
include_directories(${PROJECT_SOURCE_DIR}/include)

# Add the usr directory to most of the paths if the installcation directory is /
set(BinDir "usr/bin")
set(LibDir "usr/lib")
set(IncludeDir "usr/include")
set(PythonDistDir "dist-packages")

# Find the boost libraries
find_package(Boost REQUIRED COMPONENTS
  program_options
  system
  filesystem
  log)
include_directories(${Boost_INCLUDE_DIR})

# Find the pcap library
find_package(PCAP)

# Find the pthread library
find_package(Threads)

# Find the python3 interpreter
if (${CMAKE_VERSION} VERSION_LESS "3.12.0")
  set(Python_ADDITIONAL_VERSIONS "3")
  set(PYTHON_EXECUTABLE "/usr/bin/python3")
  find_package(PythonInterp 3.0 REQUIRED)
  set(RELATIVE_PYTHON_DIR "${LibDir}/python${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}/${PythonDistDir}/wifibroadcast")
else ()
  find_package(Python3 COMPONENTS Interpreter)
  set(RELATIVE_PYTHON_DIR "${LibDir}/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/${PythonDistDir}/wifibroadcast")
endif ()
set(PYTHON_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/${RELATIVE_PYTHON_DIR}")

# Add the radiotap source
include_directories(src/radiotap)

# Create the library
add_library(wifibroadcast
  SHARED
  src/raw_socket.cc
  src/fec.c
  src/fec.cc
  src/logging.cc
  src/radiotap/radiotap.c)
target_link_libraries(wifibroadcast ${Boost_LIBRARIES} ${PCAP_LIBRARY})
install(TARGETS wifibroadcast DESTINATION ${LibDir} COMPONENT applications)

# Build the wfb_bridge program
add_executable(wfb_bridge
  src/wfb_bridge.cc
  src/transfer_stats.cc
  src/udp_send.cc
  src/udp_receive.cc
  src/log_thread.cc
  src/raw_send_thread.cc
  ${CMAKE_CURRENT_BINARY_DIR}/version.cc)
install(TARGETS wfb_bridge DESTINATION ${BinDir} COMPONENT applications)
target_link_libraries(wfb_bridge ${Boost_LIBRARIES} ${PCAP_LIBRARY} wifibroadcast pthread)

# Build the FEC test program
add_executable(test_fec src/test_fec.cc)
target_link_libraries(test_fec ${Boost_LIBRARIES} wifibroadcast)

# Add rules to install the python executables
install(PROGRAMS python/wifi_config DESTINATION ${BinDir} COMPONENT applications)

# Add rules to install the python files
install(DIRECTORY python/ DESTINATION ${RELATIVE_PYTHON_DIR} COMPONENT application
  FILES_MATCHING PATTERN "*.py")

# Add rules to install the configuration files
install(FILES conf/wfb_bridge DESTINATION etc/default COMPONENT configuration)

# Add rules to install the services files
install(FILES services/wfb_bridge_ground.service DESTINATION lib/systemd/system COMPONENT configuration)
install(FILES services/wfb_bridge_air.service DESTINATION lib/systemd/system COMPONENT configuration)

# Add rules to install the NetworkManager wifi ignore list
install(FILES conf/wifi_ignore.conf DESTINATION etc/NetworkManager/conf.d COMPONENT configuration)

# Add rules to install the wifi module configuration files
install(FILES etc/modprobe.d/ath9k_hw.conf DESTINATION etc/modprobe.d COMPONENT configuration)
install(FILES etc/modprobe.d/rtl88au.conf DESTINATION etc/modprobe.d COMPONENT configuration)
# This file conflicts with the standard configuration file and prevents the .deb file from loading.
# Replace the standard configuration file with a link to this file after installation.
install(FILES etc/modprobe.d/ath9k_htc.conf.openhd DESTINATION etc/modprobe.d COMPONENT configuration)
install(DIRECTORY lib/firmware/ath9k_htc DESTINATION lib/firmware COMPONENT configuration)

# Install the include files
install(FILES
  include/fec.h
  include/fec.hh
  include/raw_socket.hh
  DESTINATION ${IncludeDir}/wifibroadcast COMPONENT development)

# Add uninstall target
add_custom_target(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_MODULE_PATH}/uninstall.cmake")

# Add the debian debian package dependencies
set(CPACK_DEBIAN_PACKAGE_DEPENDS "python3-pyudev,libpcap0.8,libboost-program-options-dev,libboost-system-dev,libboost-filesystem-dev,libboost-log-dev,firmware-ath9k-htc")

# Add rules to build a debian package
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "webbbn@gmail.con")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_PACKAGE_VERSION ${VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY /)
set(CPACK_SET_DESTDIR true)
set(CPACK_INSTALL_PREFIX /)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/scripts/postinst")
include(CPack)
