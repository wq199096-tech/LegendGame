#include "Tests/WorldTestHarness.h"
#include "Server/Common/ServerConfig.h"

#include <cstdio>
#include <filesystem>

namespace worldtest {
void RunFullServerTopologyCheck() {
    legend::server::ServerConfig config; std::string error;
    const auto configPath = std::filesystem::path(LEGEND_SOURCE_DIR) / "Config" / "servers.json";
    const bool loaded=legend::server::LoadServerConfig(configPath.string(),config,error);
    Check("FullServerTopologyCheck: config loads",loaded);
    if(!loaded)return;
    Check("FullServerTopologyCheck: six formal services enabled",config.services.size()>=6&&
        config.services.at("db").enabled&&config.services.at("log").enabled&&
        config.services.at("login").enabled&&config.services.at("character").enabled&&
        config.services.at("world").enabled&&config.services.at("gateway").enabled);
    Check("FullServerTopologyCheck: heartbeat 5/15",config.heartbeatIntervalSeconds==5&&config.heartbeatTimeoutSeconds==15);
    Check("FullServerTopologyCheck: periodic save 60 seconds",config.saveIntervalSeconds==60);
}
}
