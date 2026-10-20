# M2: Drogon HTTP/WS server. Увімкнення: -DHOLOS_WITH_SERVER=ON.
# Дзеркалить REST/OpenAI/WS API Karnemorkon/HolosTTS-hybrid.
add_executable(holos-server
  src/main.cpp
)
if(HOLOS_USE_SYSTEM_DROGON)
  target_link_libraries(holos-server PRIVATE holos_core Drogon::Drogon)
else()
  target_link_libraries(holos-server PRIVATE holos_core drogon trantor)
endif()
target_compile_definitions(holos-server PRIVATE HOLOS_VERSION="${PROJECT_VERSION}")
