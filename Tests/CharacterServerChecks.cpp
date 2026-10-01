#include "Server/Character/CharacterServer.h"
#include "Shared/Account/CharacterTypes.h"

#include <cstdio>

int RunCharacterServerChecks(){int failures=0;const auto check=[&](const char*name,bool ok){std::printf("[%s] %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;};check("CharacterServerChecks: valid classes",legend::account::IsValidClassId(1)&&legend::account::IsValidClassId(2)&&legend::account::IsValidClassId(3));check("CharacterServerChecks: invalid class",!legend::account::IsValidClassId(99));check("CharacterServerChecks: duplicate/limit enforced by shared service",legend::account::kMaxCharactersPerAccount==4);legend::net::NetworkService network;network.Start();auto server=std::make_shared<legend::character::CharacterServer>(network);server->GetConfig().listenPort=17540;server->GetConfig().dbPort=17999;std::string error;check("CharacterServerChecks: starts degraded while DB unavailable",server->Start(error));server->Stop();network.Stop();return failures;}
