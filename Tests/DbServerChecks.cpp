#include "Server/Db/DbServer.h"
#include "Server/Common/PersistenceClient.h"
#include "Server/LoginServer/Account/Database/DatabaseSchema.h"
#include "Shared/InternalProtocol/PersistenceMessages.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

int RunDbServerChecks() {
    int failures=0;const auto check=[&](const char*name,bool ok){std::printf("[%s] %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;};
    const auto dir=std::filesystem::path("testdata")/"stage255_db";std::filesystem::create_directories(dir);
    const auto path=(dir/"db.sqlite").string();std::error_code ec;std::filesystem::remove(path,ec);
    legend::net::NetworkService network;network.Start();auto server=std::make_shared<legend::db::DbServer>(network);
    server->GetConfig().listenPort=17550;server->GetConfig().databasePath=path;std::string error;
    check("DbServerChecks: starts with migration",server->Start(error));
    check("DbServerChecks: healthy after startup",server->Health()==legend::internal::ServiceHealth::Healthy);
    legend::server::PersistenceClient::Config clientConfig;
    clientConfig.port=17550;clientConfig.serviceType=legend::internal::ServiceType::LoginServer;
    clientConfig.instanceId="db-check";clientConfig.requestTimeout=std::chrono::milliseconds(1500);
    auto client=std::make_shared<legend::server::PersistenceClient>(network,clientConfig);client->Start();
    const auto wait=[&](auto predicate,int milliseconds=3000){const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);while(std::chrono::steady_clock::now()<deadline){if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}return predicate();};
    check("DbServerChecks: authenticated persistence RPC link",wait([&]{return client->IsAvailable();}));
    std::vector<std::uint8_t> payload;legend::internal::EncodeAccountCredentials({"rpc_account","ValidPass123!"},payload);
    std::atomic<bool> registerDone{false};legend::internal::DbResponse registerResult;
    client->AsyncRequest(legend::internal::DbOperation::SaveAccount,std::move(payload),[&](auto response){registerResult=std::move(response);registerDone.store(true);});
    check("DbServerChecks: account save RPC",wait([&]{return registerDone.load();})&&registerResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::AccountRegisterResult registered;std::string decode;
    check("DbServerChecks: account save result",legend::internal::DecodeAccountRegisterResult(registerResult.payload.data(),registerResult.payload.size(),registered,decode)&&registered.accountId!=0);
    legend::internal::EncodeAccountCredentials({"rpc_account","ValidPass123!"},payload);
    std::atomic<bool> loginDone{false};legend::internal::DbResponse loginResult;
    client->AsyncRequest(legend::internal::DbOperation::LoadAccount,std::move(payload),[&](auto response){loginResult=std::move(response);loginDone.store(true);});
    check("DbServerChecks: account load/auth RPC",wait([&]{return loginDone.load();})&&loginResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::AccountLoginResult login;decode.clear();
    check("DbServerChecks: Argon2id login returns session",legend::internal::DecodeAccountLoginResult(loginResult.payload.data(),loginResult.payload.size(),login,decode)&&login.accountId==registered.accountId&&!login.sessionToken.empty());
    legend::internal::EncodeValidateSessionRequest({login.sessionToken},payload);
    std::atomic<bool> resumeDone{false};legend::internal::DbResponse resumeResult;
    client->AsyncRequest(legend::internal::DbOperation::ValidateSession,std::move(payload),[&](auto response){resumeResult=std::move(response);resumeDone.store(true);});
    check("DbServerChecks: session validation RPC",wait([&]{return resumeDone.load();})&&resumeResult.errorCode==legend::internal::InternalErrorCode::Ok);
    std::atomic<bool> invalidDone{false};legend::internal::DbResponse invalidResult;
    client->AsyncRequest(legend::internal::DbOperation::LoadInventory,{},[&](auto response){invalidResult=std::move(response);invalidDone.store(true);});
    check("DbServerChecks: unsupported request rejected",wait([&]{return invalidDone.load();})&&invalidResult.errorCode==legend::internal::InternalErrorCode::InvalidRequest);
    client->Stop();
    server->Stop();network.Stop();
    legend::account::Database db;bool schemaOk=db.Open(path,error)&&legend::account::InitializeSchema(db,error);db.Close();
    check("DbServerChecks: legacy-compatible schema reopens",schemaOk);
    std::filesystem::remove_all(dir,ec);return failures;
}
