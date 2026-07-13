# Node.js G0 공개 계약 ledger

검토 기준일은 2026-07-13이다. 아래 문서는 Node.js 정식 공개 interface의 전체 분모이며,
각 문서의 규범 문장을 public symbol, runtime 동작, contract test와 E2E 항목에 연결해 검토했다.

| ID | 정식 interface 문서 | 검토 결과 |
|----|---------------------|-----------|
| ND-DOC-001 | `01-system-structure.ko.md` | package, module, DI, lifecycle와 public export 검토 완료 |
| ND-DOC-002 | `02-handler-interfaces.ko.md` | 전체 public interface, overload, actor, Spot, stream, location과 monitoring 시그니처 검토 완료 |
| ND-DOC-003 | `03-stream-connector.ko.md` | connector framing, flow, closing 계약 검토 완료 |

## 정식 spec SHA-256 snapshot

아래 목록은 Node.js G0가 실제로 읽은 공통 spec 26개와 Node.js 정식 계약 3개의 전체 분모다.
파일이 추가되거나 내용이 바뀌면 문서 회귀 검증이 실패하며, 변경된 계약을 다시 검토한 뒤 이
snapshot을 함께 갱신해야 한다.

| 범위 | 파일 | SHA-256 |
|------|------|---------|
| common | `00-public-contract-governance.ko.md` | `67c50c2750243eaaaad34dd579d45707dd37eacca9c31d3c074601ce3692bd88` |
| common | `01-overview.ko.md` | `729dabd5dfc131095164ec4dec823edb05a75d7ac3ac939ce67c109f9ba66274` |
| common | `02-interaction-model.ko.md` | `bbc0dbf64aadd7b03e6b3952b2cfc283d69ce271968926c8fda1ce5a5d491492` |
| common | `03-message-model.ko.md` | `2a6cbc45740487b9fab970699b94cac29f774d397407d314747b5b94479b010c` |
| common | `04-async-execution-policy.ko.md` | `9c8021112e2f4b1566679afeefaee8013b7dd643d12817260a063ec93cefb804` |
| common | `05-framework-api.ko.md` | `49e0d306d42b1ab9909c9d7a29da44c25a57198168c23936070de6e5fd73cb93` |
| common | `10-channel-topology.ko.md` | `a44902a9f8775cfabe50b946fe1cab7f1a56083ad4a210c13a7d941eaca31c41` |
| common | `11-channel-messaging.ko.md` | `7d372f6a15a0e45bf8eb9c26d891d02dca3bcb78475c0ab1925d34abbaefb91d` |
| common | `20-spot-messaging.ko.md` | `34f0253beeafe0607da40b100ad78db40c0b018f3a18d3a60cffa66b31f80de4` |
| common | `21-spot-node.ko.md` | `03f4e3e114799a62cdd64cc230e11d5e8c024109747714db7306c000404552aa` |
| common | `22-actor-model.ko.md` | `6b68a42a1ebab714fc7b1cd775e45f4ab4adfdaf536d181fd44fccaa9f50e1fc` |
| common | `23-spot-actor.ko.md` | `a4e8e2231abb2ecbb70e3c1938bb5b9bf39233981e329a25f8dc5d8befa407b2` |
| common | `24-spot-address-messaging.ko.md` | `c4cb5fc6f41aea5877062956657668abfe7d228f0dca7b9e785eb0054e3e2353` |
| common | `25-stage-wrapper-on-spot.ko.md` | `d837409648b996bf010ac1c9509f24d3679d58948006c121eff61963ddc01c3c` |
| common | `30-stream-session.ko.md` | `fde5faaec066875870711a8f52d5fbfb543f80ddc102b83917eae3b6fc47b0fd` |
| common | `31-session-actor-dispatch.ko.md` | `4dac9b99fd195db97cf3f9bc375749311b7bbfac69e31749fbb203ad79a2ea8a` |
| common | `32-stream-connector.ko.md` | `01b72e31d4f661c60cf70110e53d2d158b68d5ce18c270f4fdc033adc083da05` |
| common | `40-location-runtime.ko.md` | `e5f0140d6f37cb592be91d989005983192705970efcf9f5e2defbd75083416a6` |
| common | `41-location-store-redis.ko.md` | `253e1a9fdd6ab9041a4158f09c64b6a36e4a55d5d019b3627397c4870ff1f210` |
| common | `50-runtime-monitoring.ko.md` | `7c9fc83fc43202fae89864a8870b646ad788c7e49f85c4b3c5ec785274b49a94` |
| common | `51-runtime-metrics.ko.md` | `d808af4314ff9e1a3531275310a2aa2325403c74f362fb637da60c35937653dd` |
| common | `52-message-flow-tracing.ko.md` | `516df0e441d62169b57ae642d8e9301778c8d8e8ac7a849a9703bd7c02f6731e` |
| common | `53-flow-correlation.ko.md` | `1622ba04984576c56281f403b975b67e9d8e52e4298af392c6a543ff283d2511` |
| common | `54-graceful-drain-handoff.ko.md` | `4395cee67501d0a5dc17385acedd6a2e52824d2226b8d65b654b26afd9b3057e` |
| common | `90-implementation-gap.ko.md` | `46c99b8b66e35950f34a40fafd81c4fa16bbfdf756d64a694cff18a3c8dcf01e` |
| common | `README.ko.md` | `a7e2944d4f73accc42e4efe35f7758048361ab6fb584ffaab61fbf5d3749950e` |
| node | `01-system-structure.ko.md` | `5805a37bb89c8b1b9dcb104dc841d30e26126ff44f52620e94ac2f7760c03a60` |
| node | `02-handler-interfaces.ko.md` | `bd4c9dc272271b265a7447a2bdd951b964268554855d02a4b1c4c3571785b639` |
| node | `03-stream-connector.ko.md` | `108146aff90ea2d6fe1adc055e25b20ed55a31deb26782aa02a2abd978d0a0cf` |

공통 `02-framework-interfaces.ko.md`는 정식 Node.js 언어 interface 3개 분모에서는 제외한다. 다만
공통 spec 26개 분모에는 포함해 G0 hash를 고정했으며, G7 문서 정합성 검토 대상으로도 유지한다.

bindings 기준은 `@zlink-systems/zlink` 9.0.2이며, package의 public `version()`이 보고하는 core
runtime도 9.0.2이다.
G0에서 고정한 실제 artifact 증거는 다음과 같다.

| 항목 | 값 |
|------|----|
| 중앙 pin | `framework/languages/node/package.json`의 `file:../../../.artifacts/wsl/npm/zlink-systems-zlink-9.0.2.tgz` |
| archive 절대경로 | `/home/hep7/project/kairos/zlink/.artifacts/wsl/npm/zlink-systems-zlink-9.0.2.tgz` |
| archive SHA-256 | 최종 package 검증에서 9.0.2 artifact 기준으로 기록 |
| 설치 package 절대경로 | `/home/hep7/project/kairos/zlink/framework/languages/node/node_modules/@zlink-systems/zlink/package.json` |
| lock integrity | `sha512-F17hOaFlex5l5IQB2i1znMbG7P7f8BlwOdUPxmVyK32xmjk8EWgn93C8oRV/V6yFS2At66fOExLnEvD7uQSvrA==` |
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
| browser connector flow 문맥 | browser handler의 async continuation flow 보존과 관련 없는 callback 격리 | `MFLOW-EXT-014`; browser async-context 계약 결정과 회귀 테스트 필요 | GAP |
| location과 Redis | 역할별 store, readiness/query/resolver, typed `Draining` row | location/store test, Config 6과 Node↔.NET smoke | PASS |
| codec | serializer selection, codec extension/registrar, compression과 flow marker | codec test, Config 4와 cross-language smoke | PASS |
| monitoring과 metrics | typed runtime events, OpenTelemetry meter와 낮은 cardinality label | monitoring/metrics test, Config 7과 11 | PASS |
| graceful drain | `ZLinkDrainControl`, typed result, NestJS shutdown 순서와 natural drain | drain test, Bingo와 Config 11 | PASS |
| package 경계 | framework/NestJS/connector/codec/location packages와 supporting stream-wire | source export test, ABI matrix, 실제 `.tgz` 7개 consumer | PASS |

공통 spec 26개와 Node 정식 계약 문서 3개는 위 축의 세부 행에 모두 연결했다. 세부 E2E 분모 181개는
`node-g6-e2e-ledger.ko.md`에서 selector와 marker 단위로 관리한다.
