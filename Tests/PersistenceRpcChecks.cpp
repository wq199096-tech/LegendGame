#include "Shared/InternalProtocol/PersistenceMessages.h"

#include <cstdio>

int RunPersistenceRpcChecks(){using namespace legend::internal;int failures=0;const auto check=[&](const char*name,bool ok){std::printf("[%s] %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;};CharacterCreateCommand command{42,"RpcHero",1,2};std::vector<std::uint8_t>bytes;std::string error;CharacterCreateCommand decoded;check("PersistenceRpcChecks: typed create command",EncodeCharacterCreateCommand(command,bytes)&&DecodeCharacterCreateCommand(bytes.data(),bytes.size(),decoded,error)&&decoded.accountId==42&&decoded.name=="RpcHero");std::vector<legend::account::CharacterSummary> list{{7,"Hero",1,1,3,2,99}};std::vector<legend::account::CharacterSummary> decodedList;check("PersistenceRpcChecks: typed character list",EncodeCharacterList(list,bytes)&&DecodeCharacterList(bytes.data(),bytes.size(),decodedList,error)&&decodedList.size()==1&&decodedList[0].mapId==2);return failures;}
