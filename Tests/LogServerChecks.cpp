#include "Server/Log/LogServer.h"
#include "Server/Common/LogClient.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

int RunLogServerChecks(){using namespace legend::internal;int failures=0;const auto check=[&](const char*name,bool ok){std::printf("[%s] %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;};const auto root=std::filesystem::path("testlogs")/"stage255";std::error_code ec;std::filesystem::remove_all(root,ec);legend::net::NetworkService network;network.Start();auto server=std::make_shared<legend::logserver::LogServer>(network);server->GetConfig().listenPort=17560;server->GetConfig().logRoot=root.string();std::string error;check("LogServerChecks: start",server->Start(error));LogEvent event{static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()),ServiceType::WorldServer,2,LogEventType::WorldEnter,1,2,"entered","{\"mapId\":1}"};check("LogServerChecks: enqueue safe event",server->Enqueue(event));event.message="password leaked";check("LogServerChecks: reject sensitive event",!server->Enqueue(event));std::this_thread::sleep_for(std::chrono::milliseconds(1200));server->Stop();network.Stop();bool found=false;if(std::filesystem::exists(root))for(const auto&item:std::filesystem::recursive_directory_iterator(root))if(item.path().extension()==".jsonl")found=true;check("LogServerChecks: JSONL flush",found);std::filesystem::remove_all(root,ec);return failures;}
