# ZoneWorld 샘플 구현 계획 — 전 언어 server + 공통 브라우저 client

## 0. 작업 승인

이 문서가 ZoneWorld 작업 승인이다. 시나리오 정본
[`sample/zoneworld/README.ko.md`](../framework/common/sample/zoneworld/README.ko.md) §0.1의 승인
게이트를 이 계획으로 충족한다. 승인받지 않은 에이전트는 이 샘플의 구현·수정·삭제를 진행하지
않는다.

## 1. 목표

시나리오 정본의 내용을 **전부** 구현해 완료 상태로 만든다. 범위는 5개 언어 server 구현
(`dotnet`, `java`, `kotlin`, `node`, `cpp`)과 이들 전부에 연결되는 **TypeScript 브라우저 client
하나**다. 완료 판정은 정본 §13(구현 완료 기준), §11(client self-check `ZW-A1`~`ZW-F4`),
§12(smoke 실행 기준)를 모두 충족하는 것이다.

## 2. 정본과 권위

| 대상 | 정본 |
|---|---|
| 시나리오 — 월드 규격, 메시지 계약, 검증 기준 | [`sample/zoneworld/README.ko.md`](../framework/common/sample/zoneworld/README.ko.md) |
| 공개 계약 | [`spec/`](../framework/spec/README.ko.md)의 공통 spec과 언어별 spec |
| 저장소 규약 | `AGENTS.md` — 특히 "Framework public contract parity" |

샘플은 공개 계약의 근거가 아니다. **새 public API를 만들지 않는다.** 구현 중 표면이 부족해
보이면 그것이 진짜 능력 격차인지 언어별 관용 표현 차이인지 먼저 판정한다. spec이 뒷받침하지
않는 기능은 public API로 추가하지 말고 별도 draft로 분리해 보고한다.

## 3. 작업 위치

정본 §0.2가 정한 배치를 따른다. **server와 headless 시나리오 client는 다른 정본 샘플과 같은
자리**(`framework/languages/<lang>/samples/ZoneWorld/`)에 두고, **공유되는 브라우저 client만**
`shared_sample`에 따로 둔다.

```text
framework/languages/dotnet/samples/ZoneWorld/    # 다른 정본 샘플과 같은 위치
  Shared/  Server/  Client/  run_sample.sh
# java·kotlin·node·cpp도 각자 framework/languages/<lang>/samples/ZoneWorld/ 아래 같은 구성

framework/languages/shared_sample/zoneworld/
  client/     TypeScript 브라우저 client — 5개 언어 server가 공유한다 (Phase 2)
```

**`<lang>/samples/ZoneWorld/Client/`는 그 언어의 시나리오 client다**(§6.2) — 정본 6종의 `Client/`와
같은 형태로, server 동작을 headless로 검증한다. `shared_sample`의 브라우저 client는 화면을 보여
준다. 시나리오 client는 언어별이므로 각 언어가 자기 것을 구현하고, 브라우저 client는 하나를 공유한다.

server 디렉터리 구조는 정본 §6, 브라우저 client 구조는 §9.3을 따른다. 기존 정본 6종은 건드리지
않는다. **아직 정본 aggregate(`dotnet/samples/run_samples.sh` 목록·`samples/README.md` 표)에는
등록하지 않는다** — 4개 언어 완료·blocker 해소 후에 넣는다(정본 §0.2).

## 4. 이미 확인된 선행 조건 (재조사 불필요)

- **browser connector 준비 완료.** `@zlink-systems/stream-connector`는 browser-only ESM package
  root이며 public transport는 `WebSocket`·`WebSocketSecure`만 남았다. 실제 Chromium에서 `ws`/`wss`
  request/reply, push, reconnect, drain, close를 검증했다. 근거는
  [`spec/90-implementation-gap.ko.md`](../framework/spec/90-implementation-gap.ko.md) §4.10
  (gap 없음으로 종결).
- **필요한 framework 표면이 5개 언어에 전부 있다.** channel fanout(`AddFanoutChannel` 계열),
  runtime event(location·socket·spot), actor cross-node transfer와 transfer adapter, spot pub/sub,
  spot timer, spot bridge, owner 일관 channel. 관련 spec 행(10·20·21·23·50)이 전 언어 충족이다.
- **C++ ATD-C3B는 해소됐다**(커밋 `c28814232`). timer handler에서 outbound request를 await하는 것에
  제약은 없다. 근본 원인은 stream connector가 heartbeat pong을 `dispatch()`에서만 보낸 것이었고,
  timer와 무관하게 heartbeat 창보다 오래 걸리는 모든 동기 request가 대상이었다. 다만
  `90-implementation-gap.ko.md` §5.1과 ledger `CPP-ATD-TIMER-RESUME-001`이 아직 갱신되지 않아 열린
  것처럼 보인다. **그 서술은 원인 오귀속이므로 무시한다.**

## 5. 알려진 제약과 함정

- **zone 노드는 같은 언어끼리 묶는다.** `CPP-FANOUT-WIRE-001`이 열려 있다 — framework에 부착된
  SPOT의 multipart publish가 첫 파트만 전달되는 core 결함 때문에, C++은 SPOT pub/sub을 `ZLFE` 단일
  프레임으로 발행하고 다른 언어 SPOT 구독자는 2-part envelope만 해석한다. ZoneWorld는
  `zone-node-1`·`zone-node-2`가 같은 언어이므로 경계 동기화(정본 §4.1)가 이 결함에 걸리지 않는다.
  **언어를 섞은 노드 구성을 만들지 않는다.**
- **client 빌드 스택이 없다.** 정본 §9.2가 지정한 Vite·Preact·@preact/signals·Vitest가
  devDependencies에 없다. 새로 들인다.
- **기존 브라우저 하네스는 node workspace에 묶여 있다.** `framework/languages/node/`의
  `scripts/browser-e2e/`(install-chromium·run-sample·run-e2e-client·connector-driver),
  `test/browser/stream-connector-chromium.test.js`, `npm run build:browser`가 esbuild + Playwright
  Chromium 구동의 좋은 출발점이지만 `samples/<X>/Client/main.ts` 경로를 전제한다. **공통 client
  하나를 5개 언어 러너가 각각 띄우는 구조로 배선을 새로 짠다.**
- **maintenance store는 zlink 표면이 아니다.** 앱이 소유하는 Redis 접근이다(정본 §8.4). 언어별
  Redis 클라이언트 의존을 `ZoneNode`·`Ops`에 추가하고 location store와 같은 Redis를 쓴다.
- **RouteMesh channel은 spot bridge 용도로만 등록한다**(정본 §1.1·§4). 애플리케이션 노드 지정에는
  owner 일관 channel(`zoneworld.ops.<NodeId>`)을 쓴다.

## 6. 진행 순서

### 6.1 Phase 0 — 계약 고정

정본 §7의 message 계약을 언어 중립으로 확정한다. 5개 언어 server DTO와 client DTO가 같은 필드와
같은 wire를 갖게 한다. `ActorRefWire`(정본 §7.4)는 기존 Bingo 방식을 따른다. 이 단계의 산출물이
이후 모든 언어의 기준이므로, 계약이 흔들리면 뒤의 언어가 전부 재작업된다.

### 6.2 Phase 1 — 언어별 server + **언어별 시나리오 client** (직렬)

**언어 순서는 `dotnet` → `java` → `kotlin` → `node` → `cpp`다.**

**한 번에 한 언어만 진행한다.** 여러 언어를 동시에 작업하지 않는다. 앞 언어가 §6.4의 완료
게이트를 통과하기 전에는 다음 언어에 착수하지 않는다. 동시 진행은 계약 드리프트와 미완성 코드의
상호 복제를 낳는다.

#### 브라우저 client보다 **언어별 시나리오 client**를 먼저 만든다

각 언어는 server와 함께 **그 언어의 시나리오 client**를 구현한다. 기존 정본 6종이
`Client/`에 두는 것과 같은 형태다 — 브라우저 없이 stream connector로 붙어 시나리오를 순서대로
실행하고 성공·실패를 종료 코드와 로그로 보고하는 headless client다.

브라우저 client를 먼저 만들지 않는 이유는 셋이다.

- **server 동작을 결정적으로 검증한다.** 정본 §11의 `ZW-A1`~`ZW-F4`는 전부 wire에서 관찰
  가능한 동작이다. 브라우저·렌더·타이밍을 끼우면 server 결함과 UI 결함이 섞여 원인이 흐려진다.
- **포팅 기준이 된다.** 언어별 시나리오 client가 있으면 다음 언어를 포팅할 때 같은 시나리오를
  그 언어에서 그대로 돌려 server 동작이 같은지 즉시 대조할 수 있다.
- **기존 샘플과 같은 모양이다.** 정본 6종의 언어별 러너 규약(`run_sample.sh` → 서버 기동 →
  client 시나리오 실행 → 성공 로그)을 그대로 따르므로 새 하네스를 발명하지 않는다.

브라우저 client(§6.5)는 5개 언어 server가 모두 이 관문을 통과한 뒤에 만든다. 그때 브라우저가
검증하는 것은 **화면**(정본 §10)이고, server 동작은 이미 시나리오 client가 보증한 상태다.

#### `dotnet`은 기준 구현이고, 나머지 4개 언어는 포팅이다

**`dotnet` server와 `dotnet` 시나리오 client를 먼저 구현한다.** 3역할(`Gateway`, `ZoneNode`×2,
`Ops`)과 정본 §6의 레이어 분리(Domain / Application / Ports / Infrastructure), 타입 분해, 책임
배치, 상태 전이, 오류 분류를 여기서 확정한다. 시나리오 client의 시나리오 분해와 성공 기준도 여기서
확정하고, 나머지 언어는 같은 시나리오를 같은 순서로 옮긴다.

**`java`·`kotlin`·`node`·`cpp`는 `dotnet` server의 설계와 구현을 포팅하는 개념으로 진행한다.**
각 언어에서 설계를 새로 하지 않는다. 백지에서 다시 짜면 언어마다 구조가 갈라져, 같은 샘플을 읽는
사용자가 언어별로 다른 것을 배우게 된다.

포팅의 기준선은 **관문 3까지 수렴한 `dotnet` 구현**이다(§6.4). 기능만 끝난 중간 상태가 아니라,
POSD/DDD 리팩토링이 수렴한 최종 형태를 옮긴다. 이것이 언어를 직렬로 진행하는 이유다 — 기준이
흔들리는 동안 뒤 언어를 시작하면 그 리팩토링을 4번 다시 하게 된다.

포팅에서 **유지하는 것**과 **바뀌어도 되는 것**을 구분한다.

| 유지한다 (언어가 달라도 같아야 한다) | 바뀌어도 된다 (언어 관용 표현) |
|---|---|
| 역할 분리와 레이어 경계, 의존 방향 | 등록 문법(interface·attribute·decorator·template) |
| 타입 분해와 책임 배치, 파일 단위 구성 | 비동기 표현(`ValueTask`·`CompletionStage`·`suspend`·`Promise`·coroutine) |
| 메시지 이름, 필드, wire | 명명 관례(casing), 컬렉션·불변 타입 선택 |
| 상태 전이와 검증 순서, 오류 분류 | build system, DI 컨테이너, 패키지 구조 관례 |
| 정본 §7(반드시 지킬 설계 결정) 전부 | 언어 표준 라이브러리 활용 방식 |

**포팅 중 `dotnet` 설계의 결함을 발견하면 그 언어에서 우회하지 않는다.** `dotnet` 기준 구현을
먼저 고치고, 이미 포팅한 언어에 그 수정을 반영한 뒤 진행한다. 기준이 하나여야 포팅이 성립한다.

### 6.4 언어별 완료 게이트

한 언어는 아래 세 관문을 **순서대로** 통과해야 완료다. 하나라도 통과하지 못하면 다음 언어로
넘어가지 않는다.

#### 관문 1 — 기능 완료 (그 언어의 시나리오 client로 검증한다)

그 언어 server가 **그 언어의 시나리오 client**와 함께 정본 §11의 self-check `ZW-A1`~`ZW-F4`를
**전부** 통과하고, §12의 smoke 순서와 성공 로그 마커를 만족한다. `ZW-D2`는 §11.1의 세 번째
노드(`zone-node-3`, zone 없이 subscriber만)로 검증한다. 통과하지 못한 항목을 통과한 것처럼 적지
않는다.

이 관문에 브라우저는 등장하지 않는다. `ZW-A1`~`ZW-F4`는 전부 wire에서 관찰 가능한 동작이므로
headless 시나리오 client로 판정한다. 브라우저는 §6.5에서 **화면**을 검증한다.

#### 관문 2 — POSD/DDD 리팩토링 반복

기능이 완료된 코드를 POSD·DDD 기준으로 리팩토링한다. **한 번으로 끝내지 않고 반복한다.**

각 회차는 다음을 수행한다.

1. 그 언어의 ZoneWorld 구현 전체를 대상으로 리팩토링 후보를 도출한다. 관심 축은 기존 POSD 리뷰
   문서와 같다 — 삭제 가능한 vestigial 코드, 결함, 중복, god-file, 레이어 경계 침범, 정책 다중화.
2. 후보를 반영한다.
3. 반영 후 **관문 1의 검증을 다시 돌려 회귀가 없음을 실측으로 확인한다.**

#### 관문 3 — codex 수렴 확인

관문 2의 각 회차 끝에서 **codex 에이전트로 리팩토링 리뷰를 받는다.** codex가 **더 이상 의미 있는
리팩토링 요소를 제시하지 않을 때까지 관문 2와 관문 3을 반복한다.** codex가 제시한 항목이 없거나
남은 항목이 전부 "의미 없음"으로 판정될 때 그 언어가 수렴한 것으로 본다.

codex 사용 규약:

- **한 요청에 한 항목만 넣는다.** 여러 항목을 묶어 한 번에 요청하지 않는다. 항목이 여럿이면
  병렬로 여러 요청을 낸다. 묶으면 리뷰 밀도가 떨어진다.
- codex는 **리뷰와 진단**을 맡는다. 코드 반영은 이 세션이 직접 한다.
- codex가 제시한 항목을 반려할 때는 반려 사유를 기록한다. "의미 없음" 판정을 근거 없이 내리지
  않는다.

수렴 판정과 그 근거(회차 수, 각 회차에서 반영한 항목, 마지막 회차에서 codex가 제시한 항목과 그
처리)를 §8의 진행 기록에 남긴다.

### 6.5 Phase 2 — 공통 브라우저 client

**5개 언어 server가 모두 §6.4의 세 관문을 통과한 뒤에** 시작한다. 이 시점에 server 동작은 언어별
시나리오 client가 이미 보증한 상태이므로, 브라우저가 새로 검증하는 것은 **화면**이다.

TypeScript 브라우저 client를 `client/`에 구현한다. 런타임 패턴은 단방향 데이터 흐름(정본 §9.1),
폴더 구조는 Feature-Sliced Design(§9.3), 화면 규격과 UI 품질은 §10을 따른다. 이 단계에서 client
빌드 스택(Vite·Preact·signals·Vitest)과 Playwright 러너 배선을 함께 세운다.

client는 하나이고 server가 5개이므로, **client는 특정 언어 server에 의존하는 코드를 갖지 않는다.**
`Gateway`와 `Ops`의 주소만 설정으로 받는다.

### 6.6 Phase 3 — 교차 검증

**같은 브라우저 client 하나**로 5개 언어 server 전부에 대해 정본 §12의 smoke 순서를 재실행한다.
언어별로 따로 통과한 것과 하나의 client가 전부에 붙는 것은 다른 검증이다.

## 7. 반드시 지킬 설계 결정

정본에서 발췌한다. 어기면 5개 언어의 결과가 갈린다.

- **좌표 권위는 player actor**이고 zone spot은 사본만 보관한다(§2.1).
- **이동 검증 순서 고정** — `OutOfRange` → `TooFar` → `DiagonalCrossing` → `ZoneMaintenance`.
  먼저 걸린 사유 하나만 반환한다(§2.2).
- **점검 모드는 진입만 막는다.** 이미 그 노드에 있는 플레이어의 이동은 막지 않는다. 캐시는
  최적화이고 **권위는 목표 노드**다(§2.3).
- **transfer adapter를 반드시 등록한다.** 등록하지 않으면 actor가 factory로 재생성되어 좌표가
  유실되고 샘플이 성립하지 않는다(§2.6).
- **봇 경로는 결정적이다.** §2.7의 초기 좌표·방향 표를 그대로 쓴다. 무작위 이동은 쓰지 않는다.
- **플레이어 정렬은 `PlayerId` UTF-8 byte 오름차순**(ordinal)이다. 언어 기본 로캘 비교를 쓰지
  않는다(§2.4).
- **공지는 best-effort다.** 중복은 client가 `AnnouncementId`로 제거하고 유실은 허용한다(§8.2).
- **client state는 서버 push로만 바뀐다.** 입력이 state를 직접 쓰지 않는다(§9.1).
- **관제 화면은 polling하지 않는다.** `NodeStatusNotify` push로만 갱신한다(§10.2).
- **UI 품질 요구**(§10.0)를 만족한다. 이 샘플은 사람이 화면을 보고 판단하는 것이 검증 수단이므로,
  화면이 읽히지 않으면 목적이 성립하지 않는다.

## 8. 진행표

**이 절이 진행 상태의 유일한 기록이다.** 정본의 모든 요구를 항목으로 펼쳐 두었으므로, 여기를
전부 채우면 누락이 없다.

표기는 `[ ]` 미착수, `[~]` 진행 중, `[x]` 완료다. **`[x]`는 실측 근거가 있을 때만 쓴다.** 코드를
작성했다는 것은 완료가 아니다. 그 항목을 검증하는 실행이 통과해야 완료다.

### 8.0 전체 요약

| 단계 | 내용 | 상태 |
|---|---|---|
| Phase 0 | 계약 고정(§8.1) | **완료** — dotnet 계약 빌드 성공, client 계약 `tsc --noEmit` 통과 |
| Phase 1 — `dotnet` | **기준** server + 시나리오 client + 3관문(§8.3·§8.4·§8.5) | **완료.** 수동 peer 배선 없이 `run_sample.sh all` **30/30 3회 연속 + §12 마커 7종 전량**을 확인했다. 자동 연결의 `Busy` 오판과 같은 endpoint owner 교체 결함, 지연 구독 수신 뒤 readiness signal이 남는 core 결함을 고쳤다. 문제 집중 조합(`ZW-B2,ZW-B4,ZW-D1`)도 5회 연속 확인했다(§8.5.4·§8.5.6). 개별 핸들러 수동 등록 없이 assembly scan과 handler group만 사용한다. |
| Phase 1 — `java` | `dotnet` 포팅 + 시나리오 client + 3관문 | 미착수 |
| Phase 1 — `kotlin` | `dotnet` 포팅 + 시나리오 client + 3관문 | 미착수 |
| Phase 1 — `node` | `dotnet` 포팅 + 시나리오 client + 3관문 | 미착수 |
| Phase 1 — `cpp` | `dotnet` 포팅 + 시나리오 client + 3관문 | 미착수 |
| Phase 2 | 공통 브라우저 client(§8.6) | **client 구현 완료.** Vite·Preact signals·Canvas 2D·stream connector·Vitest·Playwright를 구성했다. `dotnet` 실서버에서 actor transfer 중 WebSocket 유지, 노드별 점검·진단, 실제 노드 종료의 server push를 3/3 확인했다. |
| Phase 3 | 교차 검증(§8.7) | `client × dotnet` 완료. 브라우저 3/3 뒤 같은 실행에서 server 30/30도 통과했다. 나머지 언어 server 구현 뒤 같은 client로 검증한다. |
| 완료 | 최종 확인(§8.8) | `dotnet` 범위 완료. 5개 언어 전체 완료 판정은 보류한다. |

### 8.1 Phase 0 — 계약 고정

**게임 message — 브라우저 ⇄ Gateway** (정본 §7.1)

- [x] `JoinWorldReq` — `PlayerId`
- [x] `JoinWorldRes` — `PlayerId`, `ZoneId`, `NodeId`, `X`, `Y`
- [x] `MoveMsg` — `X`, `Y` (one-way send)
- [x] `ZoneStateNotify` — `ZoneId`, `Tick`, `Players`
- [x] `ZoneChangedNotify` — `PlayerId`, `ZoneId`, `NodeId`, `Transferred`
- [x] `WorldAnnounceNotify` — `AnnouncementId`, `Text`
- [x] `MoveRejectedNotify` — `Reason`, `X`, `Y`

**관제 message — 브라우저 ⇄ Ops** (정본 §7.2)

- [x] `WatchNodesReq` / `WatchNodesRes`
- [x] `NodeStatusNotify`
- [x] `NodeAlertNotify`
- [x] `AnnounceWorldReq` / `AnnounceWorldRes`
- [x] `SetMaintenanceReq` / `SetMaintenanceRes` (`Error=NodeUnavailable` 포함)
- [x] `NodeDiagnosticsReq` / `NodeDiagnosticsRes` (`Error=NodeUnavailable` 포함)

**서버 내부 message** (정본 §7.3)

- [x] `WorldAnnounceEvent` (fanout, topic `world.announce`)
- [x] `NodeMaintenanceChangedEvent` (fanout, topic `world.maintenance`)
- [x] `DeliverAnnounceMsg` (spot bridge 경유)
- [x] `BotTickMsg` (인자 없음 — 수신측이 tick 값을 읽지 않아 회차 2에서 제거했다)
- [x] `ApplyNodeMaintenanceReq` / `ApplyNodeMaintenanceRes` (owner 일관 channel)
- [x] `GetNodeDiagnosticsReq` / `GetNodeDiagnosticsRes` (owner 일관 channel)
- [x] `ReportSpotEventMsg` (이벤트 시)
- [x] `ReportNodeStatusMsg` (1초 주기)
- [x] `ZoneBorderEvent` (spot pub/sub)
- [x] `EnterZoneMsg` — `PlayerId`, `X`, `Y`, `IsBot`, `FromNodeId` (join payload) / `EnterZoneRes`
- [x] `EnsurePlayerActorReq` / `EnsurePlayerActorRes` (`zoneworld.actors`, 입장 zone 호스팅 노드만 서빙)
- [x] `EnterWorldReq` / `EnterWorldRes` (entry spot 경유 actor 입장)
- [x] `ActorRefWire` — `NodeRid`, `ActorId`, `Generation` (정본 §7.4)

**삭제한 계약** — 구현하며 죽은 계약으로 판명되어 정본과 함께 제거했다(§8.10).

- `UpdatePositionMsg` — actor와 zone spot이 같은 handler 문맥에 있어 메시지 없이 직접 갱신한다
- `LeaveZoneMsg` — zone 이동은 `JoinSpot`이므로 이전 spot 퇴장은 framework의 `OnLeaveActor`가 알린다

**이름 규약** (정본 §4)

- [x] `zoneworld.zones` (Spot mesh)
- [x] `zoneworld.bridge` (route mesh — **spot bridge 전용**)
- [x] `zoneworld.ops.<NodeId>` (owner 일관 channel)
- [x] `zoneworld.broadcast` (fanout channel)
- [x] `zoneworld.report` (client-server channel)
- [x] `zoneworld.actors` (client-server channel)
- [x] 경계 topic `zone.border.<from>.<to>` **8종**(정본 §4.1) — 대각선 zone은 topic이 없다
- [x] payload codec = JSON

**월드 규격 상수** (정본 §2) — 5개 언어와 client가 같은 값을 쓴다

- [x] 좌표 `0..99`, zone 4분할, 노드 배치(서=`zone-node-1`, 동=`zone-node-2`)
- [x] 경계 밴드 10, tick 100ms, 봇 timer 500ms, 이동 제한 축당 5, 입장 좌표 `(25,25)`

### 8.2 Phase 1 — 언어별 시나리오 client

**브라우저 없이** stream connector로 붙어 정본 §11의 시나리오를 순서대로 실행하는 headless
client다. 기존 정본 6종의 `Client/`와 같은 형태이며, 그 언어 server의 관문 1을 판정한다.

| 항목 | `dotnet` | `java` | `kotlin` | `node` | `cpp` |
|---|:--:|:--:|:--:|:--:|:--:|
| stream connector 연결(`Gateway`·`Ops` 두 종단) | [x] | [ ] | [ ] | [ ] | [ ] |
| 시나리오 러너 — `ZW-*` 개별 실행과 `all` 실행 | [x] | [ ] | [ ] | [ ] | [ ] |
| 게임 시나리오 `ZW-A*`·`ZW-B*` | [x] | [ ] | [ ] | [ ] | [ ] |
| 관제 시나리오 `ZW-C*`·`ZW-D*`·`ZW-E*` | [x] | [ ] | [ ] | [ ] | [ ] |
| 봇 시나리오 `ZW-F*` | [x] | [ ] | [ ] | [ ] | [ ] |
| 성공·실패를 종료 코드와 로그로 보고 | [x] | [ ] | [ ] | [ ] | [ ] |
| `run_sample.sh` — §12 순서로 기동 후 client 실행 | [x] | [ ] | [ ] | [ ] | [ ] |

**`dotnet` 시나리오 client가 기준이다.** 시나리오 분해, 단언, 대기 조건, 성공 로그를 여기서
확정하고 나머지 언어는 같은 시나리오를 같은 순서로 옮긴다.

### 8.3 Phase 1 — 언어별 server 요소

정본 §6의 요소를 전부 채운다. **`dotnet` 열이 기준 구현이고, 나머지 4개 열은 수렴한 `dotnet`
구현의 포팅이다**(§6.3). 언어별 문법과 build system은 달라도 요소 구성과 책임 배치는 같다.

| 요소 | `dotnet` | `java` | `kotlin` | `node` | `cpp` |
|---|:--:|:--:|:--:|:--:|:--:|
| **Gateway** — `PlayerSession` + `PlayerSessionBinder` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Spots** — `ZoneEntrySpot` (entry spot은 Gateway가 아니라 `ZoneNode`가 소유한다, §8.10) | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Handlers** — `EnsurePlayerActorHandler`, `PlayerJoinWorldHandler`, `PlayerEnterWorldHandler` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Domain** — `World`, `ZoneId`, `ZoneState`, `PlayerPosition` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Domain** — `MovePolicy`, `BorderView` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Application** — `MoveUseCase`, `ZoneTickUseCase` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Application** — `BotPatrolPolicy`, `NodeMaintenancePolicy` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Ports** — `MaintenanceStorePort`, `OpsReportPort` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Spots** — `ZoneSpot` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Spot handlers** — `EnterZoneHandler`, `UpdatePositionHandler`, `LeaveZoneHandler` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Spot handlers** — `ZoneTickHandler`, `BotTickHandler` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Spot handlers** — `ZoneBorderSubscriptionHandler`, `DeliverAnnounceHandler` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Actors** — `PlayerActor`, `PlayerActorFactory`, `BotSpawner` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Actors** — **transfer adapter 등록**(정본 §2.6) | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Handlers** — `WorldAnnounceSubscriber`, `NodeMaintenanceChangedSubscriber` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Handlers** — `ApplyNodeMaintenanceHandler`, `GetNodeDiagnosticsHandler` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Monitoring** — `LocalSpotEventHandler` | [x] | [ ] | [ ] | [ ] | [ ] |
| **ZoneNode Store** — `MaintenanceStoreRepository` | [x] | [ ] | [ ] | [ ] | [ ] |
| **Ops Application** — `NodeRegistry`, `AnnouncementService`, `MaintenanceService` | [x] | [ ] | [ ] | [ ] | [ ] |
| **Ops** — `OpsConsoleSession` | [x] | [ ] | [ ] | [ ] | [ ] |
| **Ops Handlers** — `WatchNodesHandler`, `AnnounceWorldHandler`, `SetMaintenanceHandler` | [x] | [ ] | [ ] | [ ] | [ ] |
| **Ops Handlers** — `NodeDiagnosticsHandler`, `ReportSpotEventHandler`, `ReportNodeStatusHandler` | [x] | [ ] | [ ] | [ ] | [ ] |
| **Ops Monitoring** — `LocationEventHandler`, `SocketEventHandler` | [x] | [ ] | [ ] | [ ] | [ ] |
| **Ops Store** — `MaintenanceStoreRepository` | [x] | [ ] | [ ] | [ ] | [ ] |
| **runner** — 정본 §12 순서로 서버 기동 + client 실행 | [x] | [ ] | [ ] | [ ] | [ ] |
| **`zone-node-3`** — subscriber만(정본 §11.1, `ZW-D2` 전용) | [x] | [ ] | [ ] | [ ] | [ ] |

### 8.4 관문 1 — 기능 완료 (그 언어의 시나리오 client로 판정)

정본 §11의 전 항목이다. **하나라도 미통과면 그 언어는 관문 2로 넘어가지 않는다.** 판정은 §8.2의
headless 시나리오 client로 한다. 브라우저는 이 관문에 등장하지 않는다.

**`dotnet` 관문 1 통과 — 정본 §11의 25개 전량 통과.** 회차 2에서 **러너 판정 5개**가 늘어 `run_sample.sh all`은 30개 검사를 돌린다(`ZW-D1`의 subscriber·zone spot 수신, `ZW-F1`의 봇 8마리, `ZW-F3`의 push 부재, `ZW-D2`, `ZW-F2`). **2회 연속 30/30 그린.**

`ZW-D2`·`ZW-F2`는 **러너가 서버 로그로 판정**하도록 구현했다 — 둘 다 client가 볼 수 없는 것을
주장하기 때문이다(`ZW-F2`는 **client의 부재**를, `ZW-D2`는 client에 노출되지 않는 **세 번째 노드**를).
러너가 `zone-node-3`(zone 없이 fanout subscriber만)를 함께 띄우고, client 실행 뒤 각 노드 로그를
확인한다.

`ZW-C4`는 **결함 주입**으로 구현했다 — 러너가 `ZONEWORLD_FAULT_TICK_ZONE=zone-nw`를 `zone-node-1`에만
주고(`env`로 국한), 그 zone spot의 tick handler가 **한 번** 던진다. 그 spot runtime event를 노드가
local로 받아 `ReportSpotEventMsg`로 Ops에 보고하고, 콘솔이 `NodeAlertNotify`로 받는다. 결함은 콘솔이
붙기 전에 일어날 수 있으므로 **Ops가 최근 경고를 보관했다가 `WatchNodesReq`에 재생**한다 — 운영자가
화면 앞에 없었다는 이유로 사라지는 경고는 경고가 아니다.

**러너에 프로세스 중단·재시작 능력을 추가했다** — `stop_node`·`start_zone_node`·`run_client_with_stop`.
client는 `Gateway`와 `Ops`만 알기 때문에 노드를 없애는 일은 러너만 할 수 있다. `ZW-B4`·`ZW-C2`·`ZW-E5`가
이 배선으로 **통과한다.**

**client 시나리오는 두 벌로 나눴다.** `Scenarios.All`은 client가 혼자 끝까지 몰 수 있는 것들이고,
`Scenarios.RunnerDriven`은 러너가 노드를 없애 줘야 하는 것들이다. `all`은 앞의 것만 돌린다 — 뒤의
것을 러너 없이 돌리면 그냥 timeout이 난다.

**구현하며 고친 것 셋.**

1. **owner lease가 너무 길었다.** 노드가 죽어도 location row가 기본 15초 동안 남아, `Registered=false`가
   시나리오 대기 시간 안에 오지 않았다. 세 서버 모두 `options.ConfigureLocations()`로
   `OwnerLeaseTtl=3s`, `HeartbeatInterval=1s`로 줄였다. §8.1이 "노드 등록·해제 = location runtime
   event"라고 정했으므로, 관측이 늦는 것은 lease 설정 문제지 설계 문제가 아니다.
2. **봇은 노드 재시작을 넘어 존속한다.** X축 순찰 봇은 경계를 넘으므로, `zone-node-2`가 재시작할 때
   "자기" 봇이 이미 `zone-node-1`에 살아 있을 수 있다. 그대로 다시 만들면 같은 id의 actor가 둘이 되어
   **location claim conflict로 기동이 실패했다.** `ZoneNodeBootstrap`이 이미 있는 봇은 만들지 않도록
   고쳤다(충돌 자체를 "이미 존재"의 권위 있는 답으로 해석한다).
3. **경고가 콘솔이 붙기 전에 발생하면 아무도 못 봤다.** Ops가 최근 경고를 보관했다가 `WatchNodesReq`에
   재생한다 — 운영자가 화면 앞에 없었다는 이유로 사라지는 경고는 경고가 아니다.

**그리고 둘 더.**

4. **`ZW-C3`을 위해 노드의 신원을 socket event에 실었다.** Ops는 report channel의 **server 소켓**에서
   socket event를 받는데, 그 이벤트가 어느 **노드**의 것인지 알 방법이 없었다(소켓만 안다). ZoneNode의
   report channel client에 `SetRoutingId(node.NodeRid)`를 주자 Ops의 `SocketEventHandler`가 rid로 노드를
   식별해 `Connected`를 갱신할 수 있게 됐다.
5. **`ZW-B4`의 간헐 실패는 순서 문제였다.** 노드를 죽였다 살리는 시나리오들이 연달아 돌면서, `ZW-B4`가
   **막 재시작한** `zone-node-2`로 플레이어를 걸어 들어가야 했다. `ZW-B4`는 `zone-node-2`를 **쓰고 나서**
   없애는 유일한 시나리오이므로 **맨 앞으로** 옮겼다 — 갓 돌아온 노드는 걸어 들어가기에 가장 불안정한
   상대다. 재시작 뒤 유예도 12초로 늘렸다.

| ID | 시나리오 | `dotnet` | `java` | `kotlin` | `node` | `cpp` |
|---|---|:--:|:--:|:--:|:--:|:--:|
| `ZW-A1` | 입장·이동 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-A2` | 이동 검증 순서(`OutOfRange` 우선) | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-A3` | 같은 zone 플레이어 목록·정렬 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-A4` | 대각선 경계 거부 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-A5` | 같은 zone 좌표 갱신 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-B1` | 경계 동기화(대각선 zone 제외) | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-B2` | 노드 간 transfer + WS 연결 유지 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-B3` | 노드 내부 zone 이동(transfer 없음) | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-B4` | 경계 snapshot 만료(3 tick) | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-C1` | 노드 관찰 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-C2` | 노드 종료 → `Registered=false` | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-C3` | 연결 단절 → `Connected=false` | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-C4` | spot 이벤트 보고(`TimerHandlerFailed`) | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-D1` | 전 노드 공지(발행자에 노드 목록 없음) | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-D2` | 노드 추가 시 공지(`zone-node-3`) | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-E1` | 노드 지정 점검(격리 확인) | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-E2` | 점검 중 기존 플레이어 이동 허용 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-E3` | 점검 중 이탈 허용 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-E4` | 노드 진단 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-E5` | 재시작 복원(maintenance store) | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-E6` | 점검 중 신규 입장 거부 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-F1` | 봇 8마리 존재·이동 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-F2` | 봇 노드 간 transfer(client 없이) | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-F3` | 봇에 push하지 않음 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-F4` | 봇 방향 반전 | [x] | [ ] | [ ] | [ ] | [ ] |

**smoke 마커** (정본 §12) — 언어별 runner 로그가 아래를 포함해야 한다.

| 마커 | `dotnet` | `java` | `kotlin` | `node` | `cpp` |
|---|:--:|:--:|:--:|:--:|:--:|
| `topology=ready` | [x] | [ ] | [ ] | [ ] | [ ] |
| `zoneworld-transfer=completed` | [x] | [ ] | [ ] | [ ] | [ ] |
| `zoneworld-border-sync=completed` | [x] | [ ] | [ ] | [ ] | [ ] |
| `zoneworld-ops-observe=completed` | [x] | [ ] | [ ] | [ ] | [ ] |
| `zoneworld-ops-announce=completed` | [x] | [ ] | [ ] | [ ] | [ ] |
| `zoneworld-ops-maintenance=completed` | [x] | [ ] | [ ] | [ ] | [ ] |
| `zoneworld=completed` | [x] | [ ] | [ ] | [ ] | [ ] |

**마커는 러너가 소유한다.** client 한 번의 실행은 스위트 전체를 보지 못한다 — 노드를 없애야 하는
시나리오는 러너가 몰고, 여섯 개는 서버 로그로 판정한다. 그래서 client가 자기 배치를 통과한 것을
`zoneworld=completed`로 찍으면 **완주하지 않은 성공 마커**가 된다(회차 3에서 그렇게 되어 있었다).
러너가 각 phase를 구성하는 시나리오가 **전부** 통과했을 때만 그 마커를 찍고, 하나라도 빠지면
`!! <marker> withheld: <ID> did not pass`로 보류 사유를 남긴다.

### 8.5 관문 2·3 — 리팩토링 반복과 codex 수렴

**언어마다 아래 표를 새로 채운다.** 회차는 codex가 더 이상 의미 있는 리팩토링 요소를 제시하지
않을 때까지 늘어난다. 회차 수를 미리 정하지 않는다.

`dotnet` 리팩토링 회차 기록:

| 회차 | 반영한 항목 | 관문 1 회귀 재실행 | codex가 제시한 항목 | 처리(반영/반려+사유) | 수렴 |
|---|---|:--:|---|---|:--:|
| 1 | ① 진단용 `JOIN-DIAG` try/catch 제거(`PlayerMoveHandlers`) ② 죽은 계약 `UpdatePositionMsg` 삭제(계약·정본 §7.3 동시) ③ 공지 시나리오가 정본을 과하게 단언하던 것 교정 ④ 런너 위생: `dotnet run` 래퍼 제거 + 유령 프로세스 사전 점검 | [x] `run_sample.sh all` **2회 연속 전량 통과** | (미실시) | | 아니오 |
| 2 | codex 4건 병렬 리뷰(ZoneSpot·Ops·시나리오 client·Gateway·maintenance store 중복) 결과 반영 — 아래 §8.5.1 | [x] `run_sample.sh all` **2회 연속 30/30** | 아래 §8.5.1 | 아래 §8.5.1 | 아니오 |
| 3 | codex 3건 병렬 리뷰(actor 계층 · domain wire-DTO · bootstrap/config) 결과 반영 — 아래 §8.5.2. **정본 위반 2건**(Ops 노드 목록, peer 수동 배선)과 **거짓 그린 2건**(ZW-B4 틀린 단언, §12 마커 미발행)을 함께 해소 | 아래 §8.5.3의 재실행으로 대체 | 아래 §8.5.2 | 아래 §8.5.2 | 아니오 |
| 4 | codex 수렴 게이트 리뷰 → **NOT CONVERGED 1건**(봇 스폰의 오류 삼킴). 반영 — 아래 §8.5.3. peer 배선 복원(§8.5.4), `ZW-B4` 재교정 | [x] `run_sample.sh all` **30/30 2회 연속 + §12 마커 7종 전량**. 3회차에서 `ZW-A4`가 `Request timed out`으로 1회 실패 — **잔여 flake, 미해소**(아래) | 아래 §8.5.3 | 반영 | 회차 5에서 재판정 |
| 5 | 검증 중 발견 2건 — **RoutingId 관례 위반**(§8.5.5), **`ZW-F3-no-push` 체크 위양성**(§8.5.5) 반영 | 아래 §8.5.5 재실행 | 아래 §8.5.5 | 반영 | 판정 대기 |
| 6 | 자동 연결 2건 수정(§8.5.4), 수동 핸들러 등록 제거, 실행 시점 사전 조건 마커 추가 | [x] `run_sample.sh all` **30/30 2회 연속**, `ZW-B4`·`ZW-E5` 각각 **5회 연속** | 자동 연결·자동 핸들러 경로 재검토 | 반영 | **dotnet 수렴** |
| 7 | 지연 구독 수신 뒤 readiness signal이 남는 core 결함 수정(§8.5.6), 실제 local package 9.0.8로 재검증 | [x] 문제 집중 조합 **5회 연속**, `run_sample.sh all` **30/30 3회 연속**, 공통 브라우저 실서버 **3/3** | 수동 연결·고정 sleep 없이 자동 연결과 자동 핸들러 경로 재검토 | 반영 | **dotnet 수렴** |
| 8 | POSD·DDD 재검토(§8.5.7): 전역 player identity, Ops use case 경계, 좌표→zone 규칙 소유권 정리 | [x] 강화한 `ZW-B2`, Ops 집중 조합, `run_sample.sh all` **30/30 + 공통 브라우저 3/3** | shallow service·adapter 누출·중복 규칙 제거 | 반영 | **dotnet 수렴** |

#### 8.5.5 검증 중 발견 2건 (문서 대비 정합·체크 정밀화)

"dotnet 샘플이 정본대로 구현·동작하는가"를 확인하다 둘을 잡았다.

1. **`ActorRefWire.NodeRid`가 정본 6종 관례를 어겼다.** ZoneWorld는 노드 rid를 wire에 `ToHex()`로
   싣고 `RoutingId.FromHex()`로 되돌렸는데, **정본 6종은 만장일치로 `RoutingId.ToString()`/
   `RoutingId.From(string)`** 을 쓴다(`RoutingId.From` 56곳, `FromHex` 0곳). §7.4가 "Bingo와 같은
   방식"이라 명시하는데 Bingo는 `actor.NodeRid.ToString()`(인코드)·`RoutingId.From(snapshot.NodeRid)`
   (디코드)다. **안전성:** `RoutingId.ToString()`은 인쇄 가능한 UTF-8이면 그 문자열을 그대로 반환하고
   (`TryToUtf8String`이 uint/guid/hex 폴백보다 먼저), 이 샘플의 노드 rid는 `zn1`·`zn2`·`zn3`라 왕복이
   성립한다. → 두 곳(`ZoneNodeHandlers` 인코드, `PlayerSession` 디코드)을 `ToString()`/`From()`으로
   교정. 정본 §7.4의 "hex 문자열"·"수신측(zone spot)" 서술도 바로잡았다(수신측은 **Gateway**, zone
   spot은 §8.3대로 actor 인스턴스를 보관하므로 이 DTO를 소비하지 않는다).
2. **`ZW-F3-no-push` 체크가 위양성을 냈다.** 러너가 `"No current session binding exists for actor"`를
   부재 검사했는데, 이 문자열은 **사람 플레이어가 disconnect한 직후의 tick**에서도 (양성적으로) 뜬다 —
   spot이 그 사람을 아직 residents에 들고 있는 한 틱 동안 push를 시도하고, `PushToClients`의 try/catch가
   그것을 warn으로 잡고 넘긴다(설계상 정상, §8.9.2). 봇은 애초에 push 대상(`Humans(state)`)에서 제외되어
   이 오류를 내지 않는다. §11 ZW-F3의 의도는 **봇**(session 미bind actor)이므로, 체크를 봇 id 한정
   (`... actor 'bot-`)으로 좁혔다. 봇 제외가 깨지면 봇 id로 이 오류가 떠 여전히 잡히고, 사람 disconnect
   경합은 걸리지 않는다. **`RoutingId` 변경과 무관하다** — 걸린 actor는 정상 bind된 사람이고, 사람
   시나리오·교차 노드 transfer(`ZW-B2`, decode 경로)는 매 실행 통과했다.

**잔여 flake — stream message timeout(회당 최대 1건, 시나리오는 실행마다 다름).** 관측된 것:
`ZW-A4`(`Request timed out`, 1/3회), `ZW-C2`(`NodeStatusNotify` timeout, 이동 후 재검증 1회). 공통점은
**client가 특정 stream 메시지를 제한 시간 안에 못 받는다**는 것이고, 매번 다른 시나리오에서 하나만
난다 — 특정 시나리오의 로직 결함이 아니라 **기동/부하 타이밍**으로 보인다. 핵심 기능 경로(transfer·
경계 동기화·공지·점검)는 매 실행 그린이고 §12 마커도 그때마다 나온다. **원인(기동 경합 vs 부하)을
가려 잡기 전까지 관문 1을 "통과"로 적지 않는다.**

**작업 위치 이동(2026-07-15).** server + Shared + headless 시나리오 Client + run_sample.sh를
`shared_sample/zoneworld/dotnet/` → **`dotnet/samples/ZoneWorld/`**(다른 정본 샘플과 같은 자리)로
옮겼다. `shared_sample/zoneworld/`에는 브라우저 client만 남는다. 근거: headless 시나리오 client는
**언어별**이므로(§6.2) "shared"에 둘 이유가 없고, 공유되는 것은 브라우저 client 하나뿐이다. MSBuild
props는 ZoneWorld가 자기 것을 그대로 가져가 nearest-wins로 정본 샘플과 충돌하지 않는다. 이동 후
새 위치에서 빌드·핵심 경로 그린 확인. **정본 aggregate 등록은 보류**(§3).

#### 8.5.1 `dotnet` 회차 2 — codex 지적과 처리

codex 규약대로 **한 요청에 한 항목**씩 4건을 병렬로 냈다(ZoneSpot POSD / Ops 역할 / 시나리오 client /
Gateway 역할, 그리고 maintenance store 중복 판정).

**반영한 것 — 시나리오 client가 정본을 과소 단언하고 있었다.** 이것이 이 회차의 핵심이다. 시나리오
client는 "서버가 동작한다"의 정의이자 나머지 4개 언어가 베낄 템플릿이므로, 여기서 느슨한 단언은
4번 복제된다.

| 항목 | 정본이 요구한 것 | 있던 것 | 고친 것 |
|---|---|---|---|
| `ZW-C1` | `Registered=true` **그리고** `Connected=true` | `Registered`만 확인 | 두 노드 모두 두 플래그 확인 |
| `ZW-C2`·`ZW-C3` | 노드가 사라지면 `Registered=false`/`Connected=false` | 두 필드의 **기본값이 false**라 아무것도 안 하고도 통과 | **먼저 true를 확인**한 뒤 false로의 전이를 요구 |
| `ZW-B1` | 경계 공유 zone에만 나타나고 **대각선 zone에는 나타나지 않는다** | 양성 사례만 확인 | zone-se에 관측자를 두고 **음성 대조** 추가 |
| `ZW-E1` | 그 노드의 **두 zone 모두** 신규 진입 거부 | `zone-ne`만 행위 검증 | `zone-se` 진입 거부도 실제로 시도 |
| `ZW-E4` | `Zones=[zone-nw, zone-sw]` (정확한 목록) | `Contains` 2회 | 정렬 후 **완전 일치** |
| `ZW-A3` | 두 client가 **서로**를 본다, `Players` 전체가 정렬 | 한쪽만, 사람 부분수열만 | 양방향 + 전체 목록 정렬 |
| `ZW-D1` | 두 노드의 subscriber와 **모든 zone spot**이 수신, `AnnouncementId` 중복 없음 | 한 플레이어의 수신을 요구(정본은 best-effort라 명시) | client는 **중복 없음**만, subscriber·zone spot 수신은 **러너가 서버 로그로** 판정 |
| `ZW-F1` | 봇 **8마리** | `> 0` | 한 client는 8마리를 볼 수 없다(자기 zone + 인접 밴드뿐) → **러너가 로그로 8을 판정**, client는 자기 zone 봇의 이동을 확인 |
| `ZW-F3` | session 미bind actor 대상 **push 시도가 없다** | 사람이 공지를 받았다는 사실만 확인 | **부재는 부재로** — 러너가 "unbound actor push" 에러의 **비존재**를 판정 |
| `ZW-F4` | 점검 노드로 향하던 봇이 거부되면 반전 | 아무 X 순찰 봇의 X 감소면 통과 | **경계 직전(zone-nw, 다음 걸음이 경계를 넘는)** 봇으로 한정 |
| `ZW-C4` | tick handler 예외 주입 → `NodeAlertNotify` | 아무 노드의 경고면 통과 | 결함을 주입한 **그 노드**의 경고여야 함 |
| `Program` | — | 스위트 전체가 5분 예산을 공유 | **시나리오별 독립 예산** |

**이 강화가 실제 결함 셋을 드러냈다.**

1. **`Connected`가 한 번도 갱신되지 않았다.** framework는 채널의 client/server 소켓을 구분하려고
   routing id를 `<rid>\0<role>`로 파생시킨다(`ZLinkRoutingIdPolicy.Derive`). Ops는 socket event의
   rid를 `zn1`과 그대로 비교하고 있었으므로 **어떤 노드와도 일치하지 않았다.** `ZW-C3`는 `Connected`의
   기본값이 false여서 **거짓 통과**하고 있었다. → 구분자 앞부분으로 노드를 식별하도록 수정.
2. **공지가 자기 노드의 zone spot에 닿지 않아도 아무도 몰랐다.** `WorldAnnounceSubscriber`가
   `handle is null`을 조용히 `continue`했고, 예외도 삼켰다. 공지가 세상의 절반에만 도달해도 성공처럼
   보인다. → null·예외 모두 `LogError`로 드러내고, 한 zone의 실패가 다른 zone을 막지 않게 했다.
3. **만료된 경계 snapshot이 되살아날 수 있었다.** `ApplyBorderSnapshot`은 그 zone의 기존 항목과만
   tick을 비교하는데, 만료로 항목이 **사라지면** 비교 대상이 없어져 늦게 도착한 stale snapshot이
   죽은 노드의 플레이어를 다시 올린다. → 만료를 넘어 살아남는 **zone별 high-water tick**을 두었다.

**서버 쪽 반영.** ① node id ↔ routing id 매핑이 **세 곳**(Ops의 정·역방향, `Program.cs`의 채널 목록)에
흩어져 있던 것을 `ZoneTopology`의 **단일 노드 서술표**로 모았다. ② `OpsConsoleSession.OnDispatchAsync`의
도달 불가 분기 제거(`is var`는 항상 매치한다). ③ `ZoneNode`의 `IMaintenanceStorePort.WriteAsync`
제거 — 노드는 읽기만 한다(§8.4). ④ `BotTickMsg.Tick` 제거(수신측이 읽지 않는다). ⑤ 봇 이름 로스터를
`Shared`로 올렸다 — 시나리오 client와 브라우저 client가 같은 이름을 봐야 한다.

**반려한 것.**

| codex 지적 | 반려 사유 |
|---|---|
| 경계 snapshot 만료가 1 tick 늦다(`ExpireStaleSnapshots`를 notify **전에** 부르라) | 정본 §2.5가 "2. `ZoneStateNotify`를 만들어 push … 4. 만료된 snapshot 제거" **순서를 고정**한다. 앞당기면 "3 tick 미수신 시 제거"가 2 tick이 된다 |
| `Ops`/`ZoneNode`의 동명 `MaintenanceStoreRepository`를 통합하라 | codex 자신의 최종 판정도 **KEEP SEPARATE**다. 정본 §6이 역할별 port/adapter를 규정하고, 통합하면 좁은 역할별 port를 read/write 공용 표면으로 넓혀야 한다. 실제 중복은 Redis 쓰기 두 줄뿐 |
| Gateway: 같은 `PlayerId`로 재접속 시 이전 session binding이 해제되지 않는다 / 노드 유실 시 stale binding | 정본 §11에 없는 **framework 수준 과제**다(샘플이 우회할 문제가 아니다). §8.9.2에 후속 트랙으로 분리 |
| `ZoneState`/`ZoneTickUseCase`가 wire DTO(`PlayerView`·`ZoneStateNotify`)를 만든다 | 타당한 지적이나, 샘플의 교육적 가치와 4개 언어 포팅 비용을 감안해 **회차 3에서 판단**한다. 지금 바꾸면 5개 언어의 Domain 타입이 하나씩 더 늘어난다 |

#### 8.5.2 `dotnet` 회차 3 — codex 지적과 처리

codex 규약대로 **한 요청에 한 항목**씩 3건을 병렬로 냈다(actor 계층 / domain wire-DTO / bootstrap·config).

**이 회차의 핵심은 회차 1·2가 "그린"이라고 적어 둔 것이 사실이 아니었다는 것이다.** 회귀를 다시 돌려
베이스라인부터 확인하지 않았다면 아래 넷 중 어느 것도 드러나지 않았다.

| 드러난 것 | 실체 |
|---|---|
| **`ZW-B4`는 통과한 적이 없다** | "2회 연속 30/30 그린"이라고 적혀 있었으나 베이스라인 재실행에서 실패했다. 원인은 시나리오의 **틀린 단언**이다 — 노드 종료 뒤 "남은 플레이어가 **전부** zone-nw 소속"이라고 요구했는데, zone-nw의 인접 zone은 죽은 `zone-ne`뿐 아니라 **살아 있는 `zone-sw`**(zone-node-1)도 있다. Y축 순찰 봇이 `zone-sw` 밴드에 있는 tick에 걸리면 정당하게 목록에 남는다. 회차 2가 이 실패를 "순서 문제"로 진단하고 시나리오를 맨 앞으로 옮긴 것은 **원인 오귀속**이었다(타이밍이 바뀌어 두 번 통과했을 뿐이다) |
| **§12 마커는 하나도 발행되지 않았다** | 정본 §12가 요구하는 성공 로그 7종 중 `topology=ready` 외에는 아무도 찍지 않았다. 게다가 client가 **자기 배치만 통과해도** `zoneworld=completed`를 찍고 있었다 — 러너 주도 5개와 로그 판정 6개를 돌기도 전에 나오는 완주 마커다 |
| **`Ops`가 노드 목록을 들고 있었다** | `ZoneTopology.AllNodes`에 `zone-node-3`까지 하드코딩되어 있었고 `Ops/Program.cs`가 그것을 순회했다. 정본 §13·`ZW-D1`이 금지하는 바로 그것이고, **`ZW-D2`를 위양성으로 만든다** — "코드 변경 없이 노드를 추가한다"는데 그 노드가 이미 코드에 있었다 |
| **peer를 손으로 dial하고 있었다** | `ConnectRouter`/`ConnectPeerPub`/peer bridge endpoint. [spec 10 §5.2](../framework/spec/server/10-channel-topology.ko.md)는 **한 역할 안에서 자동 연결과 수동 연결을 섞지 말라**고 정한다(연결 집합의 소유자가 둘이 된다). peer rid를 endpoint와 함께 등록하는 형태는 **location store가 없는 구성**용인데 이 샘플은 store를 쓴다. 정본 §3의 "peer 자동 연결"과도, `ZoneWorldSettings`의 자기 주석과도 어긋났다. 정본 6종 중 교차 노드 transfer를 하는 Bingo는 peer를 dial하지 않는다 |

**반영한 것.**

1. **`ZW-B4` 단언 교정** — 죽은 zone(`zone-ne`)의 snapshot이 통째로 사라지는 것을 기다린 뒤 그 안에
   감시 대상이 없음을 단언한다. 만료를 먼저 기다리므로 "죽어 가는 노드의 마지막 snapshot"과 경합하지
   않는다. 살아 있는 `zone-sw` 밴드 플레이어는 남는 것이 정상이다(§4.1).
2. **§12 마커를 러너가 소유** — phase를 구성하는 시나리오가 전부 통과했을 때만 그 마커를 찍는다.
   client는 `zoneworld-batch=passed`만 찍는다(§8.4).
3. **`ZoneTopology`를 노드 서술자 하나로** — node id ↔ rid ↔ zones를 한 자리에 묶고, `zone-node-3`을
   **뺐다**. 이제 `Ops`가 아는 노드 집합은 정본 §2가 고정한 **zone 배치**뿐이고 그것은 노드 **지정**
   호출(§8.4)에만 쓴다. 발행 경로는 그것을 보지 않는다. zone 배치 밖의 `zone-node-3`은 `Ops`의
   코드·설정 어디에도 없는데 공지를 받는다 — `ZW-D2`가 이제 진짜를 증명한다.
4. **peer 수동 배선 삭제 — 시도했고, 되돌렸다.** ~~location store auto-connect만 쓴다~~
   **1회 그린을 보고 성공으로 적었다가 연속 실행에서 무너졌다.** 아래 §8.5.4가 정본이다.
5. **`zone-node-3`을 subscriber-only로**(정본 §11.1) — mesh·bridge·자기 channel·report·monitoring을
   전부 등록하지 않고 `zoneworld.broadcast` subscriber 하나만 둔다. 전용 `BroadcastProbeSubscriber`를
   둔 이유는 zone이 없는 노드에는 공지를 넘길 zone spot이 없기 때문이다.
6. **`BotSpawner`의 catch를 `ActorCreateFailed`로 좁혔다** — 모든 `ZLinkFrameworkException`을 삼키면
   봇이 하나도 안 만들어져도 `topology=ready`가 찍힌다. **함정:** conflict 뒤 `FindAsync`로 존재를
   재확인하는 방식은 **틀렸다.** conflict를 허용한 바로 그 stale한 location 뷰에 다시 묻는 것이라
   여전히 null이 오고, 재시작한 노드가 기동에 실패한다(실측으로 C2·C3·E5가 연쇄로 깨졌다).
   claim conflict 메시지 자체가 권위 있는 답이다.
7. **`PlayerActor.ZoneId`를 파생으로** — 좌표에서 유도한다. 권위가 자기 자신과 불일치할 수 없게 된다.
   `Restore`는 transfer로 온 zone과 좌표가 어긋나면 던진다.
8. **`TransferInAsync`가 빈 state에 던진다** — 조용히 기본 actor를 만들면 사람은 (0,0)으로 순간이동하고
   봇은 방향을 잃는데 샘플은 아무 일 없다는 듯 계속 돈다(§2.6).
9. **`ZoneWorld.Shared`에서 `Systems.Zlink` 패키지 참조 제거** — 쓰지 않았다. 없애면 "wire 계약에는
   framework 의존이 없다"가 **컴파일러가 강제하는 사실**이 되고, Domain이 그 레코드를 참조해도
   정본 §6의 "Domain은 ZLink 타입을 참조하지 않는다"를 깨지 않음이 보장된다.

**반복 실행이 드러낸 결함 둘.** 위를 반영하고 한 번 30/30 그린이 났지만, **한 번의 그린은 근거가
아니다.** 연속 실행에서 다시 깨졌고 아래 둘이 나왔다.

10. **공지가 노드 하나에 통째로 유실될 수 있었다(silent).** `WorldAnnounceSubscriber`가
    `SendToSpot(...).Submit(cancellationToken)`에 **publish handler의 취소 토큰**을 넘기고 있었다.
    이 send는 one-way라 handler보다 오래 사는데, handler가 반환하는 순간 그 토큰은 끝난다. 아직
    나가지 못한 send가 취소되면 **송신 dispatch 로그조차 남지 않고** 사라진다 — subscriber는
    "받았다"고 기록하고, 에러도 drop 로그도 없고, 그 노드의 **모든** zone spot이 공지를 못 받는다.
    한 실행에서 `zone-node-1`이 정확히 그렇게 됐다(`ZW-D1-spots` 실패). framework 자체 테스트도
    one-way는 `.Submit()`을 토큰 없이 부른다. → 토큰을 넘기지 않는다.
11. **`ZW-B4`가 transfer 직후의 과도기 상태에 걸렸다.** zone spot이 들고 있는 것은 좌표의 **사본**이고
    사본은 actor보다 한 턴 늦다(§2.1). transfer 직후 잠깐 east는 여전히 **출발 zone의 주민 사본**으로
    보인다 — 그 창에서는 east가 `zone-nw` 소속으로 화면에 있고 `zone-ne`는 아직 east를 보고하지 않았다.

    이 창 때문에 **만료를 한 가지 사실로 판정할 수 없다.**
    - "east가 사라졌다"만 보면: 위 창에서 잠깐 참이 된다(엉뚱한 east를 본 것이다).
    - "`zone-ne` 플레이어가 없다"만 보면: `zone-ne`의 첫 snapshot이 오기 전에 참이다.

    둘 중 어느 쪽으로 고쳐도 **노드를 죽이기도 전에**(러너는 20초 뒤에 죽인다) 즉시 매칭되어 실패했다.
    실제로 두 번 다 그렇게 실패했다. → **두 사실을 동시에** 요구한다 — east가 없고 `zone-ne`가 통째로
    없다. 그것만이 이 시나리오가 말하는 상태다.

> **교훈.** 이 둘은 어느 쪽도 한 번의 실행으로는 보이지 않는다. 회차 1·2가 "2회 연속 그린"을
> 근거로 적어 둔 것이 재실행에서 사실이 아니었던 이유이기도 하다. **관문 1의 재검증은 연속 실행으로
> 한다.**

**정본 반영.** 위 3·4와, 회차 1·2에서 구현이 정본을 앞질러 간 부분(§8.10)을 정본에 반영했다 —
§3.1(entry spot은 `ZoneNode` 소유), §4(peer 자동 연결), §5·§6(구조), §7.3(`EnterZoneMsg`/`EnterZoneRes`/
`EnterWorldReq`/`EnterWorldRes`, 삭제한 `UpdatePositionMsg`·`LeaveZoneMsg`), §8.2(발행 경로가 노드를
열거하지 않는다), §8.3(zone spot이 actor 인스턴스를 보관한다), §13. **정본과 구현이 이제 일치한다.**

**반려한 것.**

| codex 지적 | 반려 사유 |
|---|---|
| Domain(`ZoneState`·`ZoneTickUseCase`)이 wire DTO(`PlayerView` 등)를 만든다 — 회차 2에서 회차 3으로 미룬 항목 | **codex 자신의 최종 판정이 KEEP이다.** 정본 §6이 정한 규칙은 "Domain은 **ZLink 타입**을 참조하지 않는다"이고 `Shared.Contracts`는 framework 의존이 없는 순수 레코드다(위 9번으로 이제 구조적으로 보장된다). 현재 실패도, 표현하지 못하는 불변식도 없다. 5개 언어에 병렬 domain 타입과 매퍼를 하나씩 늘리는 대가가 미래의 컴파일 파급 하나를 막는 이득보다 크다 |
| 봇 방향 반전(`PlayerMovement.Reject`)이 Infrastructure에서 domain 정책을 결정한다 | codex도 "현재 런타임 실패가 아닌 구조 문제"로 분류했다. `PlayerActor`는 `IZLinkActor`를 구현하므로 Domain에 둘 수 없고, 좌표의 **권위**는 actor다(§2.1) — 자기 상태 전이를 자기가 소유하는 것이 맞다. 규칙은 한 곳에만 있고(`ReverseDirection`), 층을 나누면 5개 언어에 hop만 하나씩 는다 |

#### 8.5.4 **해결 — peer 자동 연결의 성공 오판과 owner 교체 결함**

회차 3에서 spec을 근거로 peer 수동 배선(`ConnectRouter`·`ConnectPeerPub`·peer bridge endpoint)을
삭제했다. 근거는 옳았다 — [spec 10 §5.2](../framework/spec/server/10-channel-topology.ko.md)는 한 역할
안에서 자동 연결과 수동 연결을 섞지 말라고 하고, peer rid를 endpoint와 함께 등록하는 형태는 location
store가 **없는** 구성용이며, 정본 §3도 "peer 자동 연결"이라고 적는다. 교차 노드 transfer를 하는 정본
샘플 Bingo도 peer를 dial하지 않는다.

삭제 직후에는 연속 실행에서 무너졌다. 원인은 수동 배선이 필요한 토폴로지가 아니라 framework의
자동 연결 상태 관리 두 곳에 있었다.

| 증상 | 실측 |
|---|---|
| `zone-node-1`이 **자기 spot으로 들어오는 것을 받지 못한다** | 공지를 fanout으로 받고(2회) 자기 zone spot으로 보내는데 **전달 0회, 에러 0회, drop 로그 0회** — 송신 dispatch 라인조차 남지 않는다. `zone-node-2`는 같은 코드로 2/2 성공 (`ZW-D1-spots` 실패) |
| 같은 노드가 인접 zone의 **경계 snapshot도 받지 못한다** | `zone-nw`(node-1)가 `zone-ne`(node-2)의 `ZoneBorderEvent`를 받지 못해 `ZW-B4`가 timeout |
| **보내는 것은 된다** | 같은 실행에서 node-1 → node-2 actor transfer는 정상(`ZW-B2`·`ZW-F2` 통과), east도 `zone-ne`에 정상 도착 |

이 비대칭은 연결 시도 실패를 성공으로 기록한 상태와 재시작 전 연결이 함께 남은 상태에서 발생했다.

**수정 1 — 실패를 성공으로 기록하지 않는다.** `ZLinkSpotPeerConnector`가 native connect의 `Busy`를
삼키고 성공으로 반환했다. reconciler는 endpoint 소유권을 기록한 뒤 다시 시도하지 않았으므로 실제
연결이 없는 상태가 유지됐다. 예외를 그대로 전달해 자동 연결 transaction이 rollback되고 다음 tick에
재시도하도록 고쳤다. router와 pub/sub 각각에 busy-once 회귀 테스트를 추가했다.

**수정 2 — 같은 endpoint의 owner 교체는 metadata만 갱신한다.** 노드가 같은 endpoint로 재시작하면
owner identity만 바뀐다. 이 경우 기존 연결을 끊고 다시 연결하면 transport 재연결과 겹쳐 이전 pipe와
새 pipe가 함께 남을 수 있었다. endpoint 또는 fingerprint가 바뀔 때만 socket handover를 수행하고,
owner만 바뀌면 reconciler의 소유권 metadata만 갱신하도록 계약과 구현을 맞췄다.

**검증.** 고정 sleep이나 수동 연결을 넣지 않았다. runner는 client가 실제 사전 조건을 확인한 뒤 남기는
`scenario ... armed` 마커를 기다린다. `ZW-B4`와 `ZW-E5`는 각각 5회 연속 통과했고,
`run_sample.sh all`은 30/30과 §12 마커 7종을 포함해 2회 연속 통과했다. ZoneWorld 코드에서
`ConnectRouter`·`ConnectPeerPub`·connection endpoint 수동 등록이 없고, 핸들러도 assembly scan과
handler group으로만 등록한다.

#### 8.5.6 **해결 — 지연 구독 수신 뒤 readiness signal이 남는 core 결함**

`ZW-B2`와 `ZW-D1` 뒤에 `ZW-B4`를 실행하면 구독 메시지가 queue에 있어도 후속 dispatch가 발생하지
않는 간헐 실패가 있었다. .NET framework는 native dispatch callback에서 작업을 직렬 queue에 넣고,
실제 구독 수신은 callback이 끝난 뒤 수행한다. 이 경로에서 사용하던 core의 중복 dequeue 구현은
메시지만 꺼내고 readiness signal 상태를 비우지 않아, 다음 메시지의 signal이 생략될 수 있었다.

중복 구현을 제거하고 signal을 함께 정리하는 core의 기존 구독 수신 함수 하나로 경로를 통합했다.
회귀 테스트는 callback 안에서 구독을 받지 않고 callback이 끝난 뒤 multipart API로 받은 다음,
readiness signal이 해제됐는지 직접 확인한다. `test_spot_dispatch_event` 9/9와
`test_spot_pubsub_scenario` 8/8이 통과했다.

수정한 `core/build` runtime을 포함한 `Systems.Zlink` 9.0.8 local package를 만들었고, package 안의
linux-x64 native 파일 3개가 해당 runtime과 같은 SHA-256인지 확인했다. 별도 native 경로 덮어쓰기 없이
문제 집중 조합을 5회 연속, 전체 30개 시나리오를 3회 연속 실행했다. 고정 sleep이나 수동 연결은
추가하지 않았다. .NET framework 단위 테스트 651/651과 Redis location 일반 테스트 40/40도
통과했다. 공통 client는 Vitest 4/4, 타입 검사, 독립 Playwright 2/2를 통과했고, 실제 server에서는
actor transfer·노드별 운영 명령·실제 노드 종료의 server push를 3/3 확인했다.

#### 8.5.7 POSD·DDD 재검토와 리팩토링

| 위험 신호 | 대안 | 선택 |
|---|---|---|
| spawn 노드의 로컬 actor manager가 transfer된 player identity까지 판단한다 | claim 예외 뒤 다시 조회 / 전역 identity를 소유한 actor directory에 ensure 위임 | `IZLinkActorDirectory.EnsureAsync`에 위임했다. 오류와 claim race를 handler가 알 필요가 없다. |
| `MaintenanceService`는 store로 전달만 하고, 저장→fanout→owner 명령→실패 의미 변환은 ZLink handler에 있다 | 얕은 service 삭제 후 handler에 유지 / application service를 깊게 만들고 transport를 port 아래로 이동 | `IWorldOperationsPort`가 topic·channel·timeout·framework 오류를 숨기고 application service가 use case 순서를 소유한다. |
| 좌표에서 zone을 고르는 규칙이 `ZoneTopology`와 `World`에 중복된다 | spawn zone 상수 고정 / shared 규칙 한 곳으로 통합 | `ZoneWorldSpec.ZoneOf` 하나로 통합해 spawn 좌표나 zone split 변경이 한 곳에서 끝난다. |

`ZW-B2`는 transfer 뒤 기존 연결이 계속 동작하는 것에 더해, 연결을 종료한 뒤 같은 `PlayerId`로
재접속해 동쪽 노드의 동일 actor와 좌표를 복원하고 다시 이동하는 것까지 확인한다. 연결이 없는 동안
actor의 좌표는 유지하고 zone의 resident projection만 제거되므로, 재접속 handler가 현재 zone
aggregate의 projection을 복원한다.

`ZoneSpot`은 resident·border snapshot·tick invariant를 함께 소유하는 aggregate라 분할하지 않았다.
시나리오 client도 정본 순서가 한 파일에서 읽히는 이점이 크므로 길이만으로 나누지 않았다. topic별
빈 border handler type은 현재 자동 scan 계약이 type당 topic 하나를 요구하므로 유지했다. 이를
합치려면 수동 등록이나 새 public surface가 필요해 호출자 복잡성이 늘어난다.

#### 8.5.3 `dotnet` 회차 4 — 봇 스폰의 오류 삼킴 (codex 수렴 게이트)

codex가 회차 3의 변경만 다시 보고 **NOT CONVERGED — 1건**을 냈다. 타당하고, 근거가 코드였다.

**지적.** `BotSpawner`의 catch를 `ZLinkFrameworkErrorKind.ActorCreateFailed`로 좁힌 것으로는 부족하다.
framework는 **location store 장애**에도 같은 kind를 던진다
(`ZLinkActorOwnershipCoordinator`의 `StoreFailure` → `ActorCreateFailed`, "location store is
unavailable"). 그러면 store가 죽어도 "이미 다른 노드가 소유 중"으로 읽고 넘어가, **봇이 하나도 없는
월드 위에서 `topology=ready`가 찍힌다**(정본 §2.7의 봇 8마리 위반).

**여기서 두 번 헛짚었다.** 회차 3에서 "conflict 뒤 존재를 재확인한다"고 넣은
`IZLinkActorManager.FindAsync`는 **로컬 전용**이다(`TryGetCreatedActorState` — 이 노드가 만든 actor만
안다). 재시작한 노드에는 그 봇이 없으므로 언제나 null이고, 그래서 rethrow가 되어 노드가 기동에
실패했다. 원격에 살아 있는 봇을 볼 수 있는 표면이 아니었던 것이다.

**해결 — framework에 이미 정확한 표면이 있었다.** `IZLinkActorDirectory`다.

| 표면 | 의미 |
|---|---|
| `IZLinkActorDirectory.FindAsync` | **로컬 → location store** 순으로 조회한다. 다른 노드에 살아 있는 봇을 **본다** |
| `IZLinkActorDirectory.EnsureAsync` | 없으면 만들고, claim 경쟁에 지면 directory를 **다시** 조회해 승자를 돌려주고, 그래도 놓을 수 없으면 `ActorCreateRejected`로 **던진다** |
| `IZLinkActorManager.FindAsync` | **로컬 전용.** 이 노드가 만든 actor만 안다 |
| `IZLinkActorManager.GetOrCreateAsync` | claim conflict를 **예외로** 던진다 |

`ZoneNodeBootstrap`을 directory 기반으로 바꿔 **try/catch를 통째로 없앴다.** 이제 이렇게 갈린다.

- 봇이 다른 노드에 살아 있다 → directory가 찾는다 → 스폰하지 않는다. (예외 없음)
- location store가 죽었다 → 만들 수 없다 → **던진다** → 노드가 기동에 실패한다. 봇 없는 월드에
  `topology=ready`가 찍히지 않는다.

**부수로 드러난 잠재 결함(미착수 트랙).** `EnsurePlayerActorHandler`도 `GetOrCreateAsync`를 쓴다.
transfer로 actor가 다른 노드에 가 있는 상태에서 **같은 `PlayerId`가 재입장**하면(정본 §2.4) 같은 claim
conflict를 맞는다. 정본 §11에 이 시나리오가 없어 드러나지 않았을 뿐이다. → §8.9.2에 합류시킨다.

- [x] 마지막 회차에서 codex가 **의미 있는 항목을 제시하지 않음**을 확인했다
- [x] 수렴 시점의 관문 1 전 항목(§8.4)이 여전히 통과한다

codex 규약을 지킨다 — **한 요청에 한 항목만**(병렬 요청은 가능, 묶기 금지), codex는 리뷰·진단만
맡고 반영은 이 세션이 직접 한다, 반려에는 사유를 남긴다.

### 8.9 현재 상태 (`dotnet` 관문 1 — 통과)

재현은 `dotnet/run_sample.sh all`.

**정본 §11의 25개 전항목 통과**(client 주도 19 + 런너 주도 4 + 로그 판정 2), `run_sample.sh all`
**2회 연속 그린**.

**cross-node actor transfer가 동작한다**(`ZW-B2` 통과) — 아래 바인딩 결함을 고친 결과다. 경계
동기화, 전 노드 공지(fanout), 점검 모드 격리, 노드 관찰·진단, 봇 8마리도 전부 통과한다.

#### 함정 — 유령 노드 프로세스가 코드 회귀로 위장한다

관문 2 회귀 확인 중 `ZW-B1`·`ZW-B2`·`ZW-B4`·`ZW-E3`(**교차 노드 전송 전부**)가 재현성 있게 실패했다.
원인은 코드가 아니라 **이전 실행에서 살아남은 `zone-node-2` 프로세스**였다. 런너가 `dotnet run`을
`kill`해도 그 래퍼가 띄운 서버 자식 프로세스는 살아남는데, 그 유령이 **routing id `zn2`를 계속 물고
있어** 새 실행의 mesh가 진짜 노드 대신 유령으로 라우팅했다. `handoff_completion`이 무한 재시도하고
(같은 flow가 수천 회) 전송이 통째로 사라지는데, 노드 내부 이동(`ZW-B3`)은 멀쩡하다 — 딱 "cross-node만
깨진 코드 회귀"처럼 보인다.

`run_sample.sh`를 두 군데 고쳐 재발을 막았다: **① 빌드 산출물을 직접 실행**해 기록하는 pid가 서버
자신이 되게 했고(`dotnet run` 래퍼 제거), **② 시작 전 유령 점검**을 넣어 남은 서버를 먼저 정리한다.

#### 해소 — **바인딩 결함**: multipart spot request가 EINVAL로 실패했다 (수정 완료)

**`bindings/dotnet`의 `Spot.RequestToSpot`은 2파트 이상 요청을 보낼 수 없었다.** cross-node actor
transfer의 admission 요청이 정확히 2파트(header+body)라 여기에 걸렸다.

| 항목 | 내용 |
|---|---|
| core 계약 | `core/src/api/spot/request_reply/service_spot_request_reply_part_submit.cpp:100` — `if ((part_flag_ == ZLINK_PART_FINAL) != (handler_ != NULL)) errno = EINVAL;` → **reply handler는 FINAL 파트에만 non-null이어야 한다.** staged sequence가 그 호출에서 request spec을 만들기 때문이다 |
| 결함 | `bindings/dotnet/.../Runtime/Service/Spot.Request.cs`(async)와 `Spot.RouterRequest.cs`(callback) **양쪽 모두** 모든 파트에 handler 포인터를 넘겼다 → 2파트 요청의 첫 파트(`More` + handler)가 EINVAL |
| 왜 안 드러났나 | 단일 파트는 `SubmitOwnedSinglePart`(항상 `Final`)로 빠져 통과한다. 그리고 `SpotNodeRouterTarget` 경로(spot mesh router로 원격 spot에 요청)를 밟는 샘플이 없었다 — 정본 6종은 **entry spot → user spot** join만 하고, ZoneWorld의 **user spot → user spot cross-node** join이 이 경로를 처음 밟았다 |
| 수정 | 두 경로 모두 `partFlag == Final`일 때만 handler를 넘기도록 고쳤다. 로컬 NuGet 패키지 재빌드(`scripts/local-package/build-wsl.sh dotnet`) 후 `ZW-B2` 통과 확인 |
| **남은 일** | **다른 4개 언어 바인딩(java·kotlin·node·cpp)에 동일 결함이 있는지 대조해야 한다.** core 계약이 언어 중립이므로 같은 실수가 반복됐을 가능성이 높다. 별도 트랙으로 분리한다 |

#### 해소 — **같은 노드의 두 spot 사이 양방향 로컬 join이 ABBA 데드락에 빠졌다** (framework 수정 완료)

**증상.** 점검 모드를 켠 노드에서 봇들의 `JoinSpot`이 30초 hang하고, 그 spot들의 tick이 멈추며 이후
모든 join이 `phase=received`에서 죽었다. `ZW-E3`가 스위트에서 실패한 원인이다.

**근본 원인 (계측으로 확정).** 로컬 join은 **두 spot의 직렬 큐를 겹쳐 잡는다.**

```
ZLinkSpotActivationActors.JoinActorAsync(:47)
  └─ ExecuteSerializedAsync(...)                    <- (A) 대상 spot 큐를 잡는다
       └─ InvokeActorJoinAsync (admission)
       └─ CommitActorJoinCoreAsync(:314)
            └─ JoinActorToSpotCoreAsync(:333)
                 └─ previousActivation.NotifyActorLeftAfterJoinCommitAsync(:300)
                      └─ ExecuteSerializedAsync(...) <- (B) 소스 spot 큐를 잡는다  ★중첩★
            └─ InvokeActorLifecycleAsync(:349)       (대상 OnJoinedActor)
```

`(A)` 안에서 `(B)`를 await하므로, 반대 방향 join(`zone-nw`→`zone-sw`와 `zone-sw`→`zone-nw`)이 동시에
진행되면 **서로 상대의 큐를 기다리는 ABBA 데드락**이 된다. `:303`의 ambient 가드는 소스와 대상이
**같은** spot일 때만 중첩을 피해 준다.

계측 로그가 그대로 보여 준다.

```
E3DIAG join-enter actor=bot-sw-y target=zone-nw            <- zone-sw의 봇이 zone-nw로
E3DIAG facade    actor=bot-sw-y spot=zone-nw local=True     (완료되지 않음)
E3DIAG join-enter actor=bot-nw-y target=zone-sw            <- zone-nw의 봇이 zone-sw로
E3DIAG facade    actor=bot-nw-y spot=zone-sw local=True     (완료되지 않음)
```

**점검 모드가 방아쇠인 이유.** 점검 모드가 봇들을 경계에서 튕겨 내며 주기를 맞춰 놓아, 두 Y축 순찰
봇이 **같은 tick에 서로를 향해** 경계를 넘게 만든다. 점검 모드가 없으면 두 봇이 다른 시점에 넘어가
데드락 창이 열리지 않는다.

**수정.** `ZLinkFrameworkActorFacade.JoinActorAsync`에 **노드 단위 로컬 join gate**(`SemaphoreSlim(1,1)`)를
두어 로컬 join을 직렬화했다. 두 번째 join은 첫 번째가 **두 큐를 모두 놓은 뒤**에야 시작하므로 순환이
성립하지 않는다.

**검증.** `run_sample.sh ZW-E6,ZW-E3` 통과, `run_sample.sh all` **18/18 전량 통과**. 기존 정본 샘플
회귀 없음(TicTacToe·Bingo·SupportChat 정상 종료).

**남은 과제 (별도 트랙).** 이 수정은 순환을 없애지만 **중첩 구조 자체는 남는다**(한 join이 여전히 두
큐를 겹쳐 잡는다). 근본 수정은 join을 **중첩 없는 work item 3개**로 분리하는 것이다 — admission(대상
큐) → 소스 `OnLeaveActor`(소스 큐) → 대상 `OnJoinedActor`(대상 큐). spec 23 §3.3의 순서를 지키면서
큐를 겹쳐 잡지 않는다. **다른 4개 언어 framework에도 같은 중첩이 있는지 대조해야 한다.**

**왜 이 경로가 처음 밟히나.** 정본 6종에는 **같은 노드의 두 user spot 사이를 actor가 오가는** 시나리오가
없다. ZoneWorld의 노드 내부 zone 이동(§2.6)이 이 경로를 처음 밟았고, 봇(§2.7)이 그것을 **양방향
동시에** 일으켰다.

#### 해소 — bound session route가 첫 relay 전에 전파되지 않았다 (수정 완료)

### 8.9.1 **필수 후속 트랙 — 발견한 결함을 회귀망에 넣는다**

**ZoneWorld 샘플은 회귀를 지키는 자리가 아니다.** 샘플은 "이 기능이 실제로 이렇게 쓰인다"를 보여
주는 자리이고, 결함을 다시 잡아 주는 것은 contract test와 공통 e2e다. 이번에 찾은 결함 3건은
**정본 6종과 기존 e2e가 전부 통과하는 상태에서도 살아 있었다** — 커버리지에 구멍이 있었다는 뜻이므로,
그 구멍을 메우는 것이 이 샘플 작업의 산출물에 포함되어야 한다.

| 결함 | 넣을 곳 | 시나리오 | 왜 거기인가 | 상태 |
|---|---|---|---|---|
| **바인딩 multipart spot request가 EINVAL** | **바인딩 contract test**(단위) | `Spot.RequestToSpot`(및 router 경로)에 **2파트 이상**을 제출해 성공하는지 본다 | 순수 함수적이라 단위로 결정적이다. **5개 언어 바인딩 전부**에 같은 테스트가 필요하다 | **dotnet·cpp 완료**(아래) |
| **원격 actor에 bind한 session route가 첫 relay 전에 전파되지 않는다** | **공통 e2e** (config-2 SpotService 또는 31 Session Actor Dispatch) | **처음부터 원격인** actor에 session을 bind하고, **client가 아무것도 보내지 않은 상태**에서 server push가 도달하는지 본다 | 노드 분리가 본질이라 단위로 만들 수 없다 | 미착수 |
| **같은 노드 두 user spot 사이 양방향 로컬 join이 ABBA 데드락** | **공통 e2e** (config-10 SpotActorTransfer) | 같은 노드의 두 user spot 사이에서 **두 actor가 서로 반대 방향으로 동시에** join한다 | 동시성·타이밍이 본질이라 단위로는 재현이 불안정하다 | 미착수 |

**공통 e2e 문서(`framework/doc/framework/common/e2e/config-*.ko.md`)에 시나리오로 등록해야** 5개 언어가
전부 구현하고, java·kotlin·node·cpp에도 같은 결함이 있는지 자동으로 드러난다.

#### 5개 언어 바인딩 대조 결과 (multipart spot request)

core 계약은 **reply handler는 FINAL 파트에만 non-null**이다
(`service_spot_request_reply_part_submit.cpp`의 `validate_request_part_handler` —
`(part_flag == FINAL) != (handler != NULL)` → `EINVAL`). 전 언어를 대조했다.

| 언어 | 판정 | 처리 |
|---|---|---|
| `dotnet` | **결함** — 모든 파트에 handler를 넘겼다 | 수정 완료 + `tests/Zlink.Tests/test_spot_multipart_request.cs` 추가. **수정을 되돌리면 2-파트 테스트만 실패하고 1-파트는 통과**함을 실측해, 테스트가 이 결함을 정확히 겨눈다는 것을 확인했다. 바인딩 전체 180/180 그린 |
| `cpp` | **결함** — `request_submitter.hpp`의 multipart 경로가 `is_final_`을 **버리고**(`(void) is_final_`) 모든 파트에 trampoline을 넘겼다. spot 경로는 EINVAL, socket 경로는 `is_final_ = callback != nullptr`가 **항상 참**이 되어 의미를 잃었다 | 수정 완료. **기존 contract test가 틀린 계약을 굳혀 놨다** — 이름부터 `..._attaches_reply_handler_to_every_part`였다. 정본 계약대로 정정. contract 10/10 그린 |
| `java`(`kotlin` 공유) | 정상 — `last ? handler : MemorySegment.NULL` | 동등 테스트 추가 필요 |
| `node` | 정상 — `is_final ? handler : NULL` | 동등 테스트 추가 필요 |

**교훈: 테스트가 결함을 정본으로 굳힐 수 있다.** cpp의 contract test는 "모든 파트에 handler를 붙인다"를
**성공 기준으로** 단언하고 있었다. 코드에서 계약을 역으로 읽어 테스트를 쓰면 이렇게 된다 — 계약은
core spec에서 읽어야 한다.

### 8.9.2 후속 트랙 — Gateway session binding (framework 수준)

codex Gateway 리뷰가 정본 §11 밖의 framework 과제 둘을 지적했다. 샘플에서 우회할 문제가 아니므로
분리해 기록한다. **정본 시나리오는 이 둘을 다루지 않으므로 관문 1·2의 통과 여부와 무관하다.**

| 항목 | 내용 |
|---|---|
| 같은 actor에 대한 **두 번째 session bind가 첫 번째를 해제하지 않는다** | 같은 `PlayerId`로 재접속하면 이전 session의 binding이 남아, 두 session에서 온 packet이 actor의 push 목적지를 번갈아 덮어쓴다. 샘플이 `PlayerId → session` map을 따로 들면 framework의 binding 상태를 **중복**하게 되므로, framework가 actor당 현재 session 하나를 강제해야 한다 |
| actor의 **노드가 사라지면 session이 stale binding에 묶인 채 남는다** | 이후 packet이 죽은 `ActorRef`를 계속 쓴다. relay가 결국 예외를 던지지만 `MoveMsg`는 one-way라 브라우저는 응답도 끊김도 받지 못한 채 얼어붙는다. 샘플 수준의 완화는 relay 실패 시 session을 닫는 것이고, 근본 수정은 framework가 소유권 상실 시 session handle을 무효화하는 것이다 |

transfer 이후 같은 `PlayerId` 재입장 문제는 회차 8에서 해소했다. spawn 노드는 actor directory로
전역 identity를 해석하고, 현재 zone은 새 session의 첫 `JoinWorldReq`에서 resident projection을
복원한다. 강화한 `ZW-B2`가 transfer→연결 종료→같은 id 재접속→추가 이동을 실제로 검증한다.

### 8.10 구현하며 드러난 정본 설계 결함 (수정해 반영함)

| 항목 | 결함 | 수정 |
|---|---|---|
| §7.3 `EnterZoneMsg` | `ActorRef`를 join payload에 실으면 **cross-node transfer 후 stale**이다. payload는 출발 노드에서 만들어지고, actor는 자기 `ActorRef`를 알지 못한다(`IZLinkActor`는 `ActorId`만 노출) | zone spot이 actor 인스턴스를 보관해 `BoundSession`으로 push. 봇만 actor directory로 ref 해석. §8.3 재작성 |
| §7.3 `LeaveZoneMsg` | zone 이동은 **반드시 `JoinSpot`**이어야 한다(그것이 transfer를 일으키는 유일한 메커니즘). 그러면 이전 spot 퇴장은 framework의 `OnLeaveActor`가 알려 주므로 이 메시지는 죽은 계약 | 삭제 |
| §7.3 `EnterZoneMsg` | §2.3의 "노드 내부 이동은 허용, 진입만 차단"을 target spot의 `OnActorJoin`이 판정해야 하는데 framework는 source node를 admission 콜백에 넘기지 않는다(spec 23 §3.1) | `FromNodeId` 필드 추가 |
| §4 `zoneworld.actors` | Req/Res 메시지가 §7에 정의되지 않았고, 어느 노드에 도달해야 하는지도 불명확했다 | `EnsurePlayerActorReq/Res` 확정. **입장 zone 호스팅 노드만 서빙**(그 노드가 admission 권위) |
| §4·§5·§6 `Gateway` | Gateway에 entry spot을 두었으나, player actor는 `ZoneNode`에 산다. Gateway는 spot mesh에 **참여만** 하면 원격 actor에 session을 bind할 수 있다 | entry spot은 `ZoneNode`가 소유(`ZoneEntrySpot`) |
| §11 `ZW-A5` | 삭제한 `UpdatePositionMsg`를 여전히 성공 기준으로 참조했다 | "zone spot의 좌표 사본이 갱신된다"로 재작성 |
| §11 `ZW-F1` | "client가 `Players`에서 봇 **8마리**를 본다"는 **관측 불가능**하다 — client는 자기 zone과 인접 zone 밴드만 받는다(§4.1). 대각선 zone의 봇은 어떤 client에도 보이지 않는다 | client는 자기 zone 봇의 이동을 확인하고, **8마리는 서버 로그로** 판정하도록 성공 기준 재작성 |
| §11 `ZW-C1`·`ZW-C2`·`ZW-C3` | `Registered`·`Connected`는 **기본값이 false**다. "false를 기다린다"로 쓰면 아무 일도 하지 않고 통과한다 | **먼저 true를 확인한 뒤 전이를 본다**로 성공 기준 명시. 이 강화가 `Connected` 배선이 죽어 있던 실제 결함을 드러냈다 |
| §11 `ZW-F3` | "push 시도가 없다"는 **부재**라 client가 관측할 수 없다 | 서버 로그 판정임을 성공 기준에 명시 |
| §2.4 만료 임계 | 만료 임계 3 tick(300ms)이 publish 주기 100ms와 같은 크기라, 부하로 인한 도착 지터만으로도 인접 zone 플레이어가 **깜빡인다**(사라졌다 돌아온다) | 정본 값은 유지한다(전 언어가 같은 값을 써야 한다). 대신 만료를 넘어 살아남는 **zone별 high-water tick**을 두어 늦게 도착한 stale snapshot이 죽은 노드의 플레이어를 되살리지 못하게 했다. 시나리오는 "3 tick 뒤 제거"만 단언하고 "다시 나타나지 않는다"는 단언하지 않는다 — 정본이 그것까지 약속하지 않는다 |

### 8.6 Phase 2 — 공통 브라우저 client

**빌드·스택** (정본 §9.2)

- [x] Vite
- [x] Preact + `@preact/signals`
- [x] Canvas 2D 월드 렌더
- [x] `@zlink-systems/stream-connector` (브라우저 entrypoint)
- [x] Vitest (domain 단위 테스트)
- [x] Playwright headless Chromium 러너 — 공통 client와 `dotnet` 러너 배선 및 실서버 3/3 완료. 나머지 4개 언어는 server 구현 뒤 배선한다

**FSD 계층** (정본 §9.3) — 의존 방향은 `app` → `pages` → `widgets` → `features` → `entities` → `shared`

- [x] `app/` — `game.tsx`, `ops.tsx`
- [x] `pages/` — `game`, `ops`
- [x] `widgets/` — `world-canvas`, `game-hud`, `node-table`, `alert-list`
- [x] `features/` — `join-world`, `move-player`, `announce-world`, `set-maintenance`, `node-diagnostics`, `watch-nodes`
- [x] `entities/` — `player`, `zone`, `node`, `announcement`
- [x] `shared/` — `api`, `config`, `lib`, `ui`
- [x] push 전용 메시지는 feature가 아니라 해당 `entities`의 model이 적용한다
- [x] `entities/zone`이 서버와 같은 월드 규칙(§2)을 구현하고 브라우저 없이 단위 테스트된다

**게임 화면** (정본 §10.1)

- [x] 격자 100×100 + zone 경계선(X=50, Y=50)
- [x] 경계 밴드 시각 구분
- [x] 인접 zone 플레이어를 같은 zone 플레이어와 다른 표시로 구분
- [x] 봇(`IsBot=true`)을 사람과 다른 표시로 구분
- [x] 방향키 → `MoveMsg`, **좌표를 client가 먼저 바꾸지 않는다**
- [x] 현재 `ZoneId`·`NodeId` 상시 표시 + `ZoneChangedNotify` 갱신
- [x] `Transferred=true` 시각 표시
- [x] WebSocket 연결 상태 표시(zone 이동 중 유지됨을 확인 가능)
- [x] `WorldAnnounceNotify` 표시 + `AnnouncementId` 중복 제거
- [x] `MoveRejectedNotify`의 `Reason` 표시

**관제 화면** (정본 §10.2)

- [x] 노드 목록 — `Registered`·`Connected`·`Maintenance`·`Zones`·`PlayerCount`
- [x] 노드 종료 시 `Registered=false`
- [x] 연결 단절 시 `Connected=false`
- [x] `NodeAlertNotify` 시간순 표시
- [x] 공지 발행 입력 + 버튼
- [x] **노드별** 점검 전환 버튼
- [x] **노드별** 진단 버튼
- [x] 격리 확인 — 한 노드만 전환 시 다른 노드 무변화
- [x] **polling하지 않는다** — `NodeStatusNotify` push로만 갱신

**UI 품질** (정본 §10.0)

- [x] 깔끔함 · 정보 위계 · 상태를 색+형태로 구분(색만으로 구분하지 않음)
- [x] 변화 전이 150~250ms · 일관된 시각 언어 · 여백/정렬 · WCAG AA 명암비 · 고정폭 수치
- [x] tick(100ms) 갱신에도 끊기지 않음(Canvas는 `requestAnimationFrame`)

### 8.7 Phase 3 — 교차 검증

**같은 client 하나**로 5개 언어 server 전부에 대해 정본 §12 smoke를 재실행한다. 언어별로 따로
통과한 것과 하나의 client가 전부에 붙는 것은 다른 검증이다.

- [x] client × `dotnet` server — actor transfer 중 연결 유지, 노드별 점검·진단, 실제 `zone-node-2` 종료 뒤 server push를 3/3 확인
- [ ] client × `java` server
- [ ] client × `kotlin` server
- [ ] client × `node` server
- [ ] client × `cpp` server
- [ ] 5개 조합 모두에서 client 코드가 **동일**하다(언어별 분기 없음)

### 8.8 완료 기준 최종 확인

정본 §13이다. 전부 충족해야 샘플이 완료다.

- [ ] 브라우저 client **하나**가 5개 언어 server **전부**에 연결된다
- [ ] client에는 `Gateway`와 `Ops` 주소만 설정한다. zone 노드 주소가 노출되지 않는다
- [ ] client의 `state`는 서버 push로만 바뀐다. 입력이 `state`를 직접 바꾸지 않는다
- [ ] 두 화면이 §10.0의 UI 품질 요구를 만족하고 같은 시각 언어를 쓴다
- [ ] 노드 간 zone 이동에서 client의 WebSocket 연결이 끊기지 않는다
- [ ] 노드 내부 zone 이동에서는 actor transfer가 일어나지 않는다
- [ ] 경계 동기화는 인접 zone별 topic이며 대각선 zone에는 전달되지 않는다
- [ ] 전 노드 공지는 channel fanout이고 `Ops` 설정에 노드 목록이 없다. best-effort이며 중복은
      client가 `AnnouncementId`로 제거한다
- [ ] 노드 지정(점검·진단)은 owner 일관 channel이며 그 노드에만 도달한다. 점검 모드는 그 노드의
      모든 zone에 적용된다
- [ ] 점검 모드가 노드 재시작 후 maintenance store에서 복원된다
- [ ] 관제 화면의 노드 상태는 runtime event에서 온다(polling 아님). 원격 spot event는 `ZoneNode`가
      명시적으로 보고한다
- [ ] RouteMesh channel은 **spot bridge 용도로만** 등록하고 애플리케이션 노드 지정에 쓰지 않는다
- [ ] `PlayerId`·`ZoneId`·`NodeId`는 명시적 domain id이며 routing id hex를 client에 노출하지 않는다
- [ ] 기존 회귀(다른 샘플·E2E·contract test)를 깨지 않는다
- [ ] 새 public API를 추가하지 않았다(추가가 필요했다면 draft로 분리해 보고했다)

## 9. 작업 규칙

- **착수 전 구현 계획을 제시하고 합의한다.** 대형 구조 변경과 대형 문서 정비는 편집 전 협의가
  필요하다.
- 기존 회귀(다른 샘플, E2E, contract test)를 깨지 않는다.
- 실측 없이 완료를 보고하지 않는다. 통과한 `ZW-*` ID와 실행 로그를 근거로 보고한다.
- 커밋은 지시가 있을 때만 한다. 기본 작업 브랜치는 `main`이다.
