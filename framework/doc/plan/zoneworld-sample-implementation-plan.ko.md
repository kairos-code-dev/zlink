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
| 공개 계약 | [`spec/`](../framework/common/spec/README.ko.md)의 공통 spec과 언어별 spec |
| 저장소 규약 | `AGENTS.md` — 특히 "Framework public contract parity" |

샘플은 공개 계약의 근거가 아니다. **새 public API를 만들지 않는다.** 구현 중 표면이 부족해
보이면 그것이 진짜 능력 격차인지 언어별 관용 표현 차이인지 먼저 판정한다. spec이 뒷받침하지
않는 기능은 public API로 추가하지 말고 별도 draft로 분리해 보고한다.

## 3. 작업 위치

정본 §0.2가 정한 배치를 따른다. client 하나를 모든 언어 server가 공유하므로, 언어별 디렉터리가
아니라 아래 한곳에 모은다.

```text
framework/languages/shared_sample/zoneworld/
  client/     TypeScript 브라우저 client — 5개 언어 server가 공유한다 (Phase 2)
  dotnet/     Shared/ · Server/ · Client/ (언어별 시나리오 client) · run_sample.sh
  java/  kotlin/  node/  cpp/     같은 구성
```

**언어별 디렉터리의 `Client/`는 그 언어의 시나리오 client다**(§6.2). 최상위 `client/`의 브라우저
client와 다른 것이다 — 전자는 server 동작을 headless로 검증하고, 후자는 화면을 보여 준다.

client를 언어별 디렉터리에 복제하지 않는다. server 디렉터리 구조는 정본 §6, client 구조는 §9.3을
따른다. 기존 정본 6종(`framework/languages/<lang>/samples/`)은 건드리지 않는다.

## 4. 이미 확인된 선행 조건 (재조사 불필요)

- **browser connector 준비 완료.** `@zlink-systems/stream-connector`는 browser-only ESM package
  root이며 public transport는 `WebSocket`·`WebSocketSecure`만 남았다. 실제 Chromium에서 `ws`/`wss`
  request/reply, push, reconnect, drain, close를 검증했다. 근거는
  [`spec/90-implementation-gap.ko.md`](../framework/common/spec/90-implementation-gap.ko.md) §4.10
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
| Phase 1 — `dotnet` | **기준** server + 시나리오 client + 3관문(§8.3·§8.4·§8.5) | **진행 중 — 관문 1 미통과.** 구현한 18개 중 **17개 실측 통과.** cross-node transfer 포함 전 기능 동작. 남은 1개(`ZW-E3`)는 최소 재현 확보(§8.9) |
| Phase 1 — `java` | `dotnet` 포팅 + 시나리오 client + 3관문 | 미착수 |
| Phase 1 — `kotlin` | `dotnet` 포팅 + 시나리오 client + 3관문 | 미착수 |
| Phase 1 — `node` | `dotnet` 포팅 + 시나리오 client + 3관문 | 미착수 |
| Phase 1 — `cpp` | `dotnet` 포팅 + 시나리오 client + 3관문 | 미착수 |
| Phase 2 | 공통 브라우저 client(§8.6) | 미착수 (Phase 1 전 언어 완료 후) |
| Phase 3 | 교차 검증(§8.7) | 미착수 |
| 완료 | 최종 확인(§8.8) | 미착수 |

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
- [x] `BotTickMsg`
- [x] `ApplyNodeMaintenanceReq` / `ApplyNodeMaintenanceRes` (owner 일관 channel)
- [x] `GetNodeDiagnosticsReq` / `GetNodeDiagnosticsRes` (owner 일관 channel)
- [x] `ReportSpotEventMsg` (이벤트 시)
- [x] `ReportNodeStatusMsg` (1초 주기)
- [x] `ZoneBorderEvent` (spot pub/sub)
- [x] `EnterZoneMsg` (`ActorRef` 동반)
- [x] `UpdatePositionMsg`
- [x] `LeaveZoneMsg`
- [x] `ActorRefWire` — `NodeRid`, `ActorId`, `Generation` (정본 §7.4)

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
| **Gateway** — `PlayerSession` | [x] | [ ] | [ ] | [ ] | [ ] |
| **Gateway** — `GatewayEntrySpot` | [x] | [ ] | [ ] | [ ] | [ ] |
| **Gateway** — `JoinWorldHandler` | [x] | [ ] | [ ] | [ ] | [ ] |
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
| **`zone-node-3`** — subscriber만(정본 §11.1, `ZW-D2` 전용) | [ ] | [ ] | [ ] | [ ] | [ ] |

### 8.4 관문 1 — 기능 완료 (그 언어의 시나리오 client로 판정)

정본 §11의 전 항목이다. **하나라도 미통과면 그 언어는 관문 2로 넘어가지 않는다.** 판정은 §8.2의
headless 시나리오 client로 한다. 브라우저는 이 관문에 등장하지 않는다.

**`dotnet` 현황: 25개 중 17개 통과, 1개 실패(`ZW-E3`, §8.9), 7개 미구현.**

미구현 7개는 모두 **러너가 토폴로지를 조작해야** 하는 것들이라 시나리오 client만으로는 판정할 수
없다. 러너에 아래 능력을 추가해야 한다.

| ID | 필요한 러너 능력 |
|---|---|
| `ZW-B4` | 인접 zone의 노드를 **종료**하고 3 tick 뒤 그 zone 플레이어가 사라지는지 본다 |
| `ZW-C2` | `zone-node-2`를 **종료**하고 `NodeStatusNotify(Registered=false)`를 본다 |
| `ZW-C3` | `Ops`↔노드 **연결을 끊고** `Connected=false`를 본다 |
| `ZW-C4` | zone spot tick handler에 **예외를 주입**하고 `NodeAlertNotify(TimerHandlerFailed)`를 본다 |
| `ZW-D2` | **세 번째 노드**(`zone-node-3`, zone 없이 subscriber만)를 추가 실행한다(§11.1) |
| `ZW-E5` | 점검 전환 후 `zone-node-2`를 **재시작**하고 maintenance store에서 복원되는지 본다 |
| `ZW-F2` | **client를 하나도 붙이지 않은 상태**에서 봇의 노드 간 transfer를 서버 로그로 확인한다 |

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
| `ZW-B4` | 경계 snapshot 만료(3 tick) | [ ] | [ ] | [ ] | [ ] | [ ] |
| `ZW-C1` | 노드 관찰 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-C2` | 노드 종료 → `Registered=false` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `ZW-C3` | 연결 단절 → `Connected=false` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `ZW-C4` | spot 이벤트 보고(`TimerHandlerFailed`) | [ ] | [ ] | [ ] | [ ] | [ ] |
| `ZW-D1` | 전 노드 공지(발행자에 노드 목록 없음) | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-D2` | 노드 추가 시 공지(`zone-node-3`) | [ ] | [ ] | [ ] | [ ] | [ ] |
| `ZW-E1` | 노드 지정 점검(격리 확인) | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-E2` | 점검 중 기존 플레이어 이동 허용 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-E3` | 점검 중 이탈 허용 | [ ] | [ ] | [ ] | [ ] | [ ] |
| `ZW-E4` | 노드 진단 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-E5` | 재시작 복원(maintenance store) | [ ] | [ ] | [ ] | [ ] | [ ] |
| `ZW-E6` | 점검 중 신규 입장 거부 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-F1` | 봇 8마리 존재·이동 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-F2` | 봇 노드 간 transfer(client 없이) | [ ] | [ ] | [ ] | [ ] | [ ] |
| `ZW-F3` | 봇에 push하지 않음 | [x] | [ ] | [ ] | [ ] | [ ] |
| `ZW-F4` | 봇 방향 반전 | [x] | [ ] | [ ] | [ ] | [ ] |

**smoke 마커** (정본 §12) — 언어별 runner 로그가 아래를 포함해야 한다.

| 마커 | `dotnet` | `java` | `kotlin` | `node` | `cpp` |
|---|:--:|:--:|:--:|:--:|:--:|
| `topology=ready` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `zoneworld-transfer=completed` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `zoneworld-border-sync=completed` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `zoneworld-ops-observe=completed` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `zoneworld-ops-announce=completed` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `zoneworld-ops-maintenance=completed` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `zoneworld=completed` | [ ] | [ ] | [ ] | [ ] | [ ] |

### 8.5 관문 2·3 — 리팩토링 반복과 codex 수렴

**언어마다 아래 표를 새로 채운다.** 회차는 codex가 더 이상 의미 있는 리팩토링 요소를 제시하지
않을 때까지 늘어난다. 회차 수를 미리 정하지 않는다.

`<언어>` 리팩토링 회차 기록:

| 회차 | 반영한 항목 | 관문 1 회귀 재실행 | codex가 제시한 항목 | 처리(반영/반려+사유) | 수렴 |
|---|---|:--:|---|---|:--:|
| 1 | | [ ] | | | 아니오 |
| 2 | | [ ] | | | |

- [ ] 마지막 회차에서 codex가 **의미 있는 항목을 제시하지 않음**을 확인했다
- [ ] 수렴 시점의 관문 1 전 항목(§8.4)이 여전히 통과한다

codex 규약을 지킨다 — **한 요청에 한 항목만**(병렬 요청은 가능, 묶기 금지), codex는 리뷰·진단만
맡고 반영은 이 세션이 직접 한다, 반려에는 사유를 남긴다.

### 8.9 현재 상태 (`dotnet` 관문 1)

재현은 `dotnet/run_sample.sh all`.

**구현한 18개 중 16개 통과.** 남은 2개(`ZW-E3`·`ZW-F4`)는 시나리오 client 쪽 문제이며, 서버 결함이
아니다.

| 통과 | `ZW-A1` `ZW-A2` `ZW-A3` `ZW-A4` `ZW-A5` `ZW-B1` `ZW-B2` `ZW-B3` `ZW-C1` `ZW-D1` `ZW-E1` `ZW-E2` `ZW-E4` `ZW-E6` `ZW-F1` `ZW-F3` `ZW-F4` |
|---|---|
| 미해결 | `ZW-E3` — **거절된 cross-node join이 대상 노드를 막는 framework 결함 의심**(아래) |

**cross-node actor transfer가 동작한다**(`ZW-B2` 통과) — 아래 바인딩 결함을 고친 결과다. 경계
동기화, 전 노드 공지(fanout), 점검 모드 격리, 노드 관찰·진단, 봇 8마리도 전부 통과한다.

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

#### 남은 1개 — **거절된 cross-node join이 대상 노드를 막는다** (framework 결함, 최소 재현 확보)

**한 문장 요약: 봇이 도는 상태에서 노드를 점검 모드로 켜 두기만 해도 그 노드의 join 경로가
영구히 막힌다.** `ZW-E3`가 스위트에서 실패하는 이유가 이것이다.

| 조합 | 결과 | 그 조합이 하는 일 |
|---|---|---|
| `ZW-E3` 단독 | 통과 | — |
| `ZW-B3` → `ZW-E3` | **통과** | 점검 없음, 노드 내부 이동만 |
| **`ZW-E6` → `ZW-E3`** | **실패** | `ZW-E6`는 West를 점검으로 켜고 신규 입장이 거절되는지 본 뒤 끈다. **이동이 전혀 없다** |
| **`ZW-E2` → `ZW-E3`** | **실패** | `ZW-E2`도 West를 점검으로 켠다 |
| `ZW-E2` → `ZW-E3`, **봇 비활성** | **통과** | 봇이 없으면 거절이 발생하지 않는다 |

재현: `dotnet/run_sample.sh ZW-E6,ZW-E3` (러너가 쉼표 구분 목록을 받는다).

**메커니즘.** West가 점검 모드인 동안, East의 X축 순찰 봇이 West의 zone spot으로 진입을 시도하고
West의 `OnActorJoin`이 이를 **거절**한다(§2.3대로 올바른 동작이다). 그 뒤부터 West의 join 경로가
막힌다 — `ZW-E3`의 `JoinWorldReq`(entry spot actor의 `JoinSpot`)가 timeout되고, `all` 실행에서는
cross-node `JoinSpot`이 30초 hang 후 `TimeoutException: SPOT actor join timed out`으로 끝난다.
대상 노드 로그에 그 actor의 흔적이 전혀 없다.

**샘플 쪽은 배제했다.** `ZoneSpot.OnActorJoinAsync`는 거절 시 아무 상태도 남기지 않는다
(`_pendingJoins`에 넣지 않고 `Reject`만 반환).

**framework 소스에서 배제한 것.**

- **대상 측 admission 캐시** — `Runtime/Actors/ZLinkActorHandoffAdmissions.cs`. 거절 경로도 정상
  정리된다: `_admitting`은 `finally`에서 제거되고, `Register`는 거절 reply를 deadline과 함께
  `_pending`에 넣을 뿐 `MarkDrainUnsafe`를 호출하지 않는다.
- **소스 측 롤백** — `ZLinkActorRemoteJoiner.SubmitRoutedJoinActorCoreAsync`. admission이 첫 단계라
  거절 시 `sourceCaptureStarted`·`sourceLeft`가 모두 false이고, `RollbackSourceHandoffAsync`는 아무
  일도 하지 않는다(되돌릴 상태가 없으므로 옳다).

**무엇이 굳는지 특정했다 — 노드 전체의 actor join이다.** `ZW-E6,ZW-E3` 재현의 `zone-node-1` 로그:

1. 점검 모드를 켠 뒤, **거절 자체는 정상 완료된다**(`phase=replied spot=zone-nw actor=…`까지 찍힌다).
2. **그 직후부터 그 노드의 모든 `JoinSpot`이 30초 뒤 `TimeoutException: SPOT actor join timed out`으로
   끝난다.** 원격 join(`bot-nw-x`→`zone-ne`)만이 아니라 **노드 내부 join**(`bot-nw-y`→`zone-sw`,
   `bot-sw-y`→`zone-nw`)도 똑같이 hang한다.
3. 그 노드의 zone spot tick(`ZoneBorderEvent` publish)도 멈춘다. 새 `JoinSpot`은 `phase=received`만
   찍히고 `phase=replied`가 없다.

**서로 다른 actor·서로 다른 target spot의 join이 전부 걸리므로 spot 단위가 아니라 노드 단위 gate가
막힌 것이다.**

**원격 거절이 원인이다 (로컬 거절이 아니다).** 봇을 끄면 같은 조합이 통과한다
(`ZONEWORLD_DISABLE_BOTS=1 run_sample.sh ZW-E6,ZW-E3` → 통과). `ZW-E6`가 만드는 **로컬** 거절(자기 노드
entry spot → `zone-nw`)만으로는 막히지 않는다. 막는 것은 **점검 중인 노드로 진입하려는 다른 노드의
봇들이 만드는 원격 join 거절**이다.

**거절 payload는 원인이 아니다.** `ZLinkSpotActorJoinResult.Reject()`를 payload 없이 반환해도 증상이
그대로다(진단용 변경은 원복함).

**샘플 쪽은 배제했다.** `ZoneSpot.OnActorJoinAsync`는 거절 시 아무 상태도 남기지 않는다.

**framework 소스에서 배제한 것.**

- 소스 측 롤백(`ZLinkActorRemoteJoiner.SubmitRoutedJoinActorCoreAsync`) — 거절은 admission 단계라
  `sourceCaptureStarted`·`sourceLeft`가 모두 false이고 되돌릴 상태가 없다.
- 로컬 join(`ZLinkSpotActivationActors.JoinActorAsync`) — 거절이면 `CommitActorJoinCore`를 건너뛰고
  `ExecuteSerializedAsync`가 정상 반환한다.
- `ZLinkActorJoinSpotCall.ExecuteAsync`(`Runtime/Actors/ZLinkActorContext.cs:71`) — 거절과 수락을
  구분하지 않는다.

**굳는 지점을 코드 한 줄까지 좁혔다 — `HandlerInvoker.InvokeActorJoinAsync`가 반환하지 않는다.**

`ZLinkSpotActivationActors.AdmitRemoteActorJoinAsync`(`:110`)에 계측을 넣어 확인했다(진단용 변경은
원복함).

1. **원격 admission이 그 spot의 직렬 문맥 위에서 실행된다** —
   `ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)`가 **`True`**다. 따라서
   `ExecuteSerializedAsync`로 되돌아가지 않고 직접 호출 경로를 탄다. (self-deadlock 가설은 틀렸다.)
2. 그 직접 경로는 `state.Result = await HandlerInvoker.InvokeActorJoinAsync(descriptor, actorId,
   request, ct)`를 부른다.
3. **그런데 application의 `OnActorJoin`은 호출되지 않는다.** 봇에 대한
   `zone spot: join rejected, node under maintenance` 로그가 **하나도 없다**(같은 실행에서 로컬 join의
   거절 로그는 정상적으로 찍힌다).
4. `ADMIT-DIAG`가 **정확히 2건**만 찍힌다 — West의 두 zone spot에 각각 1건. 봇은 500ms마다 재시도하는데
   더 이상 admission이 들어오지 않는다. **첫 원격 admission이 그 spot을 굳힌다.**

`ZLinkSerialExecutionQueue.DrainAsync`는 `_drainGate`를 잡고 **한 번에 하나의 work item만**
`await item.InvokeAsync(...)`로 실행한다. 그 item이 끝나지 않으면 그 spot의 큐가 영구히 멈춘다 —
tick이 죽고 이후 모든 `JoinSpot`이 `phase=received`에서 멈추는 관측과 정확히 맞는다.

**따라서 결함은 `HandlerInvoker.InvokeActorJoinAsync`의 원격 admission 경로에 있다.** application
핸들러에 닿기 전에 무엇인가를 기다리며 반환하지 않는다. 로컬 join(`JoinActorAsync`)은 같은
`InvokeActorJoinAsync`를 부르면서도 정상 동작하므로, **원격 진입 시에만 성립하는 대기 조건**이다.

**다음 단계 — 남은 후보는 둘뿐이다.** `ZLinkSpotHandlerInvoker.InvokeActorJoinAsync`
(`Runtime/Spots/ZLinkSpotHandlerInvoker.cs:63`)는 애플리케이션 핸들러에 닿기까지 두 단계만 거친다.

1. `ResolveHandler(descriptor.HandlerType)` — DI 스코프에서 핸들러를 해석한다.
2. `ZLinkHandlerInvocationEngine.InvokeAsync(handler, invoker, …)` — 실제 호출.

로컬 join(`JoinActorAsync`)도 **같은** `InvokeActorJoinAsync`를 부르는데 정상 동작한다. 따라서
**원격 진입일 때만 달라지는 것**을 찾아야 한다 — 원격은 이미 그 spot의 직렬 문맥 위에서 호출되므로
(`ambientIsSelf=True`), 그 안에서 다시 직렬 자원이나 스코프를 얻으려 하면 자기 자신을 기다리게 된다.
`ZLinkHandlerInvocationEngine`이 handler invocation executor로 넘기고 그 완료를 기다리는 구조라면,
그 executor가 같은 직렬 큐를 다시 밟는지 확인한다.

**계측 방법.** `InvokeActorJoinAsync` 진입 직후와 `ResolveHandler` 직후에 로그를 넣고
`run_sample.sh ZW-E6,ZW-E3`를 돌리면 두 후보 중 어디서 멈추는지 한 번에 갈린다.

**왜 이 경로가 처음 밟히나.** 정본 6종에는 **remote join을 거절하는** 시나리오가 없다. ZoneWorld의
점검 모드가 §2.3대로 "목표 노드가 권위로 재판정해 거절"을 실제로 발생시키면서 처음 드러났다.

#### 해소 — bound session route가 첫 relay 전에 전파되지 않았다 (수정 완료)

### 8.10 구현하며 드러난 정본 설계 결함 (수정해 반영함)

| 항목 | 결함 | 수정 |
|---|---|---|
| §7.3 `EnterZoneMsg` | `ActorRef`를 join payload에 실으면 **cross-node transfer 후 stale**이다. payload는 출발 노드에서 만들어지고, actor는 자기 `ActorRef`를 알지 못한다(`IZLinkActor`는 `ActorId`만 노출) | zone spot이 actor 인스턴스를 보관해 `BoundSession`으로 push. 봇만 actor directory로 ref 해석. §8.3 재작성 |
| §7.3 `LeaveZoneMsg` | zone 이동은 **반드시 `JoinSpot`**이어야 한다(그것이 transfer를 일으키는 유일한 메커니즘). 그러면 이전 spot 퇴장은 framework의 `OnLeaveActor`가 알려 주므로 이 메시지는 죽은 계약 | 삭제 |
| §7.3 `EnterZoneMsg` | §2.3의 "노드 내부 이동은 허용, 진입만 차단"을 target spot의 `OnActorJoin`이 판정해야 하는데 framework는 source node를 admission 콜백에 넘기지 않는다(spec 23 §3.1) | `FromNodeId` 필드 추가 |
| §4 `zoneworld.actors` | Req/Res 메시지가 §7에 정의되지 않았고, 어느 노드에 도달해야 하는지도 불명확했다 | `EnsurePlayerActorReq/Res` 확정. **입장 zone 호스팅 노드만 서빙**(그 노드가 admission 권위) |
| §4·§5·§6 `Gateway` | Gateway에 entry spot을 두었으나, player actor는 `ZoneNode`에 산다. Gateway는 spot mesh에 **참여만** 하면 원격 actor에 session을 bind할 수 있다 | entry spot은 `ZoneNode`가 소유(`ZoneEntrySpot`) |

### 8.6 Phase 2 — 공통 브라우저 client

**빌드·스택** (정본 §9.2)

- [ ] Vite
- [ ] Preact + `@preact/signals`
- [ ] Canvas 2D 월드 렌더
- [ ] `@zlink-systems/stream-connector` (브라우저 entrypoint)
- [ ] Vitest (domain 단위 테스트)
- [ ] Playwright headless Chromium 러너 — **공통 client 하나를 5개 언어 러너가 각각 띄우는 배선**

**FSD 계층** (정본 §9.3) — 의존 방향은 `app` → `pages` → `widgets` → `features` → `entities` → `shared`

- [ ] `app/` — `game.tsx`, `ops.tsx`
- [ ] `pages/` — `game`, `ops`
- [ ] `widgets/` — `world-canvas`, `game-hud`, `node-table`, `alert-list`
- [ ] `features/` — `join-world`, `move-player`, `announce-world`, `set-maintenance`, `node-diagnostics`, `watch-nodes`
- [ ] `entities/` — `player`, `zone`, `node`, `announcement`
- [ ] `shared/` — `api`, `config`, `lib`, `ui`
- [ ] push 전용 메시지는 feature가 아니라 해당 `entities`의 model이 적용한다
- [ ] `entities/zone`이 서버와 같은 월드 규칙(§2)을 구현하고 브라우저 없이 단위 테스트된다

**게임 화면** (정본 §10.1)

- [ ] 격자 100×100 + zone 경계선(X=50, Y=50)
- [ ] 경계 밴드 시각 구분
- [ ] 인접 zone 플레이어를 같은 zone 플레이어와 다른 표시로 구분
- [ ] 봇(`IsBot=true`)을 사람과 다른 표시로 구분
- [ ] 방향키 → `MoveMsg`, **좌표를 client가 먼저 바꾸지 않는다**
- [ ] 현재 `ZoneId`·`NodeId` 상시 표시 + `ZoneChangedNotify` 갱신
- [ ] `Transferred=true` 시각 표시
- [ ] WebSocket 연결 상태 표시(zone 이동 중 유지됨을 확인 가능)
- [ ] `WorldAnnounceNotify` 표시 + `AnnouncementId` 중복 제거
- [ ] `MoveRejectedNotify`의 `Reason` 표시

**관제 화면** (정본 §10.2)

- [ ] 노드 목록 — `Registered`·`Connected`·`Maintenance`·`Zones`·`PlayerCount`
- [ ] 노드 종료 시 `Registered=false`
- [ ] 연결 단절 시 `Connected=false`
- [ ] `NodeAlertNotify` 시간순 표시
- [ ] 공지 발행 입력 + 버튼
- [ ] **노드별** 점검 전환 버튼
- [ ] **노드별** 진단 버튼
- [ ] 격리 확인 — 한 노드만 전환 시 다른 노드 무변화
- [ ] **polling하지 않는다** — `NodeStatusNotify` push로만 갱신

**UI 품질** (정본 §10.0)

- [ ] 깔끔함 · 정보 위계 · 상태를 색+형태로 구분(색만으로 구분하지 않음)
- [ ] 변화 전이 150~250ms · 일관된 시각 언어 · 여백/정렬 · WCAG AA 명암비 · 고정폭 수치
- [ ] tick(100ms) 갱신에도 끊기지 않음(Canvas는 `requestAnimationFrame`)

### 8.7 Phase 3 — 교차 검증

**같은 client 하나**로 5개 언어 server 전부에 대해 정본 §12 smoke를 재실행한다. 언어별로 따로
통과한 것과 하나의 client가 전부에 붙는 것은 다른 검증이다.

- [ ] client × `dotnet` server
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
