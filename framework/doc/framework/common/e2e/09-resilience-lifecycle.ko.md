<!-- framework-adapter-nav:start -->
[이전: 샘플 기반 업무 흐름](08-sample-derived-flows.ko.md) | [E2E 목차](README.ko.md)
<!-- framework-adapter-nav:end -->

# 복구와 Lifecycle E2E

이 문서는 process restart, reconnect, cancellation, shutdown, resource cleanup을 검증한다.
정상 기능이 통과해도 lifecycle이 깨지면 운영 환경에서 memory leak, stale route, 중복
handler 호출이 발생할 수 있다.

## RES-001 server restart

우선순위: `P0`

절차:

1. provider server A와 B를 시작한다.
2. client가 request를 보내 두 server가 모두 호출되는지 확인한다.
3. server B를 종료한다.
4. client request가 A로만 성공하는지 확인한다.
5. server B를 같은 routing id, 같은 endpoint로 재시작한다.

검증:

- B 종료 뒤 stale endpoint timeout이 반복되지 않는다.
- B 재시작 뒤 topology와 routing 대상에 다시 들어온다.
- B의 이전 connection state가 새 process에 섞이지 않는다.

## RES-001A Kubernetes식 pod 재스케줄

우선순위: `P0`

이 시나리오는 같은 logical rid를 가진 서버가 새 pod로 다시 뜨면서 endpoint만 바뀌는
상황을 검증한다. Discovery 문서의 `DSC-009`는 topology 교체 자체를 보며, 이 시나리오는
장애와 복구 중 실제 client request 결과를 본다.

절차:

1. provider B v1을 rid `api-b`, endpoint `<port1>`로 시작한다.
2. client가 B v1로 처리된 request evidence를 확인한다.
3. B v1 프로세스를 비정상 종료한다.
4. B v2를 같은 rid `api-b`, endpoint `<port2>`로 시작한다.
5. client는 재시작하지 않고 request를 계속 보낸다.

검증:

- 장애 구간의 request는 정상 response 또는 public timeout/error로 끝난다.
- 복구 후 request는 B v2 evidence에 기록된다.
- B v1 endpoint로 3회 이상 연속 stale timeout이 발생하지 않는다.
- registry에는 rid `api-b` entry가 하나만 active 상태로 남는다.
- client-side channel sender는 rid 기준 최신 endpoint로 갱신한다.

## RES-002 client reconnect storm

우선순위: `P1`

절차:

1. 여러 stream client를 동시에 연결한다.
2. server를 재시작하거나 network disconnect를 만든다.
3. client가 reconnect한다.

검증:

- server는 old session을 정리한다.
- reconnect 뒤 session count가 client 수와 맞다.
- actor binding이 중복되지 않는다.

## RES-003 cancellation propagation

우선순위: `P1`

절차:

1. client가 긴 request를 보낸다.
2. request timeout 또는 cancellation token으로 취소한다.
3. server handler가 cancellation을 관측하는지 확인한다.

검증:

- client는 cancellation 또는 timeout 오류를 받는다.
- server는 가능한 경우 handler cancellation을 기록한다.
- cancellation 이후 같은 channel에서 다음 request가 성공한다.

## RES-004 graceful shutdown

우선순위: `P0`

절차:

1. server가 request 처리 중일 때 shutdown을 시작한다.
2. 새 request를 거절하거나 drain한다.
3. 기존 request는 완료 또는 cancellation으로 끝낸다.

검증:

- shutdown timeout 이후 프로세스가 남지 않는다.
- registry topology에서 server가 빠진다.
- client는 shutdown 중 오류를 public error로 받는다.

## RES-004A in-flight request 중 provider crash

우선순위: `P0`

절차:

1. provider A, B를 시작한다.
2. B handler는 `SlowReq`를 받은 뒤 2초 동안 응답을 지연한다.
3. client가 B로 갈 수 있는 request를 여러 개 보낸다.
4. B가 request 처리 중 비정상 종료된다.
5. A는 계속 ready 상태로 둔다.

검증:

- B에서 이미 처리 중이던 request는 timeout, cancellation, transport error 중 문서화된
  public error로 끝난다.
- B crash 뒤 신규 request는 A로 성공한다.
- 같은 request id가 A로 자동 재전송되어 중복 성공 처리되지 않는다. 자동 retry 정책을
  지원하는 언어라면 idempotency key가 있는 request에서만 retry한다.
- client request map과 server evidence가 test 종료 시 일관된다.

## RES-005 resource cleanup

우선순위: `P1`

절차:

1. Spot, actor, stream session을 많이 만들고 닫는다.
2. close 후 evidence endpoint로 live resource count를 조회한다.
3. 같은 테스트를 여러 번 반복한다.

검증:

- live session, actor, timer count가 0 또는 expected idle count로 돌아온다.
- timer가 close 후 tick을 계속 만들지 않는다.
- temporary file, socket endpoint, Redis key prefix가 정리된다.

## RES-006 registry stale data cleanup

우선순위: `P1`

절차:

1. provider를 비정상 종료한다.
2. registry cleanup timeout을 기다린다.
3. topology를 조회한다.

검증:

- 비정상 종료된 provider가 무기한 ready로 남지 않는다.
- client는 stale provider를 routing 대상에서 제거한다.
- provider가 같은 id로 재시작해도 중복 entry가 생기지 않는다.

## RES-007 high fanout stability

우선순위: `P2`

절차:

1. subscriber 100개 또는 mesh peer 100개를 구성한다.
2. 256 byte payload를 60초 동안 초당 1000개 보낸다.
3. RSS 증가량, queue depth, error count를 수집한다.

검증:

- 테스트 구간의 RSS 증가량은 steady state 기준 128 MiB 이하로 머문다.
- error count는 0이어야 한다.
- backpressure 또는 drop 정책이 로그로 관측된다.
- 정상 종료 시 모든 connection이 닫힌다.

## RES-008 rolling update

우선순위: `P2`

절차:

1. provider A, B, C를 시작한다.
2. A를 새 버전으로 재시작한다.
3. B, C도 순서대로 재시작한다.
4. client traffic을 계속 보낸다.

검증:

- rolling update 중 전체 service가 unavailable 상태가 되지 않는다.
- version이 섞인 동안 wire compatibility가 유지된다.
- update 완료 뒤 모든 provider version이 registry metadata에 반영된다.

## RES-009 blue-green endpoint switch

우선순위: `P1`

이 시나리오는 배포 과정에서 같은 logical rid의 endpoint가 교체되는 상황을 검증한다.
공통 E2E는 generation, deployment color 같은 특정 metadata 필드를 요구하지 않는다.
언어별 구현이 active marker나 배포 제어 기능을 공개 contract로 제공한다면, 그 기능은
언어별 세부 문서에서 추가 검증으로 확장한다.

구성:

- blue provider group: rid `api-a`, endpoint `<blue-port>`
- green provider group: rid `api-a`, endpoint `<green-port>`

절차:

1. blue group을 시작한다.
2. client traffic을 보낸다.
3. green group을 같은 rid, 다른 endpoint로 ready 상태까지 올린다.
4. registry topology가 같은 rid의 endpoint 교체 또는 단일 active entry로 수렴하는지
   확인한다.
5. blue group을 drain 후 종료한다.

검증:

- overlap 구간의 request는 구현의 공개된 registry 갱신 규칙에 따라 blue 또는 green
  evidence에 기록될 수 있다.
- convergence 이후 신규 request는 green evidence에 기록된다.
- blue drain 중 in-flight request는 완료되거나 public cancellation/error로 끝난다.
- blue unregister가 늦게 도착해도 green endpoint를 덮어 쓰지 않는다.

## RES-010 network partition 후 복구

우선순위: `P1`

절차:

1. client와 provider B 사이의 network만 끊는다. registry와 provider A는 정상이다.
2. client는 request를 계속 보낸다.
3. partition을 해제한다.

검증:

- partition 중 request는 provider A로 우회하거나 public timeout/error로 끝난다.
- provider B가 registry에는 ready로 남더라도 client는 반복 실패 endpoint를 일시적으로
  회피할 수 있다. 회피 정책이 없다면 timeout 수와 복구 시간을 테스트 metadata에 남긴다.
- partition 해제 뒤 B는 다시 routing 대상이 된다.

## RES-011 registry partition

우선순위: `P1`

절차:

1. provider와 client가 정상 topology를 받은 상태에서 registry 연결만 끊는다.
2. 기존 provider endpoint는 계속 살아 있다.
3. client request를 계속 보낸다.
4. registry 연결을 복구한다.

검증:

- registry 연결이 끊겨도 이미 알고 있는 ready provider로 request가 성공할 수 있다.
- 새 provider 추가나 제거는 registry 복구 전까지 반영되지 않는다.
- registry 복구 뒤 topology delta가 적용되고 stale provider가 정리된다.

## RES-012 clock skew와 heartbeat 지연

우선순위: `P2`

절차:

1. provider heartbeat가 정상보다 늦게 도착하도록 지연을 넣는다.
2. registry cleanup timeout에 가까운 지연과 timeout을 넘는 지연을 각각 만든다.
3. client request를 계속 보낸다.

검증:

- cleanup timeout 안의 heartbeat 지연은 provider를 불필요하게 제거하지 않는다.
- timeout을 넘은 provider는 stale 처리된다.
- provider가 다시 heartbeat를 보내면 같은 rid의 현재 active entry와 충돌하지 않는
  경우에만 routing 대상으로 복구된다.

## RES-013 partial rollout wire compatibility

우선순위: `P1`

절차:

1. provider A는 old schema, provider B는 new schema로 시작한다.
2. client는 old-compatible request와 new-only request를 모두 보낸다.
3. rolling update 중 A를 내리고 new provider C를 올린다.

검증:

- old-compatible request는 old/new provider 모두 성공한다.
- new-only request는 old provider에서 명확한 unsupported error를 받거나 routing 대상에서
  old provider를 제외한다.
- registry metadata의 version 또는 capability로 routing 결정을 설명할 수 있어야 한다.
