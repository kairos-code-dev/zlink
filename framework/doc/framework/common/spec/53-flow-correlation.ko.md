[스펙 목차](README.ko.md) | [이전: 메시지 흐름 추적과 dispatch 관측](52-message-flow-tracing.ko.md) | [다음: Graceful Drain & Handoff 수명주기 계약](54-graceful-drain-handoff.ko.md)

# 메시지 흐름 상관관계 (Flow Correlation)

> **구현 상태:** 목표 계약은 이 문서에 고정되어 있으며 현재 구현과의 차이는
> [구현 차이](90-implementation-gap.ko.md)와 구현 계획에서 추적한다. 채널 envelope·스트림 헤더의
> `flow_id`는 이전 framework와 호환 계층을 두지 않고 모든 언어와 connector가 같은 protocol
> version으로 함께 전환한다.

이 문서는 [메시지 흐름 추적](52-message-flow-tracing.ko.md)(이하 **MFT**)이 스스로 명시한 세 이음매를
마감하는 언어 중립 공통 스펙이다. MFT의 개념과 관측 event를 확장하지만 `flow_id` wire 추가는
호환 decoder를 두지 않는 일괄 codec 교체다. `correlation_id`의 기존 의미·필드 포맷은 그대로 둔다.

## 1. 배경 — MFT가 남긴 세 이음매

| 이음매 | MFT 위치 | 문제 |
|--------|----------|------|
| corr 키잉 한계 | [MFT §7](52-message-flow-tracing.ko.md) | `correlation_id`는 channel/route/stream에서만 1급이고, spot 구독·actor·publish·join_spot은 `spot_rid`/`actor_id`로 키잉 → 한 흐름이 spot/actor 경계에서 corr로 관통되지 않음 |
| corr 비유일성 | [MFT §8](52-message-flow-tracing.ko.md) | `correlation_id`는 노드별 프로세스 전역 단조 카운터라 전역 유일이 아님 → 대규모 fleet에서 grep 충돌 |
| 언어별 구현 차이 | [MFT §11](52-message-flow-tracing.ko.md) | 언어별 진행률 상이 + stream/actor gateway 로거 명시 주입 누락 시 그 surface만 조용히 무로그(MFT §5 함정) |

이 문서는 상위 키 **`flow_id`**를 도입해 앞 둘을, gateway 기본 배선으로 셋째를 마감한다.

## 2. flow_id 개념

`flow_id`는 **한 논리 흐름의 시작점에서 부여되어 spot/actor 경계를 넘어 전파되는 상위 키**다.
`correlation_id`(전송계층 request↔reply 짝짓기용)는 그대로 두고, `flow_id`는 그 위에서 흐름 전체를
잇는다.

기존 [message model §3](03-message-model.ko.md)의 여러 단계 추적 정보는 `flow-id`가 소유한다.
public/runtime/wire 어디에도 독립된 `trace_id`와 `flow_id`를 동시에 만들지 않는다.

*예:* 유저가 STREAM으로 `PlaceMark`를 보냄 → actor relay → room-spot handler. 현재 corr은
stream→actor relay 구간까지만 이어지고 room-spot 내부 dispatch는 `spot_rid`로 키잉돼 corr 라인이
끊긴다. `flow_id`는 이 전체를 하나로 잇는다.

```
zlink flow: phase=received  ... corr=1a2b flow=f-9f3 actor=42
zlink flow: phase=sent      ... flow=f-9f3 spot=room-7      # actor→room-spot join_spot
zlink flow: phase=received  ... flow=f-9f3 spot=room-7      # room-spot 내부 dispatch (corr 없어도 flow로 연결)
zlink flow: phase=replied   ... flow=f-9f3 spot=room-7
```

### 2.1 생성 규칙 — create-if-absent

- **부여 지점** — framework 인바운드에 id가 없거나, timer/lifecycle callback이 시작되거나,
  framework callback 밖의 application 코드가 첫 outbound message를 제출할 때 `flow_id`를 만든다.
- **create-if-absent** — 노드가 인바운드에 `flow_id`가 **없을 때만** 새로 생성한다. 있으면
  재생성하지 않고 그대로 전파한다. 이 규칙으로 "내가 첫 홉인가"를 판정한다.
- **connector 생성** — client stream connector가 시작하는 outbound send/request는 id가 없으면 항상
  `flow_id`를 생성한다. connector에는 별도 message-flow 설정을 추가하지 않는다. 생성된 id는 reply와
  인과 관계가 있는 bound push에 전파되어 client와 server 로그를 조인할 수 있다.

### 2.2 프로토콜 필드 여부 결정 (생성=게이트, 전파=무조건)

MFT §9는 corr에 대해 "트레이싱 off여도 항상 생성"을 명시적으로 결정했다. `flow_id`는 다음으로
정한다:

- **host 생성은 모드 게이트.** 트레이싱이 완전히 off(MFT §2 `off`)인 framework host 진입점은
  `flow_id`를 새로 만들지 않는다. connector 발원은 §2.1의 무설정 생성 규칙을 따른다.
- **전파(echo)는 무조건.** 인바운드에 `flow_id`가 있으면, 이 노드의 트레이싱 모드와 **무관하게**
  다음 홉으로 전파한다. 그렇지 않으면 off 노드를 한 번만 거쳐도 흐름이 영구히 끊긴다.

> 즉 off 상태의 framework host는 **새 흐름을 시작하지 않지만, 지나가는 흐름을 끊지도 않는다.**
> connector 발원은 설정 없이 시작한다. MFLOW-EXT-013이 이 불변을 검증한다.

### 2.3 길이·문자집합 상한

`flow_id`는 lowercase hyphenated UUIDv7 문자열 **36 ASCII bytes**로 고정한다. 다른 길이, uppercase,
UUID version 또는 잘못된 variant bit는 decode 단계에서 `RequestProtocolError`/connector
`ProtocolError`로 거부한다.

## 3. 와이어 레이아웃 (MFT §9 대응)

`flow_id`는 정의상 노드 경계(STREAM→actor relay, join_spot route 메시지, transfer commit, channel
request)를 넘으므로 와이어에 실려야 한다. 이 절이 없으면 §6의 크로스노드 회귀가 검증 불가다.

### 3.1 채널 envelope

`envelope_header_t`의 첫 필드에 required `format_marker=0xF2`를 추가하고 `flow_id`와 root
`flow_origin`을 함께 **1급 optional 필드**로 추가한다.
둘은 항상 함께 존재하거나 함께 없으며 모든 route/actor/Spot relay가 두 값을 보존한다.

### 3.2 스트림 헤더

MFT §9가 correlation_id에 `has_correlation_id = 0x08`을 배정했다. `flow_id`는:

- `header_flags_t`에 **`has_flow_id = 0x10`** 추가.
- 바이트 배치: `format_marker(0xF2), kind, codec, flags, [request_seq], name(u8 len+bytes), [metadata], [correlation_id(u8 len+bytes)], [flow_id(36B), flow_origin(u8)]` — marker와 두 flow 필드를 함께 추가한다.
- `flow_origin` wire 값은 `1=inbound`, `2=timer`, `3=application`, `4=lifecycle`이다. connector가 만든
  flow는 `application`이며 root origin은 모든 홉에서 바꾸지 않는다.
- control 패킷은 flag 불가(MFT §9 규칙 유지). send/request/response/error 허용.
- flag가 set되지 않으면 marker가 있는 현재 codec의 frame에 `flow_id`가 없다는 뜻이다.

### 3.3 route mesh / transfer commit 전파

- route mesh 메시지: envelope의 `flow_id`와 `flow_origin`을 그대로 전파.
- actor transfer commit([spot-actor §5](23-spot-actor.ko.md)): commit 요청에 현재 흐름의 `flow_id`와
  `flow_origin`을
  포함해, 이동 후 target actor의 후속 라인이 같은 흐름으로 이어진다(DRAIN-003 / MFLOW-EXT-002 교집합).

### 3.4 mandatory format marker와 혼합 배포 차단

`flow_id`와 `flow_origin`을 추가한 codec은 모든 framework 언어, stream connector와 actor/session
relay에 같이 적용한다. 이전 frame을 별도 해석하는 dual decoder나 flow 필드를 제거하는 relay를
두지 않는다. marker가 없거나 `0xF2`가 아니거나 알 수 없는 mandatory flag가 있으면 기존 protocol
error 표면으로 명시적으로 실패한다. 구 decoder도 첫 byte `0xF2`를 기존 kind로 해석할 수 없으므로
혼합 배포가 조용히 성공하지 않는다.

## 4. 홉 커버리지 표

MFT §7 길목 표에 flow 열을 더한다. flow의 **생성/전파/기록** 지점을 고정하고, MFT §7에 없던 신규
길목(spot 내부 dispatch, timer, bound push)을 새로 정한다.

| surface / 길목 | flow 동작 |
|----------------|-----------|
| STREAM/channel/route 인바운드 | create-if-absent 생성 |
| callback 밖 application의 최초 outbound | 새 flow 생성(`origin=application`) |
| channel/route dispatch·client 제출 | 전파 + 기록 |
| actor packet relay / join_spot | 전파 + 기록(corr 없어도 flow 기록) |
| **spot 내부 dispatch**(신규) | 전파 + 기록 — 이 지점이 §1 이음매의 핵심 |
| **spot publish fan-out**(신규) | §4.1 트리 규칙 |
| **actor→client bound push**(신규) | 전파(있으면) |
| **timer/lifecycle 발원 콜백**(신규) | §4.2 규칙 |
| **dispatch error reporter 라인** | flow= 기록(§4.3) |

### 4.1 publish fan-out — 트리 허용

한 흐름이 publish로 N개 구독자에 갈라진다([Bingo](../sample/bingo/README.ko.md):
room reward event → 다른 Play 구독자). **트리 구조를 허용**한다 — N개 구독자 라인이 모두 같은
`flow_id`를 가진다. owner가 아니어서 **skip하는 구독자 라인에도 flow=를 찍어** "왜 처리 안 됐나"를
추적 가능하게 한다(MFLOW-EXT-010).

### 4.2 인바운드가 없는 발원 콜백

SPOT timer, [DeliveryDispatch](../sample/deliverydispatch/README.ko.md)의 timeout 재배정,
drain 같은 lifecycle callback에는 인바운드 메시지가 없다. timer는 `origin=timer`, drain·startup·
shutdown lifecycle은 `origin=lifecycle`로 새 flow를 시작한다. framework callback 밖의 application
코드가 첫 outbound를 제출하면 `origin=application`으로 시작한다. 인바운드 발원은 `origin=inbound`다.
이후 인과 관계가 있는 outbound만 같은 id를 전파한다.

### 4.3 error reporter 라인

MFT의 핵심 가치는 성공/실패가 같은 키 stream으로 읽히는 것이다([MFT §1](52-message-flow-tracing.ko.md),
MFLOW-008). 따라서 **dispatch error 이벤트에도 `flow_id` 필드를 추가**하고 error 라인에 `flow=`를
찍는다. 그렇지 않으면 실패 지점에서 flow 추적이 끊긴다(MFLOW-EXT-009).

### 4.4 비동기 실행 문맥과 정리

framework host는 callback을 호출하고 framework가 생성·await하는 continuation의 비동기 실행 문맥에
현재 flow를 저장한다. callback 완료 시 이전 문맥을 복원하여 다음 관련 없는 callback으로 id가
누출되지 않게 한다. application이 임의 executor나 detached task를 직접 만들면 framework가 그 문맥
전파를 보장하지 않으며, 그 작업의 첫 outbound는 `origin=application`인 새 flow를 시작한다.

브라우저 TypeScript projection에는 비동기 작업별 문맥을 격리하는 표준 기능이 없다. 이 환경에서는
connector instance에 현재 flow를 저장하지 않는다. inbound message가 시작한 관련 outbound는 call
builder의 `flowFrom(message)`에 message가 가진 `flowId`와 root `flowOrigin`을 한 쌍으로 넘긴다.
`flowFrom(...)`을 호출하지 않은 send/request는 관련 없는 작업으로 보고 `origin=application`인 새
UUIDv7 flow를 만든다. handler가 `await`한 뒤에도 message 값은 그대로이므로 명시적 전달 결과는
유지되고, 동시에 실행되는 timer나 event callback에는 암묵적 상태가 노출되지 않는다.

이 결정 전에는 두 설계를 비교했다. connector instance가 Promise 완료까지 현재 flow를 보관하는
방식은 호출 표면이 짧지만 같은 instance의 관련 없는 callback에 값을 노출한다. 명시적 전달 방식은
관련 outbound call에 한 번의 표시가 필요하지만 비동기 격리를 플랫폼 기능에 의존하지 않고
보장한다. 전역 Promise, timer 또는 event callback을 수정하는 방식은 사용하지 않는다. 다른 언어의
framework host에는 public context capture/wrap API를 추가하지 않으며, application 전역 변수나 thread
id로 현재 flow를 추정하지 않는다.

## 5. 샘플링 상호작용

MFT §2의 `sample_rate`는 이벤트 단위 thinning이다. `flow_id`의 존재 이유가 "한 번의 grep으로 흐름
관통"인데 라인이 확률적으로 빠지면 목적이 죽는다. 규칙:

- **flow 단위 일관 샘플링**을 기본으로 한다: `sample_rate<1`일 때 thinning 결정을 개별 이벤트가
  아니라 **`flow_id` 해시 기준**으로 내려, 한 흐름은 전부 남거나 전부 빠진다.
- `dropped`/에러는 MFT §2대로 샘플링을 우회해 항상 통과한다.

## 6. ID 형식과 생성 비용

`flow_id`는 lowercase hyphenated UUIDv7 36-byte ASCII canonical 표현 하나만 쓴다.
노드별 monotonic id는 fleet에서 충돌하므로 public option으로 제공하지 않는다. tracing mode가 `Off`면
새 id를 만들지 않고, 그 외에는 진입점에서 한 번만 생성한다. 홉마다 재생성하지 않으며 와이어 비용은
흐름당 한 개 id로 제한한다. ID 생성 알고리즘은 호출자가 선택할 설정이 아니라 framework 내부
결정이다.

## 7. gateway 기본 배선 (MFT §5 함정 제거)

MFT §5는 channel/spot에는 dispatch 옵션이 자동 전파되지만 stream/actor gateway는 로거 명시 주입이
필요하고, 빠뜨리면 그 surface만 조용히 무로그라고 경고한다.

**계약:** framework 부트스트랩에서 stream/actor gateway에도 **기본 로거 sink를 자동 배선**한다.
명시 주입이 있으면 우선하되, 없을 때 침묵 대신 기본 sink로 폴백한다. "조용한 무로그"를 기본 동작에서
제거한다.

- **게이팅 불변**: 자동 배선은 sink 연결일 뿐 출력이 아니다. MFT §2 모드 게이트는 그대로이며,
  MFLOW-001(`off`=완전 침묵)은 유지된다(배선 ≠ 출력, MFLOW-EXT-005).
- **언어 중립 의미**: DI 부트스트랩이 없는 C++ 레퍼런스에서 "자동 배선"은 gateway 생성 시 dispatch
  옵션의 로거 핸들 기본값을 상속시키는 것으로 정의한다.

## 8. 이벤트 형상 (C++ 레퍼런스)

MFT §3의 `message_flow_event_t`에 필드를 추가한다(값이 있을 때만 채운다).

```cpp
enum class flow_origin_t { inbound, timer, application, lifecycle };  // §4.2
struct message_flow_event_t {
    // ... 기존 MFT §3 필드 ...
    std::optional<std::string>   flow_id;      // 정확히 36B ASCII(UUIDv7, §2), create-if-absent
    std::optional<flow_origin_t> flow_origin;  // inbound | timer | application | lifecycle
    // dispatch_error_event_t 에도 동일 flow_id 필드 추가 (§4.3)
};
```

별도 flow-id builder나 runtime control을 추가하지 않는다. 기존 message-flow mode가 `Off`인지 여부가
생성 gate이고, ID 형식은 §6의 단일 형식이다. 이벤트에는 `flow_id`/`flow_origin` 필드를 더한다. 정식 언어 표면은
각 언어 monitoring 문서가 소유한다:
[.NET §11](languages/dotnet/01-system-structure.ko.md) ·
[Java §9](languages/java/01-system-structure.ko.md) ·
[Node §11](languages/node/01-system-structure.ko.md) ·
[C++ §9](languages/cpp/02-framework-interfaces.ko.md) · [Kotlin §8](languages/kotlin/02-handler-interfaces.ko.md).
두 event 필드도 wire와 같은 optional pair다. `flow_id`가 없으면 `flow_origin`도 없고, `flow_id`가
있으면 root `flow_origin`도 반드시 있다. 관측 경로는 둘 중 하나만 있는 불완전한 event를 내보내지 않는다.
로그 토큰 `flow=`·`origin=`은 언어 간 바이트 동일([runtime-metrics §4.0](51-runtime-metrics.ko.md)).

## 9. 파싱 규약과 구현 상태

- `flow`/`corr`/`label` 조합으로 다중 노드 흐름을 하나로 잇는 파싱 규약은 [MFT §5.1](52-message-flow-tracing.ko.md)의
  구조화 필드 규약을 이 문서의 `flow`, `origin`, `corr`, `label` key로 확장한다. 정규식 없이 구조화 key/value로
  조인한다.
- 언어별 구현 진행률과 gateway 자동 배선 현황은 [언어별 구현 차이](90-implementation-gap.ko.md)에
  기록한다.

## 10. 일괄 교체

`correlation_id`의 request/reply 의미는 유지하지만 header protocol은 새 version으로 일괄 교체한다.
구버전 decoder, flow 필드를 제거하는 relay와 corr-only compatibility mode를 유지하지 않는다. 로그에서
`flow=`는 id가 생성되거나 전달된 경우에만 나타나며, MFT `Off`의 출력 없음 계약은 유지한다.

## 11. 회귀 테스트 매트릭스 (MFLOW-EXT)

| ID | 검증 |
|----|------|
| MFLOW-EXT-001 | `flow_id`가 STREAM inbound에서 create-if-absent 생성되어 actor relay·join_spot·room-spot dispatch까지 `flow=`로 이어짐 |
| MFLOW-EXT-002 | actor 노드 간 transfer commit에도 `flow_id`가 보존됨(DRAIN-003 교집합) |
| MFLOW-EXT-003 | tracing `Off` 진입점은 id를 만들지 않지만 전달받은 id는 다음 홉에 보존 |
| MFLOW-EXT-004 | 전역 유일 id가 흐름당 1회 생성되고 홉마다 재생성되지 않음 |
| MFLOW-EXT-005 | gateway 로거 자동 배선 후에도 MFLOW-001(off=완전 침묵) 유지(배선 ≠ 출력) |
| MFLOW-EXT-006 | `flow`/`corr`/`label` 조합 파싱 규약으로 다중 노드 흐름이 하나로 조인 |
| MFLOW-EXT-007 | `flow_id`/`flow_origin`이 채널 envelope·스트림 헤더에서 round-trip되고 unknown mandatory flag는 protocol error |
| MFLOW-EXT-008 | flow_id 미탑재 인바운드에서만 신규 생성(create-if-absent), 탑재 시 재생성 안 함 |
| MFLOW-EXT-009 | dispatch **error** 라인에도 flow=가 찍혀 `grep flow=<id>`로 성공·실패 함께 잡힘 |
| MFLOW-EXT-010 | publish fan-out에서 구독자 N개 라인이 같은 flow_id(owner skip 라인 포함) |
| MFLOW-EXT-011 | timer/application/lifecycle 발원이 각각 올바른 origin으로 새 flow를 시작 |
| MFLOW-EXT-012 | `sample_rate<1`에서 flow 단위 일관 샘플링(한 flow 전부 남거나 전부 빠짐), dropped/에러는 항상 통과 |
| MFLOW-EXT-013 | 트레이싱 off 노드를 경유해도 `flow_id` **전파**는 유지(생성/기록만 게이트) |
| MFLOW-EXT-014 | framework host의 async continuation은 flow를 보존하고 callback 종료 뒤 관련 없는 callback으로 id가 누출되지 않음. 브라우저 TypeScript는 `flowFrom(message)`로 표시한 outbound만 inbound flow를 보존하고 표시하지 않은 동시 callback은 새 application flow를 사용 |
| MFLOW-EXT-015 | client connector outbound는 별도 설정 없이 id를 생성하고 reply와 server 로그가 같은 id로 조인됨 |

## 12. 언어별 투영

| 언어 | 표면 |
|------|------|
| `.NET` | event `FlowId`/nullable `FlowOrigin` optional pair; host는 기존 message-flow mode로, connector는 무설정으로 자동 생성; gateway 기본 sink 자동 배선 |
| Java/Kotlin | `ZLinkMessageFlowEvent`에 `flowId`/`flowOrigin`; connector 발원 무설정 생성; SLF4J 바인딩 기본 폴백 |
| Node | flow 이벤트에 `flowId`/`flowOrigin`; connector 발원 무설정 생성; NestJS 부트스트랩에서 gateway sink 자동 주입 |
| C++ (레퍼런스) | `message_flow_event_t`에 `flow_id`/`flow_origin`; connector 발원 무설정 생성; gateway tracer 기본 배선 |

---

> 관련: [메시지 흐름 추적](52-message-flow-tracing.ko.md) · [runtime metrics](51-runtime-metrics.ko.md) ·
> [graceful drain & handoff](54-graceful-drain-handoff.ko.md) · [spot-actor](23-spot-actor.ko.md)
