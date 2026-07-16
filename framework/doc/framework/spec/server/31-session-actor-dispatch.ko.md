# Session Actor Dispatch — 공통 스펙

[스펙 목차](../README.ko.md) · [STREAM 서버 세션](30-stream-session.ko.md) ·
[MeshNode](21-mesh-node.ko.md) · [Actor 모델](22-actor-model.ko.md)

## 1. 범위

이 문서는 ZLink Framework 10.0.0에서 STREAM session과 MeshNode Actor runtime을 연결하는 공통 공개
계약을 정의한다. 이 문서는 “STREAM session이 어느 MeshName의 Actor와 통신하는지 명확히 고정하면서
typed payload와 reply를 transport 세부 정보 없이 어떻게 전달하는가?”라는 질문에 답한다.

STREAM 연결, packet session과 session lifecycle은
[30 STREAM 서버 세션](30-stream-session.ko.md), Actor identity와 queue는
[22 Actor 모델](22-actor-model.ko.md), MeshName과 route는 [21 MeshNode](21-mesh-node.ko.md)가 소유한다.
payload·metadata와 request correlation은 [03 메시지 모델](../03-message-model.ko.md), callback 실행과
completion은 [04 비동기 실행 정책](../04-async-execution-policy.ko.md)이 소유한다.

## 2. 명시적인 MeshName 경계

Actor dispatch를 사용하는 STREAM node는 등록할 때 target MeshName 하나를 명시하고, 같은 process에서
그 이름으로 등록된 MeshNode 하나에 연결한다. endpoint, 첫 번째 MeshNode 또는 ActorRef에서 이 값을
암묵적으로 추론하지 않는다.

| 구성 | 계약 |
|---|---|
| Actor dispatch를 사용하는 STREAM node | 정확히 하나의 MeshName과 local MeshNode를 선택한다. |
| Actor dispatch를 사용하지 않는 STREAM node | MeshNode를 요구하지 않는다. |
| 한 process의 여러 STREAM node | 각각 MeshName을 명시하며 같은 MeshNode 또는 서로 다른 MeshNode를 선택할 수 있다. |
| 한 process의 여러 MeshNode | MeshName으로 구분하며 session registration 사이에 공유 상태를 만들지 않는다. |

session이 resolve, create 또는 bind하는 ActorRef는 STREAM node에 설정한 MeshName context에서만 사용한다.
ActorRef의 owner route가 다른 mesh에 속하면 bind 또는 dispatch 전에 target 오류로 실패한다. 서로 다른 MeshName 사이에
자동 relay, fallback 또는 route 변환을 제공하지 않는다.

## 3. 공개 흐름

Session Actor Dispatch는 다음 순서의 typed operation을 제공한다.

1. STREAM session callback이 client를 인증하고 domain Actor identity와 type을 결정한다.
2. 같은 MeshName에서 ActorRef를 resolve하거나 정책에 따라 Actor를 생성한다.
3. session과 Actor를 binding token으로 bind한다.
4. session handler가 선택한 typed payload를 Actor dispatch에 제출한다.
5. Actor handler는 request reply를 반환하거나 현재 bound session으로 one-way push를 보낸다.

application은 session object, ActorRef, typed payload, typed reply와 bound-session 표면만 사용한다.
MeshNode RID, STREAM transport handle, raw relay envelope, original request sequence와 endpoint를 조립하거나
보관하지 않는다.

Actor 생성 여부, Actor type 선택, 재사용과 인증은 application 정책이다. route 선택, request correlation,
binding token 검증과 typed dispatch는 Framework가 맡는다.

## 4. Inbound Actor dispatch

STREAM packet은 먼저 session의 typed handler registry로 dispatch된다. session handler가 Actor dispatch를
선택하면 Framework는 원래 session request correlation을 내부에 보존하고 ActorRef가 가리키는 owner
MeshNode로 payload를 제출한다.

수신 payload는 local 또는 remote 여부와 관계없이 target Actor application queue에 직접 들어간다.
Actor의 현재 Spot은 location과 membership 검증에 사용할 수 있지만 dispatch callback을 고르는 실행
문맥이 아니다.

- Actor payload를 Spot callback이나 Spot application queue에 넣지 않는다.
- session callback thread에서 Actor handler를 직접 실행하지 않는다.
- 같은 Actor에 수락된 session payload는 다른 Actor ingress와 함께 Actor queue 순서를 따른다.
- 서로 다른 Actor는 하나의 session 또는 Spot queue 때문에 서로 기다리지 않는다.

Actor join·leave와 lifecycle 작업이 필요하면 별도 control operation으로 제출한다. 해당 작업만 target
Spot control claim을 사용하며 업무 payload의 reply path와 합치지 않는다.

## 5. Request reply

session request를 Actor request로 dispatch하면 Framework는 client request correlation과 Actor request
completion을 연결한다. Actor handler의 typed reply 또는 typed error는 원래 STREAM session request의
terminal result 하나로 반환한다.

- Actor handler는 원본 STREAM header나 request sequence를 읽거나 수정하지 않는다.
- timeout, cancellation 또는 실행 여부가 불명확한 route 실패 뒤 다른 Actor나 MeshNode로 자동
  재전송하지 않는다.
- session이 닫히면 늦게 도착한 Actor reply를 새 session이나 새 binding으로 보내지 않는다.
- one-way session payload는 client reply를 만들지 않으며 수락 의미는
  [04 비동기 실행 정책](../04-async-execution-policy.ko.md)을 따른다.

## 6. Binding과 bound session

binding은 `(MeshName context, Actor identity, STREAM session, binding token)`의 runtime 관계다. 한 Actor는 동시에
하나의 유효한 session binding을 가지며 한 session은 여러 Actor를 bind할 수 있다.

rebind는 새 token을 발급하고 이전 token을 무효화한다. 이전 session에서 늦게 도착한 dispatch, reply,
push와 close operation은 현재 binding에 적용하지 않는다. binding token과 session route는 Framework
내부 상태이며 application이 별도 location store로 관리하지 않는다.

Actor handler의 bound-session 표면은 현재 유효한 client session으로 one-way push를 보내고 연결 종료를
요청하는 기능만 제공한다. 임의 session RID를 받는 전역 proxy나 Actor에서 client로 보내는 별도
request/reply 채널을 제공하지 않는다.

STREAM disconnect는 해당 session의 binding을 해제한다. disconnect만으로 Actor를 종료하거나 Actor의
Spot membership을 바꾸지 않는다. application에 disconnect notification이 필요하면 session lifecycle 또는
명시적인 Actor control message를 사용한다.

## 7. 실행 순서와 progress

같은 STREAM session의 callback 순서는 [30 STREAM 서버 세션](30-stream-session.ko.md)이 정한다. Actor에
제출된 뒤에는 Actor queue가 해당 Actor의 순서를 소유한다. session turn과 Actor turn을 하나의 공유 lock
또는 callback stack으로 합치지 않는다.

request completion, send-ready, binding update, disconnect cleanup과 transfer barrier는 infrastructure
claim에서 진행한다. session 또는 Actor application callback이 비동기 작업을 기다리는 동안에도 이러한
작업이 막히지 않아야 한다.

Actor transfer 중 session dispatch의 admission, barrier와 owner 변경은
[23 Spot Actor](23-spot-actor.ko.md)가 정한다. transfer 전후의 payload를 Spot callback으로 우회해서
순서를 맞추지 않는다.

## 8. Metadata

Session Actor Dispatch는 [03 메시지 모델](../03-message-model.ko.md)의 immutable metadata snapshot과
forwarding policy를 사용한다. session-local transport 정보와 request correlation은 Framework 내부에
두고, 허용된 application metadata만 Actor handler context에 전달한다.

Actor reply는 request metadata를 자동 복사하지 않는다. metadata key, 크기, ownership과 allowlist 규칙은
[03 메시지 모델](../03-message-model.ko.md)이 소유하며 이 문서에서 다시 정의하지 않는다.

## 9. 등록과 startup 검증

Actor dispatch registration은 STREAM node, MeshName과 local MeshNode의 관계를 host 시작 전에 검증한다.

| 조건 | 결과 |
|---|---|
| MeshName이 비어 있음 | 설정 오류 |
| 같은 MeshName의 local MeshNode가 없음 | 설정 오류 |
| STREAM node의 Actor dispatch MeshName을 둘 이상 지정 | 설정 오류 |
| ActorRef owner route가 STREAM node의 MeshName에 속하지 않음 | bind 또는 dispatch 오류 |
| 같은 session packet key에 handler를 중복 등록 | 설정 오류 |
| Actor type에 factory가 없음 | create 요청 오류 |
| current binding 없이 bound-session push 또는 close 요청 | session-not-bound 오류 |

언어별 정확한 등록·handler·binding 시그니처와 오류 타입은 언어별 공개 인터페이스 문서가 정한다.

## 10. Drain과 실패

- drain 중인 STREAM node는 신규 session을 받지 않고 기존 session callback과 pending reply를 deadline까지
  처리한다.
- drain 중인 MeshNode는 신규 Actor 생성과 신규 binding 배정을 거부하고 이미 수락한 Actor dispatch와
  binding cleanup을 진행한다.
- route target, Actor generation 또는 binding token 검증에 실패하면 typed 오류로 끝내고 다른 mesh나
  session으로 우회하지 않는다.
- one-way dispatch 뒤 발생한 handler 오류는 session request로 바꾸지 않고 runtime 관측 경로에 기록한다.

전체 종료 순서는 [54 Graceful Drain](54-graceful-drain-handoff.ko.md)이 소유한다.

## 11. 관측과 검증

관측 정보는 STREAM node, MeshName, MeshNode RID, session state, Actor dispatch admission, binding generation,
reply correlation 결과와 drain state를 구분해야 한다. session ID와 Actor ID는 metric label로 사용하지
않는다.

다음 조건을 검증한다.

- Actor dispatch STREAM node가 MeshName과 local MeshNode를 명시적으로 선택한다.
- 서로 다른 MeshName의 ActorRef를 bind하거나 dispatch할 수 없다.
- STREAM-only 구성은 Actor dispatch를 사용하지 않으면 MeshNode를 요구하지 않는다.
- local·remote session payload가 모두 Actor queue로 직접 전달된다.
- Actor payload가 Spot callback과 Spot application queue를 거치지 않는다.
- join·leave와 lifecycle control만 Spot control claim을 사용한다.
- rebind 뒤 이전 binding token의 reply, push와 close가 새 session에 적용되지 않는다.
- request reply가 원래 STREAM correlation으로 한 번만 완료된다.
