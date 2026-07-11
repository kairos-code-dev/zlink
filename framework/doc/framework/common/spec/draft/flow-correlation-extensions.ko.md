<!-- draft-status: DRAFT · 제안 단계 · 공개 계약 아님 -->

[스펙 목차](../README.ko.md)

# 메시지 흐름 상관관계 확장 (Flow Correlation Extensions) — DRAFT

> **상태: DRAFT.** [메시지 흐름 추적](../message-flow-tracing.ko.md)이 스스로 명시한 세 이음매
> (§7 corr 키잉 한계 · §8 corr 비유일성 · §11 언어별 구현 차이)를 마감하기 위한 제안 초안이다.
> 기존 추적 계약을 **대체하지 않고 additive하게 확장**한다. correlation_id의 기존 의미는 그대로다.

이 문서는 "없는 기능을 새로 만드는" 것이 아니라, **이미 설계된 추적의 가장자리를 다듬는** 세
확장을 정의한다. 각 확장은 독립적으로 채택 가능하다.

## 1. 배경 — 세 이음매

[메시지 흐름 추적](../message-flow-tracing.ko.md)은 정직하게 세 한계를 적어 두었다.

1. **§7:** `correlation_id`는 channel/route/stream에서만 1급이고, spot 구독·actor·publish·
   join_spot은 `spot_rid`/`actor_id`로 키잉된다 → 한 흐름이 spot/actor 경계에서 corr로 관통되지
   않는다.
2. **§8:** `correlation_id`는 노드별 프로세스 전역 단조 카운터라 **전역 유일이 아니다** → 대규모
   fleet에서 `grep corr=X`가 서로 다른 메시지를 함께 잡을 수 있다.
3. **§11:** 언어별 구현 진행률이 다르고, stream/actor gateway는 로거 명시 주입을 빠뜨리면 그
   surface만 조용히 로그가 안 난다(§5 함정).

## 2. 확장 A — spot/actor 홉을 관통하는 flow-id

### 2.1 문제

예: 유저가 STREAM으로 `PlaceMark`를 보냄 → actor relay → room-spot handler 처리.
현재 corr은 **stream→actor relay 구간까지만** 이어지고, room-spot 내부 dispatch는 `spot_rid`로
키잉되어 같은 corr 라인이 끊긴다. 그래서 "이 한 수(move)가 룸에서 어떻게 처리됐나"를 **한 번의
grep으로 따라갈 수 없다.**

### 2.2 제안

**흐름 전체를 관통하는 `flow_id`를 추가**한다. correlation_id(전송계층 짝짓기용)는 그대로 두고,
`flow_id`는 **한 논리 흐름의 시작점에서 부여되어 spot/actor 경계를 넘어 전파**되는 상위 키다.

- **부여 지점** — 흐름의 진입점(STREAM inbound 또는 최초 channel request)이 `flow_id`를 만든다.
- **전파** — actor relay, join_spot, spot 간 이동(transfer), spot publish로 흐름이 이어질 때
  `flow_id`를 실어 나른다. 각 홉의 로그 라인에 `flow=<id>`를 추가한다.
- **조인 규약** — spot/actor 홉은 여전히 `spot_rid`/`actor_id`로도 키잉되지만, 그 라인에 `flow=`가
  함께 찍혀 **corr 구간과 spot/actor 구간이 하나의 `flow_id`로 이어진다.**

```
zlink flow: phase=received  ... corr=1a2b flow=f-9f3 actor=42
zlink flow: phase=sent      ... flow=f-9f3 spot=room-7      # actor→room-spot join
zlink flow: phase=received  ... flow=f-9f3 spot=room-7      # room-spot 내부 dispatch (corr 없어도 flow로 연결)
zlink flow: phase=replied   ... flow=f-9f3 spot=room-7
```

### 2.3 경계

- `flow_id`는 **선택 기능**이다. 부여하지 않으면 기존 corr-only 동작과 동일(하위호환).
- `flow_id`는 트레이싱 키일 뿐, dispatch 결정을 바꾸지 않는다(관측 계약 유지).

## 3. 확장 B — 전역 유일 flow-id 옵션

### 3.1 문제

`correlation_id`는 노드별 단조 카운터라 fleet 전체에서 유일하지 않다. `label=`(프로세스 키)로
구분하지만, 여러 노드 로그를 한 곳에 모으면 사람이 `label`+전파 경로로 손조인해야 한다.

### 3.2 제안

`flow_id` 생성 모드를 **두 가지**로 둔다.

| 모드 | 형식 | 언제 |
|------|------|------|
| `monotonic`(기본) | 노드별 프로세스 전역 hex 카운터(기존 corr과 동일 비용) | 단일/소규모, 비용 최소 |
| `global-unique` | UUIDv7 또는 ULID(시간 정렬 가능) | 대규모 fleet, 수집 백엔드에서 전역 조인 |

- `global-unique`는 흐름 진입점에서 **한 번만** 생성해 §2의 전파 규칙으로 흐른다. 홉마다 새로
  만들지 않는다(비용은 흐름당 1회).
- 수집 파이프라인 헬퍼: `label`+`corr`+`flow` 조합을 자동으로 하나의 흐름 키로 묶는 파싱 규약을
  문서로 제공한다(정규식 없이 구조화 필드로).

### 3.3 경계

전역 유일 ID는 흐름당 1회 생성이라 핫패스 홉 비용은 없다. `monotonic`이 기본이므로 원치 않으면
비용 증가가 없다.

## 4. 확장 C — 언어별 완성 및 함정 제거

### 4.1 구현 현황 채우기

`implementation-gap.ko.md`에 언어별 MFLOW / MFLOW-EXT 통과 현황 표를 유지한다. 게임 타깃 언어
(`C#`, `C++`)를 우선 완성한다.

### 4.2 §5 함정 제거 — gateway 로거 자동 주입

현재 channel/spot에는 dispatch 옵션이 자동 전파되지만 stream/actor gateway는 로거 명시 주입이
필요하고, 빠뜨리면 그 surface만 조용히 로그가 안 난다.

**제안:** framework 부트스트랩에서 stream/actor gateway에도 **기본 로거 sink를 자동 배선**한다.
명시 주입이 있으면 그것을 우선하되, 없을 때 침묵 대신 기본 sink로 폴백한다. "조용한 무로그"를
기본 동작에서 제거한다.

## 5. 하위호환

- 세 확장 모두 **additive**다. `flow_id` 미사용 시 기존 corr-only 로그와 동일.
- `correlation_id`의 부여·전파·와이어 포맷(채널 envelope, 스트림 헤더 §9)은 **불변**.
- `flow=` 필드는 값이 있을 때만 출력되어, 기존 파서/grep 규약을 깨지 않는다.

## 6. 회귀 테스트 매트릭스 (MFLOW-EXT)

| ID | 검증 |
|----|------|
| MFLOW-EXT-001 | `flow_id`가 STREAM inbound에서 부여되어 actor relay·join_spot·room-spot dispatch까지 `flow=`로 이어짐 |
| MFLOW-EXT-002 | actor 노드 간 이동(transfer)에도 `flow_id`가 보존됨 |
| MFLOW-EXT-003 | `flow_id` 미부여 시 기존 corr-only 동작과 로그가 동일(하위호환) |
| MFLOW-EXT-004 | `global-unique` 모드가 흐름당 1회 UUIDv7/ULID를 생성하고 홉마다 재생성하지 않음 |
| MFLOW-EXT-005 | stream/actor gateway가 로거 명시 주입 없이도 기본 sink로 로그를 냄(§4.2) |
| MFLOW-EXT-006 | `flow`/`corr`/`label` 조합 파싱 규약으로 다중 노드 흐름이 하나로 조인됨 |

## 7. 언어별 투영

| 언어 | 표면 |
|------|------|
| `.NET` | `IZLinkMessageFlowControl`에 `FlowId` 필드/모드 추가; gateway 기본 sink 자동 배선 |
| Java/Kotlin | `ZLinkMessageFlowEvent`에 `flowId`; SLF4J 바인딩 기본 폴백 |
| Node | flow 이벤트에 `flowId`; NestJS 부트스트랩에서 gateway sink 자동 주입 |
| C++ (레퍼런스) | `message_flow_event_t`에 `std::optional<std::string> flow_id`; gateway tracer 기본 배선 |

---
<!-- draft-status: DRAFT · 제안 단계 · 공개 계약 아님 -->
