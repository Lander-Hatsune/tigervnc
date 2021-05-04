include_directories(${CMAKE_SOURCE_DIR}/common)

set(QUICHE_SOURCES
  quicheServer.cxx
  quicheClient.cxx
  )

add_library(blink STATIC ${QUICHE_SOURCES})
target_link_libraries(blink ${CMAKE_SOURCE_DIR}/common/blink/libquiche.a dl m)

if(WIN32)
  target_link_libraries(blink ws2_32)
endif()

if(UNIX)
  libtool_create_control_file(blink)
endif()
