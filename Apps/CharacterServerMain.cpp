#include "Server/Character/CharacterServer.h"
#include "Server/Common/ServerConfig.h"
#include "Engine/Debug/Logger.h"
#include "Shared/Version.h"

#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

int main(int argc,char**argv){std::string configPath="Config/servers.json";for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--config"&&i+1<argc)configPath=argv[++i];else{std::printf("Usage: LegendCharacterServer.exe [--config Config/servers.json]\n");return 1;}}legend::server::ServerConfig topology;std::string error;if(!legend::server::LoadServerConfig(configPath,topology,error)){std::fprintf(stderr,"[Character] config error: %s\n",error.c_str());return 1;}auto character=legend::server::FindService(topology,"character");auto db=legend::server::FindService(topology,"db");auto log=legend::server::FindService(topology,"log");std::filesystem::create_directories("Logs/Character");legend::debug::Logger::Init("Logs/Character");LOG_INFO("[Character] Service Name=LegendCharacterServer Version="+std::string(LEGEND_ENGINE_VERSION)+" Protocol=1 Listen="+character->host+":"+std::to_string(character->port)+" Config="+configPath+" Dependency Status=Degraded(waiting for DbServer)");legend::net::NetworkService network;network.Start();auto server=std::make_shared<legend::character::CharacterServer>(network);server->GetConfig().listenPort=character->port;server->GetConfig().dbHost=db->host;server->GetConfig().dbPort=db->port;server->GetConfig().logHost=log->host;server->GetConfig().logPort=log->port;server->GetConfig().serviceToken=topology.sharedSecret;server->GetConfig().dbTimeout=std::chrono::milliseconds(topology.rpcTimeoutMilliseconds);if(!server->Start(error)){LOG_ERROR("[Character] start failed on port "+std::to_string(character->port)+": "+error);network.Stop();return 1;}asio::io_context signals;asio::signal_set set(signals,SIGINT,SIGTERM);std::atomic<bool>stop{false};set.async_wait([&](const std::error_code&,int){stop.store(true);});while(!stop.load())std::this_thread::sleep_for(std::chrono::milliseconds(100));LOG_INFO("[Character] graceful shutdown");server->Stop();network.Stop();legend::debug::Logger::Shutdown();return 0;}
