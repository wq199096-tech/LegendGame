# ---------------------------------------------------------------------------
# LegendNetworkTransport：asio standalone 传输层静态库
#（TcpConnection / TcpClient / TcpServer / NetworkService）
# 依赖：LegendSharedNetwork + asio（header-only）。不依赖 legend_engine/SDL
# —— Gateway/LoginServer 与 LegendClient 共用本库。
# ---------------------------------------------------------------------------
add_library(LegendNetworkTransport STATIC
    TcpConnection.cpp
    TcpClient.cpp
    TcpServer.cpp
    NetworkService.cpp
)

target_include_directories(LegendNetworkTransport PUBLIC ${PROJECT_SOURCE_DIR})

target_compile_features(LegendNetworkTransport PUBLIC cxx_std_20)

target_link_libraries(LegendNetworkTransport
    PUBLIC
        LegendSharedNetwork
        asio_headers
)

if(MSVC)
    target_compile_options(LegendNetworkTransport PRIVATE /utf-8)
endif()
