# Spot 주소 메시징 — 공통 스펙

[스펙 목차](../README.ko.md) · [SPOT 메시징](20-spot-messaging.ko.md) ·
[MeshNode](21-mesh-node.ko.md) · [Location runtime](40-location-runtime.ko.md)

## 1. 범위

이 문서는 ZLink Framework 10.0.0에서 논리 Spot identity를 위치 투명하게 resolve하고 직접 호출하는
공통 공개 계약을 정의한다. 이 문서는 “Spot의 owner MeshNode가 바뀌어도 호출자가 transport 주소를
관리하지 않고 어떻게 같은 논리 Spot을 호출하는가?”라는 질문에 답한다.

location row, lease, event와 resolver backend는 [40 Location runtime](40-location-runtime.ko.md),
Spot handler와 실행 순서는 [20 SPOT 메시징](20-spot-messaging.ko.md), MeshName의 물리 경계는
[21 MeshNode](21-mesh-node.ko.md)가 소유한다. 이 문서는 언어별 타입 이름과 시그니처를 정하지 않는다.

## 2. SpotHandle

SpotHandle은 하나의 `(MeshName, Spot RID)` 논리 identity와 현재 owner route snapshot을 보유하는 불투명한
capability다. 공개 정보는 논리 identity이며 owner RID, endpoint, lifecycle generation과 lease는
Framework가 관리한다.

- handle은 resolver가 선택한 MeshName을 바꾸지 않는다.
- owner route snapshot은 location event 또는 resolver refresh로 원자적으로 교체할 수 있다.
- 같은 논리 Spot이 유효한 새 generation으로 활성화되면 갱신된 handle은 새 owner를 가리킬 수 있다.
- application은 owner RID와 Spot RID를 분리해서 전송 인자로 다시 조립하지 않는다.
- handle은 동시 호출에 안전해야 하며 호출자에게 background listener 수명 관리를 요구하지 않는다.

특정 activation에만 적용해야 하는 업무 operation은 payload에 domain generation 또는 idempotency key를
포함해야 한다. SpotHandle 자체는 특정 activation만을 고정하는 lock이 아니다.

## 3. Resolve

Spot resolver는 MeshName과 Spot RID로 SpotHandle을 반환한다. Actor 위치 resolver가 필요한 경우에는
MeshName과 Actor identity로 현재 Spot의 SpotHandle을 반환한다. 두 resolver는 유효한 owner lease와
generation을 확인하고, 대상이 없거나 유효하지 않으면 not-found 결과를 반환한다.

정상 send/request는 handle의 in-memory route snapshot을 사용한다. 매 호출마다 location store를 읽지
않는다. location event, runtime이 관리하는 refresh와 안전한 stale 실패만 snapshot 갱신을 시작할 수
있다. 정확한 refresh 및 cache 규칙은 [40 Location runtime](40-location-runtime.ko.md)이 정한다.

## 4. Direct send와 request

Spot direct 호출은 SpotHandle과 typed payload만 받는다. Framework는 handle의 MeshName에 대응하는 local
MeshNode를 선택하고 owner MeshNode로 route한 뒤 target Spot application queue에 제출한다.

- local과 remote target은 같은 handler, metadata와 completion 의미를 가진다.
- handle의 MeshName과 선택한 MeshNode의 MeshName이 다르면 호출 전에 실패한다.
- 서로 다른 MeshName 사이에 자동 relay나 fallback을 수행하지 않는다.
- Logical Multicast는 SpotHandle을 사용하지 않으며 ChannelName과 topic으로 대상을 정한다.

payload와 metadata ownership은 [03 메시지 모델](../03-message-model.ko.md), submit과 request completion은
[04 비동기 실행 정책](../04-async-execution-policy.ko.md)이 소유한다.

## 5. Stale route

owner 이동이나 Spot 재활성으로 handle의 route snapshot이 오래될 수 있다. 실패 처리는 handler 실행
여부를 확인할 수 있는지에 따라 다르다.

| 호출 | Framework 동작 |
|---|---|
| request | handler가 실행되지 않았음이 명확한 stale-target 응답이면 route를 한 번 refresh하고 한 번 다시 제출할 수 있다. timeout, cancellation 또는 실행 여부가 불명확한 실패에는 다시 제출하지 않는다. |
| one-way send | 현재 snapshot으로 한 번 submit한다. 숨은 request를 만들거나 자동으로 다시 제출하지 않는다. 이후 location 갱신은 다음 호출부터 적용한다. |

한 번의 refresh 뒤에도 target이 없거나 route가 유효하지 않으면 typed target 오류로 끝난다. 일반 업무
retry와 deduplication은 application 정책이다.

Spot 이동 경계에서는 이전 owner로 이미 제출한 one-way 메시지와 새 owner로 제출한 메시지 사이의 전역
순서를 보장하지 않는다. 순서가 중요한 업무는 domain generation, idempotency와 reconcile 규칙을
사용한다.

## 6. 실패와 종료

- local process에 handle의 MeshName과 일치하는 MeshNode가 없으면 구성 오류다.
- owner RID가 member가 아니거나 Spot generation이 맞지 않으면 stale 또는 target-not-found 오류다.
- 알려진 owner의 route가 제한 시간 안에 ready가 되지 않으면 route-not-connected 또는 timeout으로
  끝난다.
- target Spot이 drain 또는 종료 상태이면 신규 admission을 거부한다.
- request 실패를 다른 Spot RID나 다른 MeshName으로 우회하지 않는다.

오류 이름과 언어별 반환 형태는 언어별 공개 인터페이스 문서가 정한다.

## 7. 관측과 검증

관측 정보는 MeshName, resolve 결과, route snapshot generation, refresh 원인과 횟수, submit 결과와 stale
분류를 구분해야 한다. Spot RID는 metric label로 사용하지 않는다.

다음 조건을 검증한다.

- 호출자가 owner RID나 endpoint를 관리하지 않고 SpotHandle만 사용한다.
- handle의 MeshName과 다른 MeshNode로 direct 호출하지 않는다.
- 정상 호출이 location store를 매번 조회하지 않는다.
- handler 실행 여부가 불명확한 request와 모든 one-way send를 자동으로 다시 제출하지 않는다.
- 안전한 request refresh가 한 번으로 제한된다.
- owner 변경 뒤 갱신된 handle이 새 generation의 target Spot으로 전달한다.
