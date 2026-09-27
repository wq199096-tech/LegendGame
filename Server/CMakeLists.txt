# ---------------------------------------------------------------------------
# LegendGateway / LegendLoginServer：纯 Console 服务器（阶段9 指令八十六）
# 不 link legend_engine（SDL/OpenGL）——Logger.cpp 直接编入（仅依赖 std）。
# ---------------------------------------------------------------------------
add_executable(LegendGateway
    ../Apps/GatewayMain.cpp
    Gateway/GatewayServer.cpp
    Gateway/GatewaySession.cpp
    ../Engine/Debug/Logger.cpp
)

target_include_directories(LegendGateway PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_features(LegendGateway PRIVATE cxx_std_20)
target_link_libraries(LegendGateway PRIVATE LegendNetworkTransport)

if(MSVC)
    target_compile_options(LegendGateway PRIVATE /utf-8)
endif()

add_executable(LegendLoginServer
    ../Apps/LoginServerMain.cpp
    LoginServer/LoginServer.cpp
    ../Engine/Debug/Logger.cpp
)

target_include_directories(LegendLoginServer PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_features(LegendLoginServer PRIVATE cxx_std_20)
target_link_libraries(LegendLoginServer PRIVATE LegendNetworkTransport)

if(MSVC)
    target_compile_options(LegendLoginServer PRIVATE /utf-8)
endif()
