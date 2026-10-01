#include "Server/Common/ServerConfig.h"
#include "Server/Gateway/GatewaySession.h"
#include "Shared/Network/MessageId.h"

#include <cstdio>

int RunServerTopologyChecks() {
    int failures=0; const auto check=[&](const char*name,bool ok){std::printf("[%s] %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;};
    auto config=legend::server::MakeDefaultServerConfig();std::string error;
    check("ServerTopologyChecks: default six-service config valid",legend::server::ValidateServerConfig(config,error));
    config.services["log"].port=config.services["db"].port;
    check("ServerTopologyChecks: duplicate port rejected",!legend::server::ValidateServerConfig(config,error));
    legend::gateway::GatewaySession session(nullptr,1);
    session.MarkAuthenticated(10);
    check("GatewayStateChecks: authenticated cannot combat",!session.CanRoute(static_cast<std::uint16_t>(legend::network::MessageId::PlayerAttackRequest)));
    session.MarkCharacterSelected(20);
    check("GatewayStateChecks: selected may enter world",session.CanRoute(static_cast<std::uint16_t>(legend::network::MessageId::EnterWorldRequest)));
    session.MarkInWorld();
    check("GatewayStateChecks: in-world may combat",session.CanRoute(static_cast<std::uint16_t>(legend::network::MessageId::PlayerAttackRequest)));
    return failures;
}
