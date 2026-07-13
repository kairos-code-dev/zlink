# Node.js G0 공개 계약 ledger

검토 기준일은 2026-07-13이다. 아래 문서는 Node.js 정식 공개 interface의 전체 분모이며,
각 문서의 규범 문장을 public symbol, runtime 동작, contract test와 E2E 항목에 연결해 검토했다.

| ID | 정식 interface 문서 | 검토 결과 |
|----|---------------------|-----------|
| ND-DOC-001 | `README.ko.md` | 공개 범위와 취소 규칙 검토 완료 |
| ND-DOC-002 | `handler-interfaces.ko.md` | Promise handler와 단일 완료 규칙 검토 완료 |
| ND-DOC-003 | `handler-interfaces.ko.md` | actor lifecycle, handle, join 계약 검토 완료 |
| ND-DOC-004 | `handler-interfaces.ko.md` | channel, route, fanout, timeout 계약 검토 완료 |
| ND-DOC-005 | `handler-interfaces.ko.md` | monitoring event와 등록 계약 검토 완료 |
| ND-DOC-006 | `handler-interfaces.ko.md` | module 구성과 public export 검토 완료 |
| ND-DOC-007 | `handler-interfaces.ko.md` | location store와 자동 연결 계약 검토 완료 |
| ND-DOC-008 | `handler-interfaces.ko.md` | Spot lifecycle, actor, timer 계약 검토 완료 |
| ND-DOC-009 | `handler-interfaces.ko.md` | stream node와 typed session 계약 검토 완료 |
| ND-DOC-010 | `handler-interfaces.ko.md` | bound session relay와 disconnect 계약 검토 완료 |
| ND-DOC-011 | `handler-interfaces.ko.md` | SpotNode capability와 route 계약 검토 완료 |
| ND-DOC-012 | `32-stream-connector.ko.md` | connector framing, flow, closing 계약 검토 완료 |

공통 `25-stage-wrapper-on-spot.ko.md`는 정식 언어 interface가 아닌 상위 사용 모델이므로 분모에서는
제외했다. G7 문서 정합성 검토 대상으로 유지한다.

bindings 기준은 `@zlink-systems/zlink` 9.0.1이며, package의 public `version()`이 보고하는 core
runtime도 9.0.1이다.
G0에서 고정한 실제 artifact 증거는 다음과 같다.

| 항목 | 값 |
|------|----|
| 중앙 pin | `framework/languages/node/package.json`의 `file:../../../.artifacts/wsl/npm/zlink-systems-zlink-9.0.1.tgz` |
| archive 절대경로 | `/home/hep7/project/kairos/zlink/.artifacts/wsl/npm/zlink-systems-zlink-9.0.1.tgz` |
| archive SHA-256 | `f0d15bac4cc05d175587b1b8969f83db34b50d290f7ee7efd4ea8a3b6199f31e` |
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

공통 spec 19개와 Node interface 12개는 위 축의 세부 행에 모두 연결했다. 세부 E2E 분모 181개는
`node-g6-e2e-ledger.ko.md`에서 selector와 marker 단위로 관리한다.
