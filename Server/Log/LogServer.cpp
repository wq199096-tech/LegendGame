#include "Server/Log/LogServer.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Network/MessageId.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace legend::logserver {

using namespace legend::internal;
using legend::network::MessageId;
using legend::network::Packet;

LogServer::LogServer(legend::net::NetworkService& service)
    : m_service(service), m_server(std::make_shared<legend::net::TcpServer>(service)) {}
LogServer::~LogServer() { Stop(); }

bool LogServer::Start(std::string& error) {
    if (!m_stopped.exchange(false)) return true;
    std::error_code ec; std::filesystem::create_directories(m_config.logRoot, ec);
    if (ec) { error = "cannot create log root: " + ec.message(); m_stopped.store(true); return false; }
    if (!m_server->Listen(m_config.listenPort, error)) { m_stopped.store(true); return false; }
    m_writer = std::thread([this] { WriterLoop(); });
    m_server->StartAccepting([self=shared_from_this()](legend::net::TcpConnectionPtr connection){self->OnAccepted(std::move(connection));});
    return true;
}

void LogServer::Stop() {
    if (m_stopped.exchange(true)) return;
    m_server->Stop();
    { std::lock_guard<std::mutex> lock(m_linksMutex); m_links.clear(); }
    m_queueCv.notify_all();
    if (m_writer.joinable()) m_writer.join();
}

std::size_t LogServer::QueuedCount() const { std::lock_guard<std::mutex> lock(m_queueMutex); return m_queue.size(); }

bool LogServer::Enqueue(LogEvent event) {
    if (ContainsSensitiveLogField(event.message,event.extraJson) || !IsValidServiceType(event.service) || !IsValidLogEventType(event.eventType)) return false;
    { std::lock_guard<std::mutex> lock(m_queueMutex); if(m_queue.size()>=m_config.maxQueueSize){m_dropped.fetch_add(1);return false;} m_queue.push_back(std::move(event)); }
    m_queueCv.notify_one(); return true;
}

void LogServer::OnAccepted(legend::net::TcpConnectionPtr connection){const auto id=connection->Id();{std::lock_guard<std::mutex>lock(m_linksMutex);m_links[id]={connection,false};}auto self=shared_from_this();connection->Start([self,id](const Packet&p){self->OnPacket(id,p);},[self,id](std::uint64_t,const std::error_code&){self->OnClosed(id);});}
void LogServer::OnClosed(std::uint64_t id){std::lock_guard<std::mutex>lock(m_linksMutex);m_links.erase(id);}

void LogServer::OnPacket(std::uint64_t id,const Packet&packet){bool authenticated=false;{std::lock_guard<std::mutex>lock(m_linksMutex);auto it=m_links.find(id);if(it==m_links.end())return;authenticated=it->second.authenticated;}const auto message=static_cast<MessageId>(packet.header.messageId);if(!authenticated){if(message!=MessageId::InternalServiceHandshake){OnClosed(id);return;}ServiceHandshake hello;std::string error;const bool accepted=DecodeServiceHandshake(packet.payload.data(),packet.payload.size(),hello,error)&&hello.serviceType!=ServiceType::LogServer&&(m_config.serviceToken.empty()||hello.serviceToken==m_config.serviceToken);ServiceHandshakeAck ack{accepted,accepted?InternalErrorCode::Ok:InternalErrorCode::ProtocolMismatch,accepted?"ready":"service rejected"};Packet response;response.header.messageId=static_cast<std::uint16_t>(MessageId::InternalServiceHandshakeAck);EncodeServiceHandshakeAck(ack,response.payload);Send(id,response);if(accepted){std::lock_guard<std::mutex>lock(m_linksMutex);auto it=m_links.find(id);if(it!=m_links.end())it->second.authenticated=true;}return;}if(message==MessageId::InternalHeartbeat)return;if(message!=MessageId::InternalLogEvent){OnClosed(id);return;}LogEvent event;std::string error;if(!DecodeLogEvent(packet.payload.data(),packet.payload.size(),event,error)){LOG_WARN("[Log] malformed/sensitive event rejected");return;}if(Enqueue(std::move(event))){Packet ack;ack.header.messageId=static_cast<std::uint16_t>(MessageId::InternalLogAck);Send(id,ack);}}
void LogServer::Send(std::uint64_t id,const Packet&packet){legend::net::TcpConnectionPtr connection;{std::lock_guard<std::mutex>lock(m_linksMutex);auto it=m_links.find(id);if(it!=m_links.end())connection=it->second.connection;}if(connection)connection->Send(packet);}

void LogServer::WriterLoop(){while(true){std::deque<LogEvent>batch;{std::unique_lock<std::mutex>lock(m_queueMutex);m_queueCv.wait_for(lock,std::chrono::seconds(1),[this]{return m_stopped.load()||!m_queue.empty();});const auto count=std::min<std::size_t>(m_queue.size(),256);for(std::size_t i=0;i<count;++i){batch.push_back(std::move(m_queue.front()));m_queue.pop_front();}if(batch.empty()&&m_stopped.load())return;}if(!WriteBatch(batch)){m_dropped.fetch_add(batch.size());LOG_ERROR("[Log] batch write failed");}if(m_stopped.load()){std::lock_guard<std::mutex>lock(m_queueMutex);if(m_queue.empty())return;}}}

bool LogServer::WriteBatch(std::deque<LogEvent>& batch){for(const auto&event:batch){const auto point=std::chrono::system_clock::time_point(std::chrono::milliseconds(event.timestampMs));const std::time_t time=std::chrono::system_clock::to_time_t(point);std::tm tm{};
#ifdef _WIN32
localtime_s(&tm,&time);
#else
localtime_r(&time,&tm);
#endif
std::ostringstream date;date<<std::put_time(&tm,"%Y-%m-%d");const auto dir=std::filesystem::path(m_config.logRoot)/date.str();std::error_code ec;std::filesystem::create_directories(dir,ec);if(ec)return false;const auto file=dir/(std::string(ServiceTypeName(event.service))+".jsonl");std::ofstream output(file,std::ios::binary|std::ios::app);if(!output)return false;nlohmann::json json={{"timestamp",event.timestampMs},{"service",ServiceTypeName(event.service)},{"level",event.level},{"eventType",LogEventTypeName(event.eventType)},{"accountId",event.accountId},{"characterId",event.characterId},{"message",event.message}};if(!event.extraJson.empty()){try{json["extra"]=nlohmann::json::parse(event.extraJson);}catch(...){return false;}}output<<json.dump()<<'\n';output.flush();if(!output)return false;}return true;}

} // namespace legend::logserver
