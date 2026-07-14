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
| common | `00-public-contract-governance.ko.md` | `883e767f3c2b673c9dabc4083fa42a7fc29799d25ef9ad04761d9cbdbc5cb245` |
| common | `01-overview.ko.md` | `729dabd5dfc131095164ec4dec823edb05a75d7ac3ac939ce67c109f9ba66274` |
| common | `02-interaction-model.ko.md` | `de169def6c97bf53083848373f71919adcb899c573934aad718cebf07b8a759c` |
| common | `03-message-model.ko.md` | `c666c483fe9c1210d094330de58cdc144fb700613af8a2275f1708db489b380e` |
| common | `04-async-execution-policy.ko.md` | `af61a35d2bb1752280cb54be247370de36ae3b7285624d92ce14f766f3646367` |
| common | `05-framework-api.ko.md` | `d5bce9aabfe83712d363d5d5c68a3c29706d51276974aa1090e90ac3e0721296` |
| common | `10-channel-topology.ko.md` | `f07b59668bc53814b4b044eb473bd02ee4eba8eeb1ea65ad3c0a964c6dacc85b` |
| common | `11-channel-messaging.ko.md` | `a1d04e973c979530673541a0e22dc5dbddfe776166a61d64747892b6e5d61a14` |
| common | `20-spot-messaging.ko.md` | `ff1a001527ff64ab41ad9db3a715f03a23fd5b08020b65a49707434c9dee62ce` |
| common | `21-spot-node.ko.md` | `5ded44b2ba32545c7aa4f34600360ff1f173291f83b88cb5883da6f3485f9080` |
| common | `22-actor-model.ko.md` | `b96e3fd61f8321d4c0720ebc97b23533d6d184a4da474cb1896f3a7020631c93` |
| common | `23-spot-actor.ko.md` | `0564de5fb730c80fc1e8f804d5f4938f5fa5bc42d65202cbf8bfdbb643117ed5` |
| common | `24-spot-address-messaging.ko.md` | `345bed91840cd0075904f6b30d041136b6c9497e9c5bde1459597f35fdfc6e5b` |
| common | `25-stage-wrapper-on-spot.ko.md` | `c903b3810c41807f58ea23ce5618c5281b6acdebc75c1ef2d2d91e6ae928c32a` |
| common | `30-stream-session.ko.md` | `df63e0e612b2e73140b8e6dbc3d993274f7a057e7b98dcb76d4727951c31e62c` |
| common | `31-session-actor-dispatch.ko.md` | `0cb2249a5f76fcd8b76cb71e7a63af6e344ea3161a8fc2dc02b1cccec52b0be3` |
| common | `32-stream-connector.ko.md` | `23afd4fc65b675f5504659acffab1404c61bbf5266e8822d4639095416505206` |
| common | `40-location-runtime.ko.md` | `647aa07525e6092ceac3f6fbc3508105bacded314bb302fe5879fdb844aabb4a` |
| common | `41-location-store-redis.ko.md` | `1aee1b5d07bb89ea9180961a1e20ba219d8da9cc04800e5a0f3faa8b0e575858` |
| common | `50-runtime-monitoring.ko.md` | `5fbb73843dafd9548a7de2ca44c64d1c6aa4b2205d2ce681bdda6f34447c7215` |
| common | `51-runtime-metrics.ko.md` | `db89a0008f4e3de026665dad46ab0337fcb4583f99d2c2f20f3a97e82f4fb732` |
| common | `52-message-flow-tracing.ko.md` | `1e6df0141f8e63468d58ea9a257b78556ad066031d629183b5ac0d5859007173` |
| common | `53-flow-correlation.ko.md` | `21c3cba6c7c4a20f70d8d5351f434b6cd629115ac779847bf2c32df4418293a0` |
| common | `54-graceful-drain-handoff.ko.md` | `0e229bf89dbfb284ce42c0ecafafe4371337795486b1e9359d35d188da345478` |
| common | `90-implementation-gap.ko.md` | `beb11e0b576aa06a6d8dd48b6c920981cd835da3a4d0392580b5dd704bbae7e2` |
| common | `README.ko.md` | `1768b20f81f5e7cbe0508b210fcc207f1c19008c492895ba0bce0216928e7685` |
| node | `01-system-structure.ko.md` | `0a29f6eb49efd9085d6a0ffd86233f3c31edf2639a0f68a27e7112080086af20` |
| node | `02-handler-interfaces.ko.md` | `be6c788200f59f6721ed379c407fa3f9c2f64fc7b601787537d1add07e6a475f` |
| typescript | `README.ko.md` | `e439dc5e47b17e2f44c6184ce597ba8072da72782312c5814f68fb3be11d27f3` |
| typescript | `03-stream-connector.ko.md` | `8e416b3ed46bea4c5fa3e7496198cf409e3a625aa9a1302816258b2c70699a6f` |

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
