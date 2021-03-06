cmake_minimum_required(VERSION 3.1)
project(wifibroadcast_bridge)

# Set the version
set(VERSION "2.2.2")

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
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O3 -DNDEBUG")

# Add the local include directory to the include path
include_directories(${PROJECT_SOURCE_DIR}/include)

# Add the usr directory to most of the paths if the installcation directory is /
set(BinDir "usr/bin")
set(LibDir "usr/lib")
set(IncludeDir "usr/include")

# We have submodule, so we need git to update the submodule
find_package(Git QUIET)

# Update submodules as needed
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
  option(GIT_SUBMODULE "Check submodules during build" OFF)
  if(GIT_SUBMODULE)
    message(STATUS "Submodule update")
    execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      RESULT_VARIABLE GIT_SUBMOD_RESULT)
    if(NOT GIT_SUBMOD_RESULT EQUAL "0")
      message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
    endif()
  endif()
endif()

# Ensure the submodule directories were checked out successfully
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/modules/tinyformat/tinyformat.h")
  message(FATAL_ERROR "The tinyformat submodule was not downloaded! Please update submodules and try again.")
endif()
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/modules/inih/INIReader.h")
  message(FATAL_ERROR "The inih submodule was not downloaded! Please update submodules and try again.")
endif()
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/modules/civetweb/include/civetweb.h")
  message(FATAL_ERROR "The civetweb submodule was not downloaded! Please update submodules and try again.")
endif()

# Add the log4cpp package
find_package(log4cpp REQUIRED)
include_directories(${LOG4CPP_INCLUDE_DIR})

# Add the tinyformat submodule directory
add_subdirectory(modules/tinyformat)
include_directories(modules/tinyformat)

# Add the civetweb submodule directory
#option(BUILD_SHARED_LIBS "" ON)
#option(CIVETWEB_BUILD_TESTING "" OFF)
#option(CIVETWEB_ENABLE_CXX "" ON)
#add_subdirectory(modules/civetweb)
include_directories(modules/civetweb/include)
set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -DNO_SSL")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNO_SSL")

# Add the inih submodule directory
include_directories(modules/inih)

# Find the pcap library
find_package(PCAP)
if(${HAVE_PCAP_IMMEDIATE_MODE})
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHAVE_PCAP_IMMEDIATE_MODE")
endif()

# Find the pthread library
find_package(Threads)

# Add the libnl package
find_package(LibNL REQUIRED)
include_directories(${LibNL_INCLUDE_DIR})

# Find the udev library
find_package(udev REQUIRED)
include_directories(${UDEV_INCLUDE_DIR})

# Add the radiotap source
include_directories(src/radiotap)

# Create the library
add_library(wifibroadcast
  SHARED
  src/raw_socket.cc
  src/fec.c
  src/fec.cc
  src/radiotap/radiotap.c
  src/transfer_stats.cc)
target_link_libraries(wifibroadcast ${PCAP_LIBRARY} ${LOG4CPP_LIBRARIES} ${LibNL_LIBRARY} ${LibNL_GENL_LIBRARY})
install(TARGETS wifibroadcast DESTINATION ${LibDir} COMPONENT applications)

# Build the wfb_bridge program
add_executable(wfb_bridge
  src/wfb_bridge.cc
  src/transfer_stats.cc
  src/udp_send.cc
  src/udp_receive.cc
  src/log_thread.cc
  src/raw_send_thread.cc
  src/udev_interface.cc
  src/tun_interface.cc
  src/http_server.cc
  modules/civetweb/src/civetweb.c
  modules/civetweb/src/CivetServer.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/version.cc)
install(TARGETS wfb_bridge DESTINATION ${BinDir} COMPONENT applications)
target_link_libraries(wfb_bridge ${PCAP_LIBRARY} ${LOG4CPP_LIBRARIES} ${LibNL_LIBRARY} ${LibNL_GENL_LIBRARY} ${UDEV_LIBRARY} wifibroadcast pthread)

# Build the FEC test program
add_executable(test_fec src/test_fec.cc)
target_link_libraries(test_fec wifibroadcast ${LOG4CPP_LIBRARIES})

# Build the raw socket test program
add_executable(raw_socket_test src/raw_socket_test.cc)
target_link_libraries(raw_socket_test wifibroadcast ${LOG4CPP_LIBRARIES} ${LibNL_LIBRARY} ${LibNL_GENL_LIBRARY})

# Add rules to install the scripts
install(PROGRAMS
  scripts/set_rtl8812au_txpower
  DESTINATION ${BinDir} COMPONENT applications)

# Add rules to install the configuration files
install(FILES conf/wfb_bridge.conf DESTINATION etc COMPONENT configuration)

# Add rules to install the services file
install(FILES services/wfb_bridge.service DESTINATION lib/systemd/system COMPONENT configuration)

# Add rules to install the NetworkManager wifi ignore list
install(FILES conf/wifi_ignore.conf DESTINATION etc/NetworkManager/conf.d COMPONENT configuration)

# Add rules to install the wifi module configuration files
install(FILES etc/modprobe.d/ath9k_hw.conf DESTINATION etc/modprobe.d COMPONENT configuration)
install(FILES etc/modprobe.d/rtl8812au.conf DESTINATION etc/modprobe.d COMPONENT configuration)
# This file conflicts with the standard configuration file and prevents the .deb file from loading.
# Replace the standard configuration file with a link to this file after installation.
install(FILES etc/modprobe.d/ath9k_htc.conf.openhd DESTINATION etc/modprobe.d COMPONENT configuration)
install(DIRECTORY lib/firmware/ath9k_htc DESTINATION lib/firmware COMPONENT configuration)

# Install the include files
install(FILES
  include/wifibroadcast/fec.h
  include/wifibroadcast/fec.hh
  include/wifibroadcast/raw_socket.hh
  include/wifibroadcast/transfer_stats.hh
  DESTINATION ${IncludeDir}/wifibroadcast COMPONENT development)

# Add uninstall target
add_custom_target(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_MODULE_PATH}/uninstall.cmake")

# Add the debian debian package dependencies
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libpcap0.8")

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
