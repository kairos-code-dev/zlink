# Stage Wrapper On SPOT — 공통 스펙

[스펙 목차](README.ko.md) | [이전: SpotHandle 기반 메시징](24-spot-address-messaging.ko.md) | [다음: STREAM 서버 세션](30-stream-session.ko.md)

> 이 문서는 **SPOT 위에 상위 실행 모델(stage·room·zone)을 얹는 계약의 언어 중립 정본**이다.
> 어떤 실행 보장에 기대어도 되는지, 무엇이 framework의 책임이고 무엇이 wrapper의 책임인지를
> 소유한다.
>
> 언어별 타입과 시그니처는 `languages/<lang>/stage-wrapper-on-spot.ko.md`가 고정한다.

## 1. 목적

`SPOT`([spot-actor](23-spot-actor.ko.md), [framework API](05-framework-api.ko.md))은 주소를 가질 수
있는 논리 인스턴스까지를 설명한다. 그 위에 room·stage·zone 같은 **상위 실행 모델**을 올리려면
계약이 한 단계 더 필요하다.

stage 성격의 모델은 보통 다음을 함께 가진다.

- 단일 실행 문맥
- 생성 시점의 초기 payload
- actor 또는 session membership
- tick·timer 기반 후속 작업
- id로 위치를 찾는 lookup

**stage wrapper는 SPOT을 그대로 노출하는 일이 아니다. SPOT 위에 한 단계 높은 실행 모델을 세우는
일이다.**

## 2. 책임 경계

| 축 | 소유 |
|---|---|
| SpotNode 등록과 lifecycle | **framework** |
| spot rid 생성·삭제 | **framework** |
| channel publish/subscribe, route bridge send/request | **framework** |
| timer 등록과 취소 | **framework** |
| DI·handler·filter·context | **framework** |
| **같은 spot의 dispatch 직렬화** | **framework**(§3) |
| actor join 등록, actor stream 연결·해제, actor dispatch | **framework**([spot-actor](23-spot-actor.ko.md), [session-actor-dispatch](31-session-actor-dispatch.ko.md)) |
| membership 정책(입장·퇴장·인증·권한) | **wrapper / 응용** |
| room·stage·zone 별 broadcast 정책 | **wrapper / 응용** |
| `stageId → 위치` lookup helper | **wrapper / 응용** |

**framework는 stage를 알지 못한다.** stage wrapper는 framework의 public 표면만으로 만들 수 있어야
한다. 이것이 이 계약의 적합성 기준이다.

## 3. 실행 문맥 계약

**이 절이 stage wrapper를 가능하게 하는 핵심이다.** 상위 모델이 상태를 안전하게 소유하려면
"같은 spot 상태는 같은 실행 계약으로 처리된다"가 보장되어야 한다.

framework는 다음을 보장한다.

- **같은 spot의 dispatch callback은 하나의 실행 줄에서 직렬화된다.** 두 callback 본문이 동시에
  실행되지 않는다.
- routed packet, subscription, **channel reply**, timer가 **모두 같은 dispatch 축**으로 올라온다.
- channel request의 reply continuation도 **request를 시작한 spot의 실행 문맥**에서 실행된다.
  임의의 thread에서 완료를 resolve하지 않는다.

따라서 다음 경로에서 spot 상태를 읽고 쓸 때 **별도 lock이 필요하지 않다.**

- routed packet handler
- subscription handler
- timer handler
- channel reply continuation
- join이 끝난 actor의 packet handler
- actor session disconnect의 후속 처리

**이 보장은 `async` 대기를 가로질러서도 유지된다.** `async`는 실행 줄의 turn을 잡은 채 완료를
기다리므로, handler는 await를 가로질러도 하나의 turn이다. stage wrapper가 상태를 읽고, 외부에
요청하고, 결과로 상태를 바꾸는 흐름을 lock 없이 쓸 수 있다.

**`yield`로 기다리는 구간만 예외다.** `yield`는 turn을 반납하므로 그 대기 중에 같은 spot의 다른
callback이 실행되어 상태를 바꿀 수 있다([04 §1.1](04-async-execution-policy.ko.md)). stage
wrapper가 `yield`를 가로질러 유지해야 하는 불변식이 있으면 상태 전이를 await 이후로 모으거나
`async`를 쓴다. **`yield`는 spot 공유 상태와 무관한 대기에만 쓴다.**

**예외 두 가지:**

- **stream session callback은 spot 상태를 직접 만지지 않는다.** session callback은 actor
  dispatch나 spot 호출을 제출하는 데까지만 책임진다. 실제 상태 변경은 spot 실행 문맥 안에서
  일어난다.
- **wrapper 바깥에서 spot rid를 받아 상태를 직접 건드리는 접근**은 같은 실행 계약 바깥이므로
  별도 동기화가 필요하다.

사용자에게 내부 실행기(mailbox·queue·drain loop)를 노출하지 않는다. 사용자가 보는 것은 **등록
표면**뿐이다.

### 3.1 actor join 이후의 처리 모델

"actor가 spot에 join한다"는 말은 membership table에 row가 생긴다는 뜻만이 아니다.
**그 actor의 packet 처리 ownership이 해당 spot으로 넘어간다**는 뜻이다.

```text
joined actor session packet
    -> session packet으로 정규화
    -> spot이 소유한 inbox로 제출
    -> 단일 spot consumer
    -> actor packet handler
    -> actor가 spot 상태에 접근
```

1. session이 인증을 마치고 actor를 확보한다.
2. join 요청이 오면 framework가 **target spot의 실행 문맥으로** join을 넣는다.
3. join handler가 성공하면 framework가 `actor → spot runtime` 연결을 membership으로 기록한다.
4. 이후 session packet은 header와 payload를 보존한 **actor dispatch로 정규화**된다.
5. 정규화된 dispatch는 그 actor가 attach된 **spot runtime의 inbox**로 들어간다.
6. **그 inbox를 소비하는 실행기는 하나뿐이다.**
7. 그 실행기 안에서만 actor handler가 호출된다.

**못 박는 최소 의미:**

- join된 actor의 packet을 **spot 실행 문맥 바깥에서 직접 처리하지 않는다.**
- **actor packet 처리를 session callback thread에서 곧장 실행하지 않는다.** session callback은
  actor가 어느 spot에 속하는지 확인하고 넘기는 데까지만 책임진다.
- 내부 구현이 mailbox든 queue든 fiber든 상관없다. 위 의미만 지키면 된다.

## 4. Timer

stage 성격의 모델에서 timer는 사실상 필수다(매칭 후 지연 시작, 무입력 시 종료, 주기적 state
flush, heartbeat publish).

**timer는 spot lifecycle 안에서 등록하는 하나의 모델로 통일한다.** 별도 갈래를 만들지 않는다.

- **user spot timer의 tick은 그 spot의 실행 문맥으로 enqueue된다.** 권위 상태를 바꾸는 작업은 이
  직렬 문맥 안에서 처리한다.
- **Entry Spot timer**는 lifecycle·route·subscription과 **같은 Entry 실행 줄**에 enqueue한다.
  Entry actor packet만 actor별 mailbox에서 별도로 처리한다.
- timer handler는 tick 정보(callback 번호, fixed-rate tick 번호, 예정 시각, 지연, 건너뛴 tick
  수)를 볼 수 있다.
- **overrun 정책**으로 늦은 tick을 건너뛸지, 제한된 수만 catch-up할지, handler 완료 후 period를
  다시 기다릴지 정한다.
- **wrapper는 native timer handle을 노출하지 않는다.** wrapper가 timer를 감싸더라도 tick
  metadata·overrun 정책·handler 예외 관측을 숨기지 말고 wrapper option으로 다시 사상한다.

### 4.1 Timer 등록 검증

다음은 **host 시작 또는 등록 시점에 설정 오류로 실패한다.**

| 조건 | 결과 |
|---|---|
| **timer 이름이 비어 있다** | 설정 오류 |
| **period가 0 이하** | 설정 오류 |
| **catch-up 상한이 0 이하**(제한된 catch-up 정책을 쓸 때) | 설정 오류 |
| **지원하지 않는 overrun 정책** | 설정 오류 |


## 5. 생성 시 초기값 전달

stage는 생성 시점에 초기 payload가 필요하다(어느 node에 만들지, stage type, stage id, create
payload).

**framework 기본 계약이 제공하는 것:**

- spot 타입으로 factory를 고르는 **생성**
- 타입 + spot rid로 기존 logical spot을 **확보**
- 생성·확보 **모두** DTO 또는 message request payload를 받는 표면
- spot 쪽의 **생성 callback**이 그 request를 받는다

**wrapper 확장 후보**(framework 기본 계약이 아님): `타입 + spot rid + typed metadata` 형태의
생성 표면. 하부 C API의 공개 계약에서 바로 읽히는 내용이 아니므로 **framework 기본 계약으로
승격하지 않는다.**

## 6. Membership과 directory

**둘 다 framework 범위 밖이며 wrapper 또는 응용이 소유한다.**

- membership — 누가 입장·퇴장했는가, 현재 actor 집합, 특정 actor에게만 보낼지 전체에
  broadcast할지
- directory — `stageId → 위치` lookup, `logical key → channel/node/spot` 해석

**이 기능을 SPOT 공용 API에 넣지 않는다.** 다만 wrapper 계약은 위치 해석 책임이 어디에 있는지를
분명히 고정한다.

## 7. 언어별 요구 사항

언어별 문서는 이 계약을 **자기 언어의 타입으로 표현**하기만 한다. 새 의미를 만들지 않는다.

wrapper의 public 표면에는 application이 직접 다루는 개념만 둔다 — spot rid, node rid, handler
registry view, timer option, outbound channel client, 도메인 상태와 메서드. **spot activation
state, timer token, outbound transport, packet dispatcher 같은 runtime 내부 타입을 public
멤버로 노출하지 않는다.**

| 언어 | wrapper 타입 |
|---|---|
| 모든 언어 | **framework가 제공하는 별도 `Stage` 타입은 없다.** 사용자가 만든 도메인 객체가 spot 계약을 구현한다 |

## 8. 회귀 테스트

framework가 stage를 알지 못해도 상위 모델을 얹을 수 있는지를 확인한다.

| 항목 | 검증 |
|---|---|
| 실행 문맥 | actor join 뒤 stage 역할의 spot에서 packet이 lifecycle 순서대로 처리된다 |
| timer 수명 | stage tick timer가 spot 종료 뒤 추가 callback을 만들지 않는다 |
| public 표면만으로 구성 | application stage wrapper가 spot request·timer·lifecycle을 **public API만으로** 실행한다 |
