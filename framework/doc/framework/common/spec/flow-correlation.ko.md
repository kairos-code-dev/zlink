[스펙 목차](README.ko.md)

# 메시지 흐름 상관관계 (Flow Correlation)

> **상태: 제안(Proposed).** [공개 계약 관리](public-contract-governance.ko.md)의 승격 절차를 아직
> 거치지 않았다. 이 계약은 채널 envelope·스트림 헤더에 **`flow_id` 필드 추가**(와이어 변경, §3)를
> 함축한다 — 승격 전 그 하위호환을 함께 검토해야 한다.

이 문서는 [메시지 흐름 추적](message-flow-tracing.ko.md)(이하 **MFT**)이 스스로 명시한 세 이음매를
마감하는 언어 중립 공통 스펙이다. MFT를 **대체하지 않고 additive하게 확장**하며,
`correlation_id`의 기존 의미·와이어 포맷은 그대로 둔다.

## 1. 배경 — MFT가 남긴 세 이음매

| 이음매 | MFT 위치 | 문제 |
|--------|----------|------|
| corr 키잉 한계 | [MFT §7](message-flow-tracing.ko.md) | `correlation_id`는 channel/route/stream에서만 1급이고, spot 구독·actor·publish·join_spot은 `spot_rid`/`actor_id`로 키잉 → 한 흐름이 spot/actor 경계에서 corr로 관통되지 않음 |
| corr 비유일성 | [MFT §8](message-flow-tracing.ko.md) | `correlation_id`는 노드별 프로세스 전역 단조 카운터라 전역 유일이 아님 → 대규모 fleet에서 grep 충돌 |
| 언어별 구현 차이 | [MFT §11](message-flow-tracing.ko.md) | 언어별 진행률 상이 + stream/actor gateway 로거 명시 주입 누락 시 그 surface만 조용히 무로그(MFT §5 함정) |

이 문서는 상위 키 **`flow_id`**를 도입해 앞 둘을, gateway 기본 배선으로 셋째를 마감한다.

## 2. flow_id 개념

`flow_id`는 **한 논리 흐름의 시작점에서 부여되어 spot/actor 경계를 넘어 전파되는 상위 키**다.
`correlation_id`(전송계층 request↔reply 짝짓기용)는 그대로 두고, `flow_id`는 그 위에서 흐름 전체를
잇는다.

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

- **부여 지점** — 흐름 진입점(STREAM inbound 또는 최초 channel request)이 `flow_id`를 만든다.
- **create-if-absent** — 노드가 인바운드에 `flow_id`가 **없을 때만** 새로 생성한다. 있으면
  재생성하지 않고 그대로 전파한다. 이 규칙으로 "내가 첫 홉인가"를 판정한다.
- **connector 생성 허용** — connector(클라이언트)도 `flow_id`를 생성할 수 있다. 생성 시 reply·
  bound push에 echo되어 클라이언트 로그와 서버 로그가 한 `flow_id`로 조인된다.

### 2.2 프로토콜 필드 여부 결정 (생성=게이트, 전파=무조건)

MFT §9는 corr에 대해 "트레이싱 off여도 항상 생성"을 명시적으로 결정했다. `flow_id`는 다음으로
정한다:

- **생성은 모드 게이트.** 트레이싱이 완전히 off(MFT §2 `off`)인 진입점은 `flow_id`를 새로 만들지
  않아도 된다(비용 회피).
- **전파(echo)는 무조건.** 인바운드에 `flow_id`가 있으면, 이 노드의 트레이싱 모드와 **무관하게**
  다음 홉으로 전파한다. 그렇지 않으면 off 노드를 한 번만 거쳐도 흐름이 영구히 끊긴다.

> 즉 off 노드는 **새 흐름을 시작하지 않지만, 지나가는 흐름을 끊지도 않는다.** MFLOW-EXT-013이 이
> 불변을 검증한다.

### 2.3 길이·문자집합 상한

`flow_id`는 **≤64B, ASCII**로 제한한다(와이어 `u8 len` 전제이나 실질 상한을 좁혀 구조화 필드·수집기
계약을 닫는다).

## 3. 와이어 레이아웃 (MFT §9 대응)

`flow_id`는 정의상 노드 경계(STREAM→actor relay, join_spot route 메시지, transfer commit, channel
request)를 넘으므로 와이어에 실려야 한다. 이 절이 없으면 §6의 크로스노드 회귀가 검증 불가다.

### 3.1 채널 envelope

`envelope_header_t`에 `flow_id`를 **1급 optional 필드**로 추가한다(corr과 동일 취급). 값이 없으면
필드를 쓰지 않는다.

### 3.2 스트림 헤더

MFT §9가 correlation_id에 `has_correlation_id = 0x08`을 배정했다. `flow_id`는:

- `header_flags_t`에 **`has_flow_id = 0x10`** 추가.
- 바이트 배치: `kind, codec, flags, [request_seq], name(u8 len+bytes), [metadata], [correlation_id(u8 len+bytes)], [flow_id(u8 len+bytes)]` — `flow_id`는 **correlation_id 블록 뒤 마지막**에, flag가 set일 때만 `u8 길이 + 바이트`.
- control 패킷은 flag 불가(MFT §9 규칙 유지). send/request/response/error 허용.
- **flag 미set이면 필드 없음 = 하위호환.** 코덱을 공유하는 구버전 노드/connector는 이 필드를 모른
  채 프레임을 정상 decode한다.

### 3.3 route mesh / transfer commit 전파

- route mesh 메시지: envelope의 `flow_id`를 그대로 전파.
- actor transfer commit([spot-actor §5](spot-actor.ko.md)): commit 요청에 현재 흐름의 `flow_id`를
  포함해, 이동 후 target actor의 후속 라인이 같은 흐름으로 이어진다(DRAIN-003 / MFLOW-EXT-002 교집합).

### 3.4 구버전 노드 통과 시

`flow_id`를 모르는 구버전 노드를 흐름이 경유하면 그 노드는 필드를 전파하지 못한다. 이 경우 **전파
유실을 허용하고 정직하게 기록**한다 — 하류 노드는 create-if-absent로 새 `flow_id`를 시작하며, 흐름이
그 지점에서 분절됨을 수집 단계에서 label+corr로 부분 조인한다. "구버전 경유 시에도 flow가 관통된다"고
과장하지 않는다.

## 4. 홉 커버리지 표

MFT §7 길목 표에 flow 열을 더한다. flow의 **생성/전파/기록** 지점을 고정하고, MFT §7에 없던 신규
길목(spot 내부 dispatch, timer, bound push)을 새로 정한다.

| surface / 길목 | flow 동작 |
|----------------|-----------|
| STREAM 인바운드(진입점) | create-if-absent 생성 |
| 최초 channel request(진입점) | create-if-absent 생성 |
| channel/route dispatch·client 제출 | 전파 + 기록 |
| actor packet relay / join_spot | 전파 + 기록(corr 없어도 flow 기록) |
| **spot 내부 dispatch**(신규) | 전파 + 기록 — 이 지점이 §1 이음매의 핵심 |
| **spot publish fan-out**(신규) | §4.1 트리 규칙 |
| **actor→client bound push**(신규) | 전파(있으면) |
| **timer 발원 콜백**(신규) | §4.2 규칙 |
| **dispatch error reporter 라인** | flow= 기록(§4.3) |

### 4.1 publish fan-out — 트리 허용

한 흐름이 publish로 N개 구독자에 갈라진다([GameQuest](../dotnet/guide/samples/gamequest-sample.ko.md):
gameplay event → 다중 instance 구독). **트리 구조를 허용**한다 — N개 구독자 라인이 모두 같은
`flow_id`를 가진다. owner가 아니어서 **skip하는 구독자 라인에도 flow=를 찍어** "왜 처리 안 됐나"를
추적 가능하게 한다(MFLOW-EXT-010).

### 4.2 timer 발원 콜백

SPOT timer, [DeliveryDispatch](../dotnet/guide/samples/deliverydispatch-sample.ko.md)의 timeout
재배정, drain 등은 인바운드 메시지가 없다. 규칙: **timer 발원 작업은 새 flow를 시작한다**
(`origin=timer` 표시). 이후 그 작업이 내보내는 메시지는 이 새 flow_id로 전파된다. (MFT §7 표에 timer
길목이 없으므로 이 스펙이 신규 정의한다.)

### 4.3 error reporter 라인

MFT의 핵심 가치는 성공/실패가 같은 키 stream으로 읽히는 것이다([MFT §1](message-flow-tracing.ko.md),
MFLOW-008). 따라서 **dispatch error 이벤트에도 `flow_id` 필드를 추가**하고 error 라인에 `flow=`를
찍는다. 그렇지 않으면 실패 지점에서 flow 추적이 끊긴다(MFLOW-EXT-009).

## 5. 샘플링 상호작용

MFT §2의 `sample_rate`는 이벤트 단위 thinning이다. `flow_id`의 존재 이유가 "한 번의 grep으로 흐름
관통"인데 라인이 확률적으로 빠지면 목적이 죽는다. 규칙:

- **flow 단위 일관 샘플링**을 기본으로 한다: `sample_rate<1`일 때 thinning 결정을 개별 이벤트가
  아니라 **`flow_id` 해시 기준**으로 내려, 한 흐름은 전부 남거나 전부 빠진다.
- `dropped`/에러는 MFT §2대로 샘플링을 우회해 항상 통과한다.

## 6. monotonic 모드의 전역 비유일성 (정직한 한계)

`flow_id` 생성 모드는 둘이다.

| 모드 | 형식 | 언제 |
|------|------|------|
| `monotonic`(기본) | 노드별 프로세스 전역 hex 카운터 | 단일/소규모, 비용 최소 |
| `global-unique` | UUIDv7 또는 ULID(시간 정렬 가능) | 대규모 fleet, 백엔드 전역 조인 |

> **한계(과장하지 않는다).** `flow_id`는 노드를 **넘어 전파**되므로, `monotonic` 모드는 서로 다른
> origin 노드의 id가 수집 백엔드에서 충돌한다(MFT §8이 corr에 대해 지적한 문제가 재발). `monotonic`은
> **단일 노드 또는 `label` 조인 전제에서만 유일**하다. fleet 전역 조인이 목적이면 `global-unique`를
> 써야 한다. 대안으로 `f-<label>-<hex>` 형식으로 origin label을 id에 내장할 수 있다.

`global-unique`는 흐름 진입점에서 **한 번만** 생성(create-if-absent)하므로 홉마다의 핫패스 비용은
없다. 와이어 비용은 흐름당 ~16–26B(§2.3 상한 내).

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
struct message_flow_event_t {
    // ... 기존 MFT §3 필드 ...
    std::optional<std::string> flow_id;      // ≤64B ASCII, create-if-absent
    // dispatch_error_event_t 에도 동일 flow_id 필드 추가 (§4.3)
};
```

언어별 투영: `.NET` `IZLinkMessageFlowControl`에 `FlowId`/생성 모드, Java `ZLinkMessageFlowEvent`에
`flowId`, Node flow 이벤트에 `flowId`. 케이싱만 변환하며 로그 토큰 `flow=`는 언어 간 바이트 동일
([runtime-metrics §4.0 / X-1](runtime-metrics.ko.md)).

## 9. 파싱 규약과 구현 상태

- `flow`/`corr`/`label` 조합으로 다중 노드 흐름을 하나로 잇는 파싱 규약은 [MFT §5.1](message-flow-tracing.ko.md)의
  구조화 필드 규약을 확장해 이 문서 부록이 소유한다(승격 시 부록 추가). 정규식 없이 구조화 key/value로
  조인한다.
- 언어별 구현 진행률과 gateway 자동 배선 현황은 [언어별 구현 차이](implementation-gap.ko.md)에
  기록한다.

## 10. 하위호환

- 세 확장 모두 **additive**다. `flow_id` 미사용 시 기존 corr-only 로그와 동일.
- `correlation_id`의 부여·전파·와이어 포맷(채널 envelope, 스트림 헤더 [MFT §9](message-flow-tracing.ko.md))은
  **불변**.
- `flow=` 필드·`has_flow_id` flag는 값/set일 때만 나타나 기존 파서·grep·구버전 노드를 깨지 않는다.

## 11. 회귀 테스트 매트릭스 (MFLOW-EXT)

| ID | 검증 |
|----|------|
| MFLOW-EXT-001 | `flow_id`가 STREAM inbound에서 create-if-absent 생성되어 actor relay·join_spot·room-spot dispatch까지 `flow=`로 이어짐 |
| MFLOW-EXT-002 | actor 노드 간 transfer commit에도 `flow_id`가 보존됨(DRAIN-003 교집합) |
| MFLOW-EXT-003 | `flow_id` 미부여 시 기존 corr-only 동작과 로그 동일(하위호환) |
| MFLOW-EXT-004 | `global-unique` 모드가 흐름당 1회 UUIDv7/ULID 생성, 홉마다 재생성 안 함 |
| MFLOW-EXT-005 | gateway 로거 자동 배선 후에도 MFLOW-001(off=완전 침묵) 유지(배선 ≠ 출력) |
| MFLOW-EXT-006 | `flow`/`corr`/`label` 조합 파싱 규약으로 다중 노드 흐름이 하나로 조인 |
| MFLOW-EXT-007 | `flow_id`가 채널 envelope·스트림 헤더 와이어 round-trip 보존, flag 미set 프레임은 구버전과 상호 호환 |
| MFLOW-EXT-008 | flow_id 미탑재 인바운드에서만 신규 생성(create-if-absent), 탑재 시 재생성 안 함 |
| MFLOW-EXT-009 | dispatch **error** 라인에도 flow=가 찍혀 `grep flow=<id>`로 성공·실패 함께 잡힘 |
| MFLOW-EXT-010 | publish fan-out에서 구독자 N개 라인이 같은 flow_id(owner skip 라인 포함) |
| MFLOW-EXT-011 | timer 발원 콜백이 새 flow를 `origin=timer`로 시작 |
| MFLOW-EXT-012 | `sample_rate<1`에서 flow 단위 일관 샘플링(한 flow 전부 남거나 전부 빠짐), dropped/에러는 항상 통과 |
| MFLOW-EXT-013 | 트레이싱 off 노드를 경유해도 `flow_id` **전파**는 유지(생성/기록만 게이트) |

## 12. 언어별 투영

| 언어 | 표면 |
|------|------|
| `.NET` | `IZLinkMessageFlowControl`에 `FlowId`·생성 모드; gateway 기본 sink 자동 배선 |
| Java/Kotlin | `ZLinkMessageFlowEvent`에 `flowId`; SLF4J 바인딩 기본 폴백 |
| Node | flow 이벤트에 `flowId`; NestJS 부트스트랩에서 gateway sink 자동 주입 |
| C++ (레퍼런스) | `message_flow_event_t`에 `flow_id`; gateway tracer 기본 배선 |

---

> 관련: [메시지 흐름 추적](message-flow-tracing.ko.md) · [runtime metrics](runtime-metrics.ko.md) ·
> [graceful drain & handoff](graceful-drain-handoff.ko.md) · [spot-actor](spot-actor.ko.md)
