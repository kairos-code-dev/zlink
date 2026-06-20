<!-- framework-adapter-nav:start -->
[이전: 테스트 하네스](01-harness-and-evidence.ko.md) | [E2E 목차](README.ko.md) | [다음: Discovery와 scale-out](03-discovery-scaleout.ko.md)
<!-- framework-adapter-nav:end -->

# Channel Messaging E2E

이 문서는 client-server channel, dealer mesh, route mesh를 실제 server process와 client
connector로 검증하는 시나리오를 정의한다.

## CH-001 단일 server request-response

우선순위: `P0`

구성:

- API client 1개
- channel server 1개
- request handler 1개

절차:

1. client가 `EchoReq`를 보낸다.
2. server handler는 request id, payload, correlation id를 evidence에 저장한다.
3. handler는 `EchoRes`를 반환한다.

검증:

- client는 같은 request id를 가진 response를 받는다.
- server evidence에는 handler 호출 1회가 남는다.
- response header의 correlation id가 request와 연결된다.

## CH-002 수동 endpoint round-robin

우선순위: `P0`

구성:

- client 1개
- 같은 channel server 3개
- Discovery 없이 endpoint 3개를 직접 등록

절차:

1. client가 같은 channel로 `N = 90` 요청을 보낸다.
2. 각 server는 자신의 server id와 요청 index를 evidence에 저장한다.
3. client는 모든 응답을 모은다.

검증:

- 3개 server가 모두 요청을 받는다.
- 기본 round-robin이면 각 server가 정확히 30회 요청을 받아야 한다.
- 연결 warm-up 또는 provider 추가 직후처럼 첫 요청이 안정화 구간에 들어갈 수 있는
  구현은 테스트 시작 전에 warm-up request를 3회 보내고, 검증 대상 90회는 정확히 30회씩
  분산되어야 한다.
- 응답 순서가 요청 순서와 달라도 correlation id로 매칭된다.

## CH-003 weighted routing

우선순위: `P1`

구성:

- client 1개
- server A, B, C
- weight A=1, B=2, C=5

절차:

1. deterministic weighted round-robin 구현은 client가 `N = 800` 요청을 보낸다.
2. probabilistic weighted routing 구현은 client가 `N = 8000` 요청을 보낸다.
3. 각 server 호출 수를 evidence로 모은다.

검증:

- deterministic weighted round-robin이면 A=100회, B=200회, C=500회를 정확히 받는다.
- probabilistic weighted routing이면 A는 900-1100회, B는 1900-2100회, C는
  4900-5100회 범위 안에 들어야 한다.
- 구현은 deterministic인지 probabilistic인지 언어별 feature map 또는 테스트 metadata에 남긴다.
- weighted 기능을 지원하지 않는 언어는 feature map에서 미지원으로 표시한다. feature map
  위치는 [E2E 목차](README.ko.md)의 우선순위 절을 따른다.

## CH-004 route mesh targeted request

우선순위: `P0`

구성:

- route mesh peer 3개
- 각 peer는 routing id를 가진다.

절차:

1. client 역할 peer가 target routing id를 지정해 request를 보낸다.
2. target peer만 handler를 실행한다.
3. 다른 peer는 evidence에 호출이 없어야 한다.

검증:

- 지정한 peer만 response를 반환한다.
- 잘못된 routing id는 caller-visible error로 끝난다.
- routing id가 response evidence에 남는다.

## CH-005 dealer mesh peer request

우선순위: `P1`

구성:

- dealer mesh peer 3개
- peer 간 양방향 request 가능

절차:

1. peer A가 B와 C에 각각 request를 보낸다.
2. peer B가 A에 request를 보낸다.
3. 동시에 여러 request를 보내 correlation 충돌을 확인한다.

검증:

- 각 peer는 자신에게 온 request만 처리한다.
- 동시 request의 response가 섞이지 않는다.
- timeout이 발생해도 다른 request response map을 깨뜨리지 않는다.

## CH-006 send one-way

우선순위: `P0`

절차:

1. client가 `RecordCommand`를 send로 보낸다.
2. server는 handler 실행 후 evidence에 기록한다.
3. client는 response를 기다리지 않는다.

검증:

- server evidence에 command가 기록된다.
- client는 response wait 상태에 남지 않는다.
- handler 예외는 dispatch error observer로 보고된다.

## CH-007 request timeout과 late reply

우선순위: `P0`

절차:

1. server handler가 client timeout보다 늦게 응답한다.
2. client는 timeout error를 받는다.
3. server가 나중에 reply를 보내도 client request map이 오염되지 않아야 한다.

검증:

- timeout 이후 같은 client가 다음 request를 정상 처리한다.
- late reply는 unexpected reply dispatch error 또는 무시 정책으로 관측된다.

## CH-008 concurrent load

우선순위: `P1`

절차:

1. client 여러 개가 같은 channel에 concurrent request를 보낸다.
2. server handler는 payload sequence를 검증한다.
3. 일부 request는 느리게 처리한다.

검증:

- 모든 response가 올바른 client와 request로 돌아간다.
- 느린 handler가 다른 channel의 dispatch를 막지 않는다.
- shutdown 시 pending request가 정해진 오류로 종료된다.
