# Node.js DiscoveryRegistryHa E2E 포팅 인벤토리

기준 문서: `framework/doc/framework/common/e2e/config-6-discovery-registry-ha.ko.md`

## Scenario

| Scenario | .NET 기준 파일 | Node.js 대상 파일 | 상태 | 비고 |
|----------|----------------|-------------------|------|------|
| DR-A1 | `Client/Scenarios/BasicDiscoveryScenario.cs` | `Client/Scenarios/BasicDiscoveryScenario.ts` | done | 단일 registry baseline |
| DR-A2 | `Client/Scenarios/DrA2ClusterBridgeScenario.cs` | `Client/Scenarios/DrA2ClusterBridgeScenario.ts` | done | 2 registry peer 합산과 reg-2 only consumer request |
| DR-A3 | `Client/Scenarios/DrA3ClusterBridgeScenario.cs` | `Client/Scenarios/DrA3ClusterBridgeScenario.ts` | done | 3 registry peer 합산과 registry별 consumer request |
| DR-A4 | `Client/Scenarios/DrA4ThirdRegistryScenario.cs` | `Client/Scenarios/DrA4ThirdRegistryScenario.ts` | done | 같은 rid 다른 endpoint 충돌 광고와 bounded request/evidence |
| DR-B1 | `Client/Scenarios/DrB1FailoverScenario.cs` | `Client/Scenarios/DrB1FailoverScenario.ts` | done | late-start registry 합류와 consumer request |
| DR-B2 | `Client/Scenarios/DrB2FailoverScenario.cs` | `Client/Scenarios/DrB2FailoverScenario.ts`, `Server/Consumer/Configuration/consumer-options.ts`, `Server/Consumer/consumer-host-factory.ts`, `run_e2e.sh`, `feature-map.ko.md` | done | live+stopped endpoint consumer request와 provider evidence를 `logs/20260702-050712-85448`로 검증 |
| DR-B3 | `Client/Scenarios/DrB3RecoveryScenario.cs` | `Client/Scenarios/DrB3RecoveryScenario.ts` | done | peer flapping 후 recovered/survivor registry request |
| DR-C1 | `Client/Scenarios/DrC1EmbeddedRegistryScenario.cs` | `Client/Scenarios/DrC1EmbeddedRegistryScenario.ts` | done | live registry request와 dead registry bounded failure |
| DR-C2 | `Client/Scenarios/DrC2EmbeddedRegistryScenario.cs` | `Client/Scenarios/DrC2EmbeddedRegistryScenario.ts` | done | recovered registry peer 합산과 request |
| DR-C3 | `Client/Scenarios/DrC3EmbeddedRegistryScenario.cs` | `Client/Scenarios/DrC3EmbeddedRegistryScenario.ts`, `Client/Support/managed-process.ts`, `Client/Support/discovery-scenario-support.ts` | done | full registry outage, recovery, api-c re-advertise |
| DR-D1 | `Client/Scenarios/DrD1DirectEndpointScenario.cs` | `Client/Scenarios/DrD1DirectEndpointScenario.ts` | done | embedded registry+provider deployment |
| DR-D2 | `Client/Scenarios/DrD2DirectEndpointScenario.cs` | `Client/Scenarios/DrD2DirectEndpointScenario.ts` | done | standalone registry deployment 대조 |
| DR-D3 | `Client/Scenarios/DrD3DirectEndpointScenario.cs` | `Client/Scenarios/DrD3DirectEndpointScenario.ts` | done | embedded+standalone mixed cluster |
| DR-D4 | `Client/Scenarios/DrD4DirectEndpointScenario.cs` | `Client/Scenarios/DrD4DirectEndpointScenario.ts`, `Server/Probe/` | done | in-process query와 remote topology query 비교 |

## File Mapping

| .NET 기준 파일 | Node.js 대상 파일 | 분류 | 상태 | 비고 |
|----------------|-------------------|------|------|------|
| `.gitignore` | `.gitignore`, `logs/.gitignore` | ignore | done | dist와 logs 산출물 제외 |
| `feature-map.ko.md` | `feature-map.ko.md` | feature-map | done | full sweep 검증 결과 명시 |
| `run_e2e.sh` | `run_e2e.sh`, `feature-map.ko.md` | runner | done | bare 실행 기본값은 `all`이고 full sweep pass proof를 `logs/20260702-065342-62284`와 child log들로 확인 |
| `Shared/DiscoveryRegistryHa.Shared.csproj` | `Shared/messages.ts` | project | done | Node는 별도 shared package 없이 TypeScript shared module로 대응 |
| `Shared/Messages.cs` | `Shared/messages.ts` | shared | done | 현재 구현 scenario의 request/reply/evidence 타입 포함. DR-B2에 별도 payload 타입은 필요하지 않음 |
| `Client/DiscoveryRegistryHa.Client.csproj` | `Client/package.json`, `Client/tsconfig.json` | project | done | client build 설정 |
| `Client/Program.cs` | `Client/main.ts`, `feature-map.ko.md` | client-entry | done | DR-B2 선택과 실행 dispatch를 지원한다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client-options.ts`, `feature-map.ko.md` | support | done | DR-B2 client에 필요한 입력은 기존 옵션으로 충분하다. |
| `Client/Support/DiscoveryApiResult.cs` | `Client/Support/discovery-scenario-support.ts`, `Client/Scenarios/DrD4DirectEndpointScenario.ts` | support | done | DR-D4 topology snapshot 비교용 구조를 support와 scenario에 분리 |
| `Client/Support/ScenarioAssert.cs` | `Client/Support/scenario-assert.ts` | support | done | assertion helper |
| `Client/Scenarios/BasicDiscoveryScenario.cs` | `Client/Scenarios/BasicDiscoveryScenario.ts` | scenario | done | `DR-A1` |
| `Client/Scenarios/DrA2ClusterBridgeScenario.cs` | `Client/Scenarios/DrA2ClusterBridgeScenario.ts` | scenario | done | `DR-A2` |
| `Client/Scenarios/DrA3ClusterBridgeScenario.cs` | `Client/Scenarios/DrA3ClusterBridgeScenario.ts` | scenario | done | `DR-A3` |
| `Client/Scenarios/DrA4ThirdRegistryScenario.cs` | `Client/Scenarios/DrA4ThirdRegistryScenario.ts` | scenario | done | `DR-A4` |
| `Client/Scenarios/DrB1FailoverScenario.cs` | `Client/Scenarios/DrB1FailoverScenario.ts` | scenario | done | `DR-B1` |
| `Client/Scenarios/DrB2FailoverScenario.cs` | `Client/Scenarios/DrB2FailoverScenario.ts` | scenario | done | `DR-B2` |
| `Client/Scenarios/DrB3RecoveryScenario.cs` | `Client/Scenarios/DrB3RecoveryScenario.ts` | scenario | done | `DR-B3` |
| `Client/Scenarios/DrC1EmbeddedRegistryScenario.cs` | `Client/Scenarios/DrC1EmbeddedRegistryScenario.ts` | scenario | done | `DR-C1` |
| `Client/Scenarios/DrC2EmbeddedRegistryScenario.cs` | `Client/Scenarios/DrC2EmbeddedRegistryScenario.ts` | scenario | done | `DR-C2` |
| `Client/Scenarios/DrC3EmbeddedRegistryScenario.cs` | `Client/Scenarios/DrC3EmbeddedRegistryScenario.ts` | scenario | done | `DR-C3` |
| `Client/Scenarios/DrD1DirectEndpointScenario.cs` | `Client/Scenarios/DrD1DirectEndpointScenario.ts` | scenario | done | `DR-D1` |
| `Client/Scenarios/DrD2DirectEndpointScenario.cs` | `Client/Scenarios/DrD2DirectEndpointScenario.ts` | scenario | done | `DR-D2` |
| `Client/Scenarios/DrD3DirectEndpointScenario.cs` | `Client/Scenarios/DrD3DirectEndpointScenario.ts` | scenario | done | `DR-D3` |
| `Client/Scenarios/DrD4DirectEndpointScenario.cs` | `Client/Scenarios/DrD4DirectEndpointScenario.ts` | scenario | done | `DR-D4` |
| `Server/Consumer/DiscoveryRegistryHa.Consumer.csproj` | `Server/Consumer/package.json`, `Server/Consumer/tsconfig.json` | project | done | consumer role build 설정 |
| `Server/Consumer/Program.cs` | `Server/Consumer/main.ts` | server-entry | done | consumer 실행 진입점 |
| `Server/Consumer/ConsumerHostFactory.cs` | `Server/Consumer/consumer-host-factory.ts`, `feature-map.ko.md` | server-role | done | repeated registry endpoint를 discovery client 구성에 반영한다. |
| `Server/Consumer/ConsumerOptions.cs` | `Server/Consumer/Configuration/consumer-options.ts`, `feature-map.ko.md` | configuration | done | repeated registry endpoint 입력을 받는다. |
| `Server/Provider/DiscoveryRegistryHa.Provider.csproj` | `Server/Provider/package.json`, `Server/Provider/tsconfig.json` | project | done | provider role build 설정 |
| `Server/Provider/Program.cs` | `Server/Provider/main.ts` | server-entry | done | provider 실행 진입점 |
| `Server/Provider/ProviderHandlers.cs` | `Server/Provider/Handlers/profile-request-handler.ts` | handlers | done | profile request handler 구현 |
| `Server/Provider/ProviderHostFactory.cs` | `Server/Provider/provider-host-factory.ts` | server-role | done | registry discovery와 channel server 구성 |
| `Server/Provider/Support/ProviderEvidenceStore.cs` | `Server/Provider/Infrastructure/evidence-store.ts` | infrastructure | done | provider evidence와 bounded wait |
| `Server/Provider/Support/ProviderOptions.cs` | `Server/Provider/Configuration/provider-options.ts` | configuration | done | repeated registry endpoint, channel endpoint, evidence 입력 포함 |
| `Server/Registry/DiscoveryRegistryHa.Registry.csproj` | `Server/Registry/package.json`, `Server/Registry/tsconfig.json` | project | done | registry role build 설정 |
| `Server/Registry/Program.cs` | `Server/Registry/main.ts` | server-entry | done | registry 실행 진입점 |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/registry-host-factory.ts` | server-role | done | standalone registry와 public query endpoint 구성 |
| `Server/Registry/RegistryOptions.cs` | `Server/Registry/Configuration/registry-options.ts` | configuration | done | registry id, endpoints, peers 입력 |
| `Server/Embedded/*` | `Server/Embedded/` | embedded-role | done | DR-D1 embedded deployment와 DR-D3 mixed cluster 지원 |
| `Server/Probe/*` | `Server/Probe/` | probe-role | done | DR-D4 remote query client topology endpoint 지원 |
