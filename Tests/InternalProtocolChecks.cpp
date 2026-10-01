#include "Shared/InternalProtocol/InternalProtocol.h"

#include <cstdio>

int RunInternalProtocolChecks() {
    using namespace legend::internal;
    int failures = 0;
    const auto check = [&](const char* name, bool ok) {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name); if (!ok) ++failures;
    };
    ServiceHandshake handshake{ServiceType::WorldServer, 1, "world-1", "local-secret"};
    std::vector<std::uint8_t> bytes; std::string error; ServiceHandshake decoded;
    check("InternalProtocolChecks: handshake round trip",
          EncodeServiceHandshake(handshake, bytes) &&
          DecodeServiceHandshake(bytes.data(), bytes.size(), decoded, error) &&
          decoded.serviceType == ServiceType::WorldServer && decoded.instanceId == "world-1");
    bytes.push_back(0);
    check("InternalProtocolChecks: strict trailing byte rejection",
          !DecodeServiceHandshake(bytes.data(), bytes.size(), decoded, error));
    DbRequest request{77, DbOperation::LoadInventory, 12, {1,2,3}}; DbRequest decodedRequest;
    check("InternalProtocolChecks: db request id/version/payload",
          EncodeDbRequest(request, bytes) && DecodeDbRequest(bytes.data(), bytes.size(), decodedRequest, error) &&
          decodedRequest.requestId == 77 && decodedRequest.expectedVersion == 12 && decodedRequest.payload.size() == 3);
    LogEvent safe{1,ServiceType::LoginServer,2,LogEventType::LoginSuccess,3,0,"login accepted","{}"};
    LogEvent sensitive=safe; sensitive.extraJson="{\"password\":\"secret\"}";
    check("InternalProtocolChecks: sensitive log field rejected",
          EncodeLogEvent(safe,bytes) && !EncodeLogEvent(sensitive,bytes));
    return failures;
}
