# ---------------------------------------------------------------------------
# LegendNetworkTests：阶段9 网络验收（独立 Console，不依赖 SDL——指令一百二十九）
# ---------------------------------------------------------------------------
add_executable(LegendNetworkTests
    NetworkChecks.cpp
    ../Client/Network/GameNetworkClient.cpp
    ../Server/Gateway/GatewayServer.cpp
    ../Server/Gateway/GatewaySession.cpp
    ../Server/LoginServer/LoginServer.cpp
    ../Engine/Debug/Logger.cpp
)

target_include_directories(LegendNetworkTests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_features(LegendNetworkTests PRIVATE cxx_std_20)
target_link_libraries(LegendNetworkTests PRIVATE LegendNetworkTransport)

if(MSVC)
    target_compile_options(LegendNetworkTests PRIVATE /utf-8)
endif()

enable_testing()
add_test(NAME NetworkTests COMMAND LegendNetworkTests)
