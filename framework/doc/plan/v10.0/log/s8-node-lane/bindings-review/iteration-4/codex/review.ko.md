# S8 NODE bindings 전환 리뷰 — iteration 4 — R1 (opus / codex lane)

독립 adversarial 리뷰. 다른 리뷰어(R2) 결과·coordinator 해석을 판정 근거로 쓰지 않음. 정적 대조만(build/실행 없음).

## 1. Scope 확인
- 대상 commit `006d34f97` (freeze `7a68f7973`)
- 파일 수 **140** (일치)
- aggregate SHA-256 (`LC_ALL=C sort` 재sha256sum): `967205f1af41bf948f105b576167f21536d3a59094cb06ed675c9f4674fa3963` (**일치**)
- 시작=종료 hash 동일, 파일 수정 없음.

## 2. iter-3 finding 해소 판정
| finding | 위치 | 판정 | 근거 |
|---|---|---|---|
| R2 NF3-1: option 테이블 phantom/누락 | `runtime/options/option_mapping.ts` | **해소** | phantom `0x3035`/`0x3036` 제거됨; `AUTO_HWM_MSG_UNIT_BYTES=0x3034` 존재. 아래 I1에서 전 범위 Core 일치 검증. |
| R1: MonitorSourceKind JSDoc가 제거된 spot pub/sub source 서술 | `contracts/eventing/monitor.ts:5` | **해소** | JSDoc="Core 10.0.0 defines only the socket source", 값 `Socket:1` 단일. spot pub/sub 언급 없음. |

두 finding 모두 해소. 새 반례 없음 → 재개 안 함.

## 3. I1 계약 일치 — **CLEAN** (blocker/high/medium 0)

### 3.1 옵션 id 테이블 (`SocketOption`) vs Core
Core `core/include/zlink_enum.h` 대조. 전 항목 정확 일치:
- `zlink_option_t` 0x3001~0x3034 + `SUBMIT_RETRY_*` 0x3037~0x3039. 0x3035/0x3036 gap은 node 테이블에도 부재(정합). `AUTO_HWM_MSG_UNIT_BYTES=0x3034` 존재.
- `zlink_router_option_t` 0x3101/0x3103~0x3106 (node: `ROUTER_MANDATORY`, `PROBE_ROUTER`, `CONNECT_ROUTING_ID`, `ROUTER_REQUEST_TIMEOUT_MS`, `ROUTER_WEIGHT`).
- `zlink_dealer_option_t` 0x3201~0x3203.
- `zlink_pub_option_t` 0x3301~0x3309 (node `XPUB_*` 9종 일치).
- `zlink_sub_option_t` 0x3400, `zlink_stream_option_t` 0x3501.

### 3.2 기타 enum vs Core
- `SocketType` 0x1001~0x1008 + `ANY=0` = `zlink_socket_type_t` 정확 일치. (UPPER/PascalCase 이중 별칭은 동일 값 매핑, 계약 위반 아님.)
- `MonitorEventType` `1<<0`~`1<<15` = `zlink_socket_monitor_event_e` 정확 일치.
- `PollEventFlag` {1,2,4,8,32} = `ZLINK_POLLIN/OUT/ERR/PRI/COMPLETION`. `POLLITEMS_DFLT=16`은 이벤트 플래그 아님 → 정당히 제외.
- `RidDuplicatePolicy` {Reject0, Handover1} = `zlink_rid_duplicate_policy_t`.
- `SubmitRetryMode` {Off0, LocalFailure1} = `zlink_submit_retry_mode_t`.

### 3.3 native decl == registration (bijection)
- C++ `ZLINK_METHOD("jsName", fn)` 등록(`addon_exports.cc` 외) **175** ↔ TS `runtime/native/*NativeBinding` 선언 **175**. 양방향 차집합 0 — 완전 bijection. registered-but-undeclared / declared-but-unregistered 모두 없음.

### 3.4 pull dispatch
- mesh_node 완료가 pull-dispatch 파이프라인으로 전달됨을 계약(`contracts/service/mesh_node.ts`)·runtime(`runtime/native/binding_service.ts`)에서 일관 서술. request/lookup/join/leave/destroy 완료 경로 정합.

Verdict: **CLEAN**.

## 4. I2 POSD·DDD — **CLEAN** (blocker/high/medium 0)
- **레이어링**: `contracts/`가 `runtime/`를 import하지 않음(참조는 JSDoc 산문뿐). 방향은 `runtime → contracts` 단방향.
- **수명주기/자원**: one-shot request TSFN은 `release_request_tsfn`으로 정확히 1회 해제, 실패 경로도 payload 소멸+`close_recv_parts`. ready-handler bridge는 재진입 JS-thread 케이스 처리·Core 등록 실패 시 abort·drain 후 해제. TS `NativeHandle.close()`·batch closer들 null-guard로 double-free 방지. request 진행 releaser는 refcount·idempotent, 1ms poll은 `unref()`. native handle leak / double-free / UAF / TSFN slot leak 없음.
- **에러 처리**: `executeCallbackRequest`/`executePromiseRequest`가 모든 경로(catch 포함)에서 `releaseProgress()` 보장. dispatch 루프 내 삼킨 에러 없음(문서화된 best-effort catch만).

Verdict: **CLEAN**.

## 5. I3 정리 (폐기 no-hit·dead code) — **CLEAN** (blocker/high/medium 0; low 4건)
- TODO/FIXME/HACK/XXX 마커 없음, 주석 처리 코드 블록 없음, "removed/deprecated/legacy" 서술 없음.
- **폐기 no-hit**: 제거된 기능(spot pub/sub 등)의 잔존 참조 없음. `spot`은 활성 first-class mesh 참가자(`spotPublish` → `addon_mesh_service.cc`)이며 `spot_pubsub_example.ts`가 live 채널-토픽 pub/sub 구동 → 오탐 아님. coordinator no-hit 0과 정합.
- 17개 실행 샘플 전부 `run_samples.sh` 등록, orphan 없음.

### low finding (CLEAN 불차단)
| # | 심각도 | 위치 | 내용 |
|---|---|---|---|
| L1 | low | `contracts/messaging/operations.ts:10` `ReplyHandler` | export type, repo 전역 0참조. `RequestCallback`(line 8, `readonly Message[]`)와 near-duplicate이며 이쪽이 실사용. 어떤 method 시그니처도 `ReplyHandler`를 받지 않음 → 소비 불가한 중복 public 타입. |
| L2 | low | `contracts/eventing/poller.ts:8` `PollEvent` | export interface, 0참조. `Poller.wait()`는 `PollEvents` 버퍼를 채우고 count 반환, 결과는 `PollEvents.sourceKind(i)/slot(i)/revents(i)/fd(i)`로 위치 접근 → 어떤 API도 `PollEvent`를 생성/반환 안 함. |
| L3 | low | `contracts/service/shared.ts:15` `SpotSendReadyHandler` | export type, 0참조. `SocketSendReadyHandler`와 병렬이나 `Spot` 계약에 send-ready 등록 method 부재 → 소비자 없음. |
| L4 | low | `contracts/messaging/message.ts:6,8` `METADATA_KEY_USER_MIN`/`METADATA_VALUE_MAX` | export const, 0참조. messaging barrel 미재수출(모듈 수준에서도 dead). JSDoc="metadata lookup is not implemented yet" — 미구현 기능 예약 상수. |

### 심각도 판정 근거 (adversarial 재검토)
L1~L4는 미참조 export지만 **low**로 판정한다:
- 정확성/런타임 영향 0 (tsc green, 어떤 소비자도 잘못된 동작을 얻지 않음).
- **Core 계약 오표기 아님**: iter-3 phantom option id(0x3035/0x3036)는 존재하지 않는 Core 옵션을 주장 → 소비자가 설정 시 UB이므로 정당히 상위 심각도였다. L1~L4는 그런 허위 계약 주장이 없는 단순 미참조 TS 편의 타입/상수 → 범주적으로 낮음.
- iteration-4 규칙상 low는 CLEAN을 막지 않음. (권장: 다음 정리 사이클에서 제거하거나 `RequestCallback`으로 수렴.)

*(주: 하위 조사 sweep는 L1을 medium으로 제안했으나, 상기 근거로 R1은 low로 override한다. sweep는 조사 보조이며 판정자는 R1.)*

Verdict: **CLEAN** (iteration-4 규칙).

## 6. Coordinator 증거 (재실행 안 함)
manifest: addon node-gyp green, tsc src+samples green, no-hit 0. SocketOption=Core `zlink_option_t` 정확 일치.

## 7. 종합
- iter-3 finding 2건 모두 해소.
- I1 / I2 / I3 세 축 모두 blocker/high/medium 0. low 4건은 별도 기록, CLEAN 불차단.

BINDINGS REVIEW CLEAN
