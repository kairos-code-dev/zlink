# Stage Wrapper On SPOT — 공통 스펙

[스펙 목차](../README.ko.md) · [SPOT 메시징](20-spot-messaging.ko.md) ·
[Actor 모델](22-actor-model.ko.md) · [Spot 주소 메시징](24-spot-address-messaging.ko.md)

## 1. 범위

이 문서는 ZLink Framework 10.0.0의 public Spot 계약 위에 room·stage·zone 같은 상위 실행 모델을 만드는
공통 계약을 정의한다. 이 문서는 “Stage wrapper가 Spot 소유 상태를 한 turn에서 안전하게 유지하면서
Actor의 독립 실행 경계를 어떻게 보존하는가?”라는 질문에 답한다.

Framework는 별도 Stage runtime이나 공통 Stage base type을 제공하지 않는다. application의 domain
wrapper가 Spot의 public 등록·메시징·timer·lifecycle 표면을 조합한다. 언어별 wrapper 형태는 각 언어의
공개 인터페이스 문서가 정한다.

## 2. 책임 경계

| 책임 | 소유자 |
|---|---|
| Spot identity, 생성·종료와 application turn | Framework Spot runtime |
| Spot direct와 Logical Multicast dispatch | Framework Spot runtime |
| timer admission과 callback turn | Framework Spot runtime |
| Actor queue와 Actor 업무 handler | Framework Actor runtime |
| Actor join·leave와 lifecycle control | Framework Spot·Actor control claim |
| 입장 권한, stage state, membership 정책과 broadcast 내용 | Stage wrapper 또는 application |
| domain key에서 SpotHandle을 찾는 정책 | Stage wrapper와 location service |

Stage wrapper는 transport RID, endpoint, internal queue, native timer handle과 message storage reference를
public surface에 노출하지 않는다.

## 3. Spot turn 보존

Stage가 소유하는 상태를 읽거나 바꾸는 callback은 target Spot의 application turn에서 실행해야 한다.

- Spot direct handler
- Logical Multicast subscription handler
- Spot timer callback
- Actor join·leave와 lifecycle control callback
- Stage wrapper가 명시적으로 Spot에 제출한 domain operation

같은 Spot에서 수락된 위 작업은 하나씩 실행되며 callback 두 개가 동시에 Spot 상태를 변경하지 않는다.
callback이 비동기 작업을 기다리는 동안 turn을 유지하거나 반납하는 의미는
[04 비동기 실행 정책](../04-async-execution-policy.ko.md)이 정한다. wrapper는 별도 scheduler나 lock 규칙으로
그 계약을 바꾸지 않는다.

request reply continuation이 Spot 상태를 바꾸면 원래 Spot turn으로 다시 제출되어야 한다. transport 또는
completion thread에서 Spot 상태를 직접 변경하지 않는다.

## 4. Actor 경계

Actor가 Stage 역할의 Spot에 join해도 Actor 업무 payload는 Actor queue로 직접 전달된다. Actor payload를
Spot callback으로 변환하거나 Spot application queue에 넣지 않는다. 따라서 Actor handler는 Stage의
mutable state를 직접 참조하지 않는다.

Actor가 Stage state를 바꾸려면 Stage Spot으로 명시적인 send/request를 제출한다. 해당 handler가 Spot
turn에서 membership, score, world state와 broadcast 결정을 수행한다. 이 경계는 여러 Actor가 같은 Stage에
속해도 Actor의 독립적인 payload 처리와 Stage state의 단일-writer 의미를 함께 유지한다.

Spot control claim이 받는 Actor 관련 작업은 join·leave·transfer와 lifecycle notification뿐이다. 업무
payload와 control 작업을 같은 callback namespace로 합치지 않는다. 자세한 Actor queue 및 control 계약은
[22 Actor 모델](22-actor-model.ko.md)이 소유한다.

## 5. Timer

Stage timer는 Spot lifecycle 안에서 등록하고 tick을 같은 Spot application turn에 제출한다. timer callback은
Spot direct, Logical Multicast와 다른 timer callback에 대해 같은 직렬성 계약을 가진다.

- Spot 종료가 신규 timer tick admission을 닫는다.
- 이미 수락한 tick과 종료 callback의 순서는 Spot lifecycle 규칙으로 정한다.
- fixed-rate, delay, catch-up과 overrun option은 언어별 timer 공개 계약으로 표현한다.
- wrapper는 native handle이나 scheduler thread를 application에 노출하지 않는다.

## 6. 생성과 membership

Stage wrapper는 Spot factory에 domain 생성 payload를 전달하고 생성 callback 안에서 초기 Stage state를
만든다. 동일한 논리 Stage의 중복 생성, admission 권한과 재활성 정책은 domain 규칙으로 결정한다.

Actor join은 Spot control claim에서 Stage membership 정책을 검사한다. 성공한 membership은 Actor의 현재
Spot 위치와 Stage가 소유한 member state를 일관되게 갱신한다. transaction, fencing과 transfer barrier는
[23 Spot Actor](23-spot-actor.ko.md)가 소유한다.

Stage 전체 알림은 다음 중 의미에 맞는 경로를 사용한다.

- 같은 ChannelName의 여러 Spot에 알릴 때는 Logical Multicast를 사용한다.
- Stage 하나의 member state를 기준으로 알릴 때는 Spot turn에서 대상 Actor 또는 bound session을 고르고
  명시적인 메시지를 제출한다.

Logical Multicast를 Stage member 목록의 durable source로 사용하지 않는다.

## 7. Location과 수명

외부 service는 domain key를 SpotHandle로 resolve한 뒤 Stage Spot에 직접 호출한다. owner RID와 endpoint는
wrapper 상태에 저장하지 않는다. 위치 갱신과 stale route의 의미는
[24 Spot 주소 메시징](24-spot-address-messaging.ko.md)이 정한다.

Stage 종료는 신규 application admission과 신규 join을 닫고, 이미 수락한 Spot turn과 membership 정리를
drain deadline 안에서 완료한다. 종료 뒤의 timer, subscription과 direct 메시지는 Stage callback을 새로
만들지 않는다.

## 8. Metadata와 관측

Stage wrapper는 [03 메시지 모델](../03-message-model.ko.md)의 immutable metadata snapshot을 handler에
그대로 제공하고 transport frame이나 storage ownership을 해석하지 않는다.

관측 정보는 MeshName, Stage type, Spot turn backlog, timer 지연, membership control 결과와 종료 state를
구분해야 한다. Stage ID와 Actor ID는 metric label로 사용하지 않는다.

## 9. 검증 요구

- Spot direct, Logical Multicast, timer와 explicit Stage operation이 같은 Spot turn을 보존한다.
- Actor payload가 Stage Spot callback이나 Spot application queue를 거치지 않는다.
- Actor handler가 Stage state를 바꿀 때 명시적인 Spot 호출을 사용한다.
- join·leave와 lifecycle control만 Spot control claim에 들어간다.
- request continuation이 transport thread에서 Stage state를 직접 변경하지 않는다.
- Stage wrapper가 Framework의 public Spot·Actor·timer·location 표면만 사용한다.
- Spot 종료 뒤 신규 timer와 message callback이 실행되지 않는다.
