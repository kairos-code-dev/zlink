# Node.js G0 공개 계약 ledger

검토 기준일은 2026-07-13이다. 아래 문서는 Node.js framework와 같은 workspace가 관리하는
TypeScript browser connector 정식 공개 interface의 전체 분모이며,
각 문서의 규범 문장을 public symbol, runtime 동작, contract test와 E2E 항목에 연결해 검토했다.

| ID | 정식 interface 문서 | 검토 결과 |
|----|---------------------|-----------|
| ND-DOC-001 | `01-system-structure.ko.md` | package, module, DI, lifecycle와 public export 검토 완료 |
| ND-DOC-002 | `02-handler-interfaces.ko.md` | 전체 public interface, overload, actor, Spot, stream, location과 monitoring 시그니처 검토 완료 |
| TS-DOC-001 | `languages/typescript/README.ko.md` | browser connector 계약 소유권과 문서 범위 검토 완료 |
| TS-DOC-002 | `languages/typescript/03-stream-connector.ko.md` | browser package root, framing, 명시적 flow와 closing 계약 검토 완료 |

## 정식 spec SHA-256 snapshot

아래 목록은 Node.js G0가 실제로 읽은 공통 spec 26개, Node.js 정식 계약 2개와 TypeScript
browser connector 계약 2개의 전체 분모다.
파일이 추가되거나 내용이 바뀌면 문서 회귀 검증이 실패하며, 변경된 계약을 다시 검토한 뒤 이
snapshot을 함께 갱신해야 한다.

| 범위 | 파일 | SHA-256 |
|------|------|---------|
| . | `00-public-contract-governance.ko.md` | `883e767f3c2b673c9dabc4083fa42a7fc29799d25ef9ad04761d9cbdbc5cb245` |
| . | `01-overview.ko.md` | `136b4b2378c404b4728a4e526f985da6303456c294c06e9e425a39abb99d816b` |
| . | `02-interaction-model.ko.md` | `df441c4de567865658b0b79ded6c840d020ccf60865f58e7990a248e9fa361a0` |
| . | `03-message-model.ko.md` | `a165665cbb47ef2b69744cfa7614d40c35274154af47439693f811080934f914` |
| . | `04-async-execution-policy.ko.md` | `6614f5efd549442f95ac4f67f8ff1e10bba9c7061ee63a7608ffd91f43fea4bd` |
| . | `05-framework-api.ko.md` | `06f3d56438301a80afd983475e58c65d3b0e678a32b832c5f13813bf937ffcb6` |
| server | `10-channel-topology.ko.md` | `5f190e3b4f1b93d4a0e03c9ba23b625a1b8a56c5d79dc361917215be73fa0839` |
| server | `11-channel-messaging.ko.md` | `7a1a32c29bc2cfc642ce465f71e5f405741a2a8c23b95d0426b132947fdd0202` |
| server | `20-spot-messaging.ko.md` | `d9546cf37a3f9f34e863ac4a63eda2e2af6f1985269279579fa5b53632978108` |
| server | `21-spot-node.ko.md` | `d9a36ee80739f7035a0371c703871cea26f952e353d0f123e45b69e44c540088` |
| server | `22-actor-model.ko.md` | `8cf0cac1e46c6086de082d8ad4aeae51f339245d05da1b7bc6175f9b622ec79e` |
| server | `23-spot-actor.ko.md` | `ae0c25c9f67cb397da861e82d8aaf1311472dfe5e28212a88f1e0aa32ec20998` |
| server | `24-spot-address-messaging.ko.md` | `45576c26b8061e0a1965d219d539080a4917c6c06572b5d63bccddfb2f1bbe4d` |
| server | `25-stage-wrapper-on-spot.ko.md` | `54f7a53bc1ff7cc97ada0a41d28f50678e43d68c3ad46e0beef43466dd8ccf5c` |
| server | `30-stream-session.ko.md` | `623bca5e070513cc314c2d7f93d00dcdeab8b5f473bdeb883bfb5711eaa028e0` |
| server | `31-session-actor-dispatch.ko.md` | `49f5154412ed827496ba50f2e49a0f6bc84f3e1bcfdb4022b561dbded9b64147` |
| stream-connector | `32-stream-connector.ko.md` | `fe9072b34809ccc20b489f6a3ebdd093fdd35470d3f1ee291e065f5644cc5f99` |
| server | `40-location-runtime.ko.md` | `dfa08a0db46f59bcd107347c9f02256ff023d7c64c3f8caac42772c37d7b058b` |
| server | `41-location-store-redis.ko.md` | `f84d4a035cd773d6fe8aa0096151909e92be0743b57445dd51e5b38eeab9376c` |
| server | `50-runtime-monitoring.ko.md` | `d30ea2acfbee45009ee2e0d000f2b37009ccf9f5f134c8ad29dd2035e3b8ab99` |
| server | `51-runtime-metrics.ko.md` | `d34e9b26860a2ee285b340c5234bb27fc4c82438bbdf375e697f1350a0c1ef1f` |
| server | `52-message-flow-tracing.ko.md` | `0635851f5d9b3cf0fa6f481fb886200e1802f3bda6fe80db3648b35b53e22108` |
| server | `53-flow-correlation.ko.md` | `077319afac1aec1aba884853cd172443f5e2563d664b00b0a9e2468a252a196c` |
| server | `54-graceful-drain-handoff.ko.md` | `822ada32199d71d2c4505c561fc4f2f4db6f9c50d49eb2469b202d87dd2bc97f` |
| . | `90-implementation-gap.ko.md` | `066b6def76a9dfcc04b818778aab3e65675242c407d5c7d70836b70f4cc94d56` |
| . | `README.ko.md` | `8ffae3ae36f3305e1dfa35d1874a1c2c9c57342f5f2116abbe7f5e432f79f595` |
| server/languages/node | `01-system-structure.ko.md` | `5a9134273d25fb8f8ca7ef503e7a4dc06139852cd0e559564c06f93cda55231a` |
| server/languages/node | `02-handler-interfaces.ko.md` | `705be40bf9f33e803aa775e42dd7a5e9ab7fc8b7df079ef807d04bd3b6179447` |
| stream-connector/languages/typescript | `README.ko.md` | `aa714dbe2a429a5244722a1ad1ba6e409715677f8cd9960f0f1c2b7a7900bfde` |
| stream-connector/languages/typescript | `03-stream-connector.ko.md` | `a5d1a5ea77765d04af31d5e68eed47d64cb4f7bd30813fc13a8b33c1a3912fe0` |

공통 `02-handler-interfaces.ko.md`는 정식 Node.js 언어 interface 2개 분모에서는 제외한다. 다만
공통 spec 26개 분모에는 포함해 G0 hash를 고정했으며, G7 문서 정합성 검토 대상으로도 유지한다.

bindings 기준은 `@zlink-systems/zlink` 9.0.4이며, package의 public `version()`이 보고하는 core
runtime도 9.0.4이다.
G0에서 고정한 실제 artifact 증거는 다음과 같다.

| 항목 | 값 |
|------|----|
| 중앙 pin | `framework/languages/node/package.json`의 `file:../../../.artifacts/wsl/npm/zlink-systems-zlink-9.0.4.tgz` |
| archive 절대경로 | `/home/hep7/project/kairos/zlink/.artifacts/wsl/npm/zlink-systems-zlink-9.0.4.tgz` |
| archive SHA-256 | `d99bbeb743173b9c98f5fac44dee4be4a2722c540399c3a0302bc804db79242a` |
| 설치 package 절대경로 | `/home/hep7/project/kairos/zlink/framework/languages/node/node_modules/@zlink-systems/zlink/package.json` |
| lock integrity | `sha512-31Pu7XBKp4wDjrOrFWhVAX2Tu8ke0jgJPE6sPCanr9LWBZExJP0QfPk6fEz35dh1trYMJApX7HYWsUdDKaUAow==` |
| 공개 API 검증 | `node-binding-parity.test.js`, `backend-public-api-only.test.js`, `verify:abi-matrix` |

package export와 배포 산출물은 `contract-surface.test.js`와
`scripts/verify_packaged_contract.sh`로 검증한다. 중앙 pin이 바뀌면 위 version, hash, 절대경로와
dependency graph를 새 artifact 기준으로 다시 기록하기 전까지 G0를 다시 연다.

## member 형태별 검증 대상

| 검증 ID | 구분 | 실제 Node symbol/표현 | contract test | 결과 |
|---------|------|-----------------------|---------------|------|
| ND-SIG-OVERLOAD-001 | overload | `requestToChannel(...)`, `requestToNode(...)`의 call object와 generic `submit<TReply>()` | `contract-surface.test.js`, `channel-client.test.js` | PASS |
| ND-SIG-NULLABLE-001 | nullable | optional `AbortSignal`, timeout, metadata와 session context의 `undefined` 허용 위치 | `contract-surface.test.js`, cancellation/runtime tests | PASS |
| ND-SIG-GENERIC-001 | generic 제약 | handler payload/reply와 actor/Spot type parameter의 declaration | `contract-surface.test.js`, actor/Spot contract tests | PASS |
| ND-SIG-DEFAULT-001 | default parameter | call timeout이 없을 때 channel, route, framework 기본값 순서 적용 | `channel-client.test.js` | PASS |
| ND-REMOVE-001 | 삭제 symbol | channel call `packetName(...)`, `publishToChannel`, public `yield(...)` | `contract-surface.test.js`, repository symbol search | PASS |
| ND-REMOVE-002 | 삭제 export | registration record/normalize/validate helper와 raw dispatcher root export | `contract-surface.test.js`, packaged consumer | PASS |

## 계약 축별 폐쇄 증거

문서 단위 검토에 더해 정식 member와 동작 축을 다음과 같이 고정했다. 각 행은 declaration,
runtime 동작과 검증을 모두 포함한다.

| 계약 축 | 대표 public member와 동작 | 검증 | 결과 |
|---------|----------------------------|------|------|
| handler와 완료 | request/send/publish/route handler의 `Promise`, one-way `submit(): void`, 장기 작업의 `AbortSignal` | declaration/contract test, runtime matrix | PASS |
| typed packet identity | `ZLinkPacket`, decorator own metadata 또는 생성자 이름, channel/route/Spot/fanout override 금지 | `channel-client.test.js`, RegistrationCodec와 전체 E2E | PASS |
| actor와 membership | actor manager/directory/client, `spotRid`, discriminated join result, bound session | actor contract/integration, Config 9~10 | PASS |
| Spot과 자동 dispatch | `SpotHandle`, lifecycle, handler registry, serial turn과 단일 완료 terminator | Spot contract test, Config 2와 8 | PASS |
| channel/route/fanout | named channel clients, timeout, immutable metadata와 forwarding policy | channel/route contract test, Config 1~5 | PASS |
| stream과 session | typed session handler, actor relay, flow frame, session-closing과 close reason | Node runtime stream contract test, Config 11과 Node↔.NET smoke | PASS |
| browser connector flow 문맥 | 관련 outbound의 `flowFrom(message)` 보존과 표시하지 않은 callback 격리 | `MFLOW-EXT-014`; fake WebSocket PASS, 실제 Chromium gate 진행 | 진행 |
| location과 Redis | 역할별 store, readiness/query/resolver, typed `Draining` row | location/store test, Config 6과 Node↔.NET smoke | PASS |
| codec | serializer selection, codec extension/registrar, compression과 flow marker | codec test, Config 4와 cross-language smoke | PASS |
| monitoring과 metrics | typed runtime events, OpenTelemetry meter와 낮은 cardinality label | monitoring/metrics test, Config 7과 11 | PASS |
| graceful drain | `ZLinkDrainControl`, typed result, NestJS shutdown 순서와 natural drain | drain test, Bingo와 Config 11 | PASS |
| package 경계 | framework/NestJS/connector/codec/location packages와 supporting stream-wire | source export test, ABI matrix, 실제 `.tgz` 7개 consumer | PASS |

공통 spec 26개, Node 정식 계약 문서 2개와 TypeScript 계약 2개는 위 축의 세부 행에 모두 연결했다. 세부 E2E 분모 181개는
`node-g6-e2e-ledger.ko.md`에서 selector와 marker 단위로 관리한다.
