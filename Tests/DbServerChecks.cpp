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
    // ---- 阶段25.5：World 持久化 RPC（角色/位置/成长/背包/装备/任务/商店） ----
    const auto rpc=[&](legend::internal::DbOperation op,std::vector<std::uint8_t> bytes,
                       legend::internal::DbResponse& out,std::atomic<bool>& done){
        client->AsyncRequest(op,std::move(bytes),[&](auto response){out=std::move(response);done.store(true);});
        wait([&]{return done.load();});
    };
    legend::internal::EncodeCharacterCreateCommand({registered.accountId,"RpcWorldHero",1,1},payload);
    std::atomic<bool> charDone{false};legend::internal::DbResponse charResult;
    rpc(legend::internal::DbOperation::CreateCharacter,std::move(payload),charResult,charDone);
    legend::account::CharacterSummary character;decode.clear();
    check("DbServerChecks: character create RPC",charResult.errorCode==legend::internal::InternalErrorCode::Ok&&legend::internal::DecodeCharacterSummary(charResult.payload.data(),charResult.payload.size(),character,decode)&&character.characterId!=0);
    const std::uint64_t cid=character.characterId;
    legend::internal::EncodeCharacterIdQuery({cid},payload);
    std::atomic<bool> rowDone{false};legend::internal::DbResponse rowResult;
    rpc(legend::internal::DbOperation::LoadCharacterFull,std::move(payload),rowResult,rowDone);
    legend::internal::WorldCharacterRow fullRow;
    check("DbServerChecks: full character row RPC",rowResult.errorCode==legend::internal::InternalErrorCode::Ok&&legend::internal::DecodeWorldCharacterRow(rowResult.payload.data(),rowResult.payload.size(),fullRow,decode)&&fullRow.id==cid&&fullRow.accountId==registered.accountId&&!fullRow.deleted);
    legend::internal::EncodeWorldSavePosition({cid,1,123.5f,456.25f,legend::account::UnixNow()},payload);
    std::atomic<bool> posDone{false};legend::internal::DbResponse posResult;
    rpc(legend::internal::DbOperation::SavePosition,std::move(payload),posResult,posDone);
    check("DbServerChecks: position save RPC",posResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::EncodeWorldSaveProgression({cid,4,320,88},payload);
    std::atomic<bool> progDone{false};legend::internal::DbResponse progResult;
    rpc(legend::internal::DbOperation::SaveProgression,std::move(payload),progResult,progDone);
    check("DbServerChecks: progression save RPC",progResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::EncodeWorldSaveGold({cid,77},payload);
    std::atomic<bool> goldDone{false};legend::internal::DbResponse goldResult;
    rpc(legend::internal::DbOperation::SaveGoldWrite,std::move(payload),goldResult,goldDone);
    check("DbServerChecks: gold save RPC",goldResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::EncodeWorldItemInsert({cid,3001,3,0,legend::account::UnixNow(),false,0,0},payload);
    std::atomic<bool> itemDone{false};legend::internal::DbResponse itemResult;
    rpc(legend::internal::DbOperation::ItemInsertWrite,std::move(payload),itemResult,itemDone);
    legend::internal::WorldItemInsertResult itemInsert;
    check("DbServerChecks: item insert RPC",itemResult.errorCode==legend::internal::InternalErrorCode::Ok&&legend::internal::DecodeWorldItemInsertResult(itemResult.payload.data(),itemResult.payload.size(),itemInsert,decode)&&itemInsert.newInstanceId!=0);
    legend::internal::EncodeWorldItemInsert({cid,3001,5,0,0,true,itemInsert.newInstanceId,8},payload);
    std::atomic<bool> mergeDone{false};legend::internal::DbResponse mergeResult;
    rpc(legend::internal::DbOperation::ItemInsertWrite,std::move(payload),mergeResult,mergeDone);
    check("DbServerChecks: item merge RPC",mergeResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::EncodeWorldEquipItem({cid,itemInsert.newInstanceId,1001,true,0,1},payload);
    std::atomic<bool> equipDone{false};legend::internal::DbResponse equipResult;
    rpc(legend::internal::DbOperation::EquipItemWrite,std::move(payload),equipResult,equipDone);
    check("DbServerChecks: equip transaction RPC",equipResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::EncodeCharacterIdQuery({cid},payload);
    std::atomic<bool> invDone{false};legend::internal::DbResponse invResult;
    rpc(legend::internal::DbOperation::LoadInventory,std::move(payload),invResult,invDone);
    legend::internal::WorldInventoryList inventory;
    check("DbServerChecks: inventory load reflects equip",invResult.errorCode==legend::internal::InternalErrorCode::Ok&&legend::internal::DecodeWorldInventoryList(invResult.payload.data(),invResult.payload.size(),inventory,decode)&&inventory.items.size()==1&&inventory.items[0].slotIndex==1001&&inventory.items[0].quantity==8);
    legend::internal::EncodeWorldUnequipItem({itemInsert.newInstanceId,1001,5},payload);
    std::atomic<bool> unequipDone{false};legend::internal::DbResponse unequipResult;
    rpc(legend::internal::DbOperation::UnequipItemWrite,std::move(payload),unequipResult,unequipDone);
    check("DbServerChecks: unequip transaction RPC",unequipResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::EncodeWorldQuestInsert({cid,4001,1,legend::account::UnixNow(),{1,2}},payload);
    std::atomic<bool> questDone{false};legend::internal::DbResponse questResult;
    rpc(legend::internal::DbOperation::QuestInsertWrite,std::move(payload),questResult,questDone);
    check("DbServerChecks: quest insert RPC",questResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::EncodeWorldQuestObjective({cid,4001,1,3},payload);
    std::atomic<bool> objDone{false};legend::internal::DbResponse objResult;
    rpc(legend::internal::DbOperation::QuestObjectiveWrite,std::move(payload),objResult,objDone);
    check("DbServerChecks: quest objective RPC",objResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::EncodeWorldOfflineKill({cid,{{4001,2,5}}},payload);
    std::atomic<bool> killDone{false};legend::internal::DbResponse killResult;
    rpc(legend::internal::DbOperation::OfflineKillWrite,std::move(payload),killResult,killDone);
    check("DbServerChecks: offline kill advance RPC",killResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::EncodeWorldQuestState({cid,4001,2,legend::account::UnixNow(),false},payload);
    std::atomic<bool> stateDone{false};legend::internal::DbResponse stateResult;
    rpc(legend::internal::DbOperation::QuestStateWrite,std::move(payload),stateResult,stateDone);
    check("DbServerChecks: quest state RPC",stateResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::EncodeCharacterIdQuery({cid},payload);
    std::atomic<bool> questLoadDone{false};legend::internal::DbResponse questLoadResult;
    rpc(legend::internal::DbOperation::LoadQuestState,std::move(payload),questLoadResult,questLoadDone);
    legend::internal::WorldQuestStateList questState;
    check("DbServerChecks: quest load roundtrip",questLoadResult.errorCode==legend::internal::InternalErrorCode::Ok&&legend::internal::DecodeWorldQuestStateList(questLoadResult.payload.data(),questLoadResult.payload.size(),questState,decode)&&questState.quests.size()==1&&questState.quests[0].questId==4001&&questState.quests[0].state==2&&questState.objectives.size()==2&&questState.objectives[0].progress==3&&questState.objectives[1].progress==1);
    legend::internal::EncodeWorldShopBuy({cid,50,3002,1,7,false,0,0},payload);
    std::atomic<bool> buyDone{false};legend::internal::DbResponse buyResult;
    rpc(legend::internal::DbOperation::ShopBuyWrite,std::move(payload),buyResult,buyDone);
    legend::internal::WorldShopBuyResult buy;
    check("DbServerChecks: shop buy transaction RPC",buyResult.errorCode==legend::internal::InternalErrorCode::Ok&&legend::internal::DecodeWorldShopBuyResult(buyResult.payload.data(),buyResult.payload.size(),buy,decode)&&buy.newInstanceId!=0);
    legend::internal::EncodeWorldShopSell({cid,100,buy.newInstanceId,1,true},payload);
    std::atomic<bool> sellDone{false};legend::internal::DbResponse sellResult;
    rpc(legend::internal::DbOperation::ShopSellWrite,std::move(payload),sellResult,sellDone);
    check("DbServerChecks: shop sell transaction RPC",sellResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::EncodeWorldAddRewards({cid,10,20},payload);
    std::atomic<bool> rewardDone{false};legend::internal::DbResponse rewardResult;
    rpc(legend::internal::DbOperation::AddRewardsWrite,std::move(payload),rewardResult,rewardDone);
    check("DbServerChecks: offline reward RPC",rewardResult.errorCode==legend::internal::InternalErrorCode::Ok);
    legend::internal::EncodeWorldQuestTurnIn({cid,4001,legend::account::UnixNow(),2,10,90,0,0,-1,0},payload);
    std::atomic<bool> turnInDone{false};legend::internal::DbResponse turnInResult;
    rpc(legend::internal::DbOperation::QuestTurnInWrite,std::move(payload),turnInResult,turnInDone);
    legend::internal::WorldQuestTurnInResult turnIn;
    check("DbServerChecks: quest turn-in transaction RPC",turnInResult.errorCode==legend::internal::InternalErrorCode::Ok&&legend::internal::DecodeWorldQuestTurnInResult(turnInResult.payload.data(),turnInResult.payload.size(),turnIn,decode)&&turnIn.ok);
    legend::internal::EncodeWorldQuestAbandon({cid,4001,legend::account::UnixNow()},payload);
    std::atomic<bool> abandonDone{false};legend::internal::DbResponse abandonResult;
    rpc(legend::internal::DbOperation::QuestAbandonWrite,std::move(payload),abandonResult,abandonDone);
    check("DbServerChecks: quest abandon RPC",abandonResult.errorCode==legend::internal::InternalErrorCode::Ok);
    std::atomic<bool> invalidDone{false};legend::internal::DbResponse invalidResult;
    client->AsyncRequest(legend::internal::DbOperation::LoadInventory,{},[&](auto response){invalidResult=std::move(response);invalidDone.store(true);});
    check("DbServerChecks: malformed request rejected",wait([&]{return invalidDone.load();})&&invalidResult.errorCode==legend::internal::InternalErrorCode::InvalidRequest);
    client->Stop();
    server->Stop();network.Stop();
    legend::account::Database db;bool schemaOk=db.Open(path,error)&&legend::account::InitializeSchema(db,error);db.Close();
    check("DbServerChecks: legacy-compatible schema reopens",schemaOk);
    std::filesystem::remove_all(dir,ec);return failures;
}
