# Node.js G0 공개 계약 ledger

검토 기준일은 2026-07-13이다. 아래 문서는 Node.js framework와 같은 workspace가 관리하는
TypeScript browser connector 정식 공개 interface의 전체 분모이며,
각 문서의 규범 문장을 public symbol, runtime 동작, contract test와 E2E 항목에 연결해 검토했다.

| ID | 정식 interface 문서 | 검토 결과 |
|----|---------------------|-----------|
| ND-DOC-001 | `01-system-structure.ko.md` | package, module, DI, lifecycle와 public export 검토 완료 |
| ND-DOC-002 | `02-handler-interfaces.ko.md` | 전체 public interface, overload, actor, Spot, stream, location과 monitoring 시그니처 검토 완료 |
| ND-DOC-003 | `03-routing-id-allocation.ko.md` | builder, slot store, readiness provider와 fencing lifecycle 검토 완료 |
| TS-DOC-001 | `languages/typescript/README.ko.md` | browser connector 계약 소유권과 문서 범위 검토 완료 |
| TS-DOC-002 | `languages/typescript/03-stream-connector.ko.md` | browser package root, framing, 명시적 flow와 closing 계약 검토 완료 |

## 정식 spec SHA-256 snapshot

아래 목록은 Node.js G0가 실제로 읽은 공통 spec 26개, Node.js 정식 계약 3개와 TypeScript
browser connector 계약 2개의 전체 분모다.
파일이 추가되거나 내용이 바뀌면 문서 회귀 검증이 실패하며, 변경된 계약을 다시 검토한 뒤 이
snapshot을 함께 갱신해야 한다.

| 범위 | 파일 | SHA-256 |
|------|------|---------|
| . | `00-public-contract-governance.ko.md` | `3538efc2f99956660089ef35470501418c811bf79dbd12537f9d6053a6d6f194` |
| . | `01-overview.ko.md` | `01ec9efd421441dcb5e90d547a5d2841c071aa5c85c71b3c3b0723a465b083eb` |
| . | `02-interaction-model.ko.md` | `3b8a593352da512749f4bbf2a3be915fec0a9027955a25c20ecdf71f5909b74e` |
| . | `03-message-model.ko.md` | `5bec94bb16aaed48e5346c29b9f41af8c85b6d6058d402aae7c13ca662965182` |
| . | `04-async-execution-policy.ko.md` | `53f2daabe02d16576577c5c80cfc1b3a1a911bd4df7635d12a317c4f6849f750` |
| . | `05-framework-api.ko.md` | `145c42fdd5ed3fb0d07afc8923c1ea0b61857dbbad2c9efbcb7f56921cd57b16` |
| server | `10-channel-topology.ko.md` | `fb9042b2da236685a8d333a5f6b5cc86555a4f1068a6b134fa3c32dadc288fdd` |
| server | `11-channel-messaging.ko.md` | `39a003be40b2c513985cfbe357079dc935f44040ff9bb5b0d5e964703e22143c` |
| server | `20-spot-messaging.ko.md` | `01a76b96ec7e4a77413e7ca4376d372e302b755e73fd7e1e408a8e4990622678` |
| server | `21-mesh-node.ko.md` | `2f48cb256c74e8c1de47d899237c472db615ddd177473f91c27aa4c28d2a7bc2` |
| server | `22-actor-model.ko.md` | `e85257bf576d4e59efa75807a2b8b4a730a9a119efa1216e38e1c08d9553c798` |
| server | `23-spot-actor.ko.md` | `9783fa31ddce92cc82c36cda6947a5e3702812a6b0bfe10f0c3e7b43b29c7c86` |
| server | `24-spot-address-messaging.ko.md` | `bc9314ecbb5a02bbbfea3355195aec878143989e2d0c204e1b0a73a755f74b8e` |
| server | `25-stage-wrapper-on-spot.ko.md` | `c227e228b91f5887d94539be2c9954b33d1ee411d8d7e6f2bd5be9427f02dd53` |
| server | `30-stream-session.ko.md` | `5f516c76cf255f20693c04ec76199261623daba73aa8e0c010f42ae9fbec24f5` |
| server | `31-session-actor-dispatch.ko.md` | `90b10fbe258905a1e8483689aca6499e8eb2141ba434f02547df6d134a24321e` |
| stream-connector | `32-stream-connector.ko.md` | `a33b6c5683b6af15025c9e74ee73719d8ddea68fbefd2c6455471482a7c523e9` |
| server | `40-location-runtime.ko.md` | `e62cd34ad7fc3ba3ed48c604b086a9bda42433d61d2fcf8742ce49b584032c92` |
| server | `41-location-store-redis.ko.md` | `100f2ae2bb9cac87a0be80b9551a4c3934a42bec4ebdda260c345a57fe8b30a7` |
| server | `50-runtime-monitoring.ko.md` | `f0291df0c535ea1e334c0eba46bb78508dbefab0ad02565a13ba436673ab11ae` |
| server | `51-runtime-metrics.ko.md` | `003489d9bc188efa7532a6e0e50c0154c31a6146eed86c7dcd2534121ccc2313` |
| server | `52-message-flow-tracing.ko.md` | `76b9547c7ca67c81e9577e135f783680b416263fafd2391c10b6472fbaf92177` |
| server | `53-flow-correlation.ko.md` | `89b668fcd8ccc710323cd3a3548d8f19626f7b288b790fc4dc01987f66e4c0c4` |
| server | `54-graceful-drain-handoff.ko.md` | `bf1d47d7e9b80a675b8f8e21014116f335d38ea40cbe901b9cc236cbf094b426` |
| . | `90-implementation-gap.ko.md` | `94d8f4dd5a1352648ed6a90039d1afffbd12869382a3ea0bcb94bea4372f804c` |
| . | `README.ko.md` | `40bbce9a6a54e06bc8592e97177923909c27187e0cf52bc1bc4cf120e2d9a9a3` |
| server/languages/node | `01-system-structure.ko.md` | `2ea619ddf4388708439917a7b30768ab9f926b0bf61b00abbd239937bb5f50a6` |
| server/languages/node | `02-handler-interfaces.ko.md` | `b60ff122098454d12ad2db7876db1b2ddf67cc09fa4ce41a16d6cb6a283e7315` |
| server/languages/node | `03-routing-id-allocation.ko.md` | `05bc00540846219e043fe265c38d5264a913df2e96198116fdc22ef199f643e9` |
| stream-connector/languages/typescript | `README.ko.md` | `aa714dbe2a429a5244722a1ad1ba6e409715677f8cd9960f0f1c2b7a7900bfde` |
| stream-connector/languages/typescript | `03-stream-connector.ko.md` | `1590131d1296193a23432634c59e9579d594e5833991bfaa51b8b3cf0e2413bd` |

공통 `02-handler-interfaces.ko.md`는 정식 Node.js 언어 interface 2개 분모에서는 제외한다. 다만
공통 spec 26개 분모에는 포함해 G0 hash를 고정했으며, G7 문서 정합성 검토 대상으로도 유지한다.

bindings 기준은 `@zlink-systems/zlink` 10.6.0이며, package의 public `version()`이 보고하는 core
runtime도 10.6.0이다.
G0에서 고정한 실제 artifact 증거는 다음과 같다.

| 항목 | 값 |
|------|----|
| 중앙 pin | `framework/languages/node/package.json`의 `file:../../../.artifacts/wsl/npm/zlink-systems-zlink-10.6.0.tgz` |
| archive 절대경로 | `/home/hep7/project/kairos/zlink/.artifacts/wsl/npm/zlink-systems-zlink-10.6.0.tgz` |
| archive SHA-256 | `f51421b7188ef8146b3316f6058f0903195b5768a0c039528bb512fff123835a` |
| 설치 package 절대경로 | `/home/hep7/project/kairos/zlink/framework/languages/node/node_modules/@zlink-systems/zlink/package.json` |
| lock integrity | `sha512-qS7dJyuHAwBVAVlTMoL/bwouvOkaMFDEU5s5XaV9Y/NP345Kejgg/kXRstdL9eoGSwvWvRROkY1eytfAxtlZUA==` |
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

공통 spec 26개, Node 정식 계약 문서 3개와 TypeScript 계약 2개는 위 축의 세부 행에 모두 연결했다. 세부 E2E 분모 181개는
`node-g6-e2e-ledger.ko.md`에서 selector와 marker 단위로 관리한다.
