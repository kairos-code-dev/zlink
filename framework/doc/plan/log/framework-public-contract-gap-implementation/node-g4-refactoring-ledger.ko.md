# Node.js G4 DDD/POSD 리팩터링 ledger

검토 기준일은 2026-07-14이다. public contract 구현 뒤 framework, NestJS, Stream Connector의
production source를 책임 경계, 정보 은닉, 호출자 복잡성, 패스스루와 중복 구현 관점에서 다시
검토했다.

## review manifest

| 범위 | 포함 경로 또는 경계 | 제외와 근거 |
|------|---------------------|-------------|
| production framework | `packages/framework/src/**` | `dist`, coverage와 생성 로그는 source가 아니므로 제외 |
| NestJS integration | `packages/nestjs/src/**`, DI token/provider와 lifecycle wiring | 생성된 JavaScript와 설치 산출물 제외 |
| Stream Connector와 supporting package | `packages/stream-connector/src/**`, `packages/stream-wire/src/**` | HTTP client는 framework public contract gap 계획의 분모가 아니므로 제외 |
| codec/location extension | `packages/framework-codec-*/src/**`, `packages/framework-locations-redis/src/**` | 외부 dependency source 제외 |
| public/package wiring | 각 package `package.json`, root export, subpath export, root `package.json`, lock과 TypeScript build graph | sample과 E2E는 G5/G6에서 별도 검토 |
| runtime 경계 | bindings 공개 socket/context API, Redis store, NestJS lifecycle, `.NET` cross-language test host | bindings와 `.NET` production 구현 자체는 소유 언어 범위라 제외 |

| 검토 항목 | 결과 | 근거 |
|-----------|------|------|
| public registration 내부 정보 노출 | 해소 | framework root export에서 registration 구현을 제거하고 NestJS 전용 subpath로 제한 |
| codec 책임의 호출자 전가 | 없음 | typed JSON 기본 경로와 package codec 확장점 유지; 메시지별 등록 API 없음 |
| flow correlation 중복 생성 | 해소 | Stream Connector가 stream correlation codec을 소유하고 framework가 이를 사용 |
| reconnect metric 중복 계수 | 해소 | connector lifecycle이 reconnect attempt 계기를 소유 |
| drain 종료 타이머 잔존 | 해소 | 정상 종료 뒤 deadline 타이머를 즉시 해제하도록 수정 |
| subscriber poller 종료 순서 | 해소 | receive loop 완료 뒤 등록 socket을 제거하고 poller를 dispose |
| context shutdown 뒤 event-loop turn 결합 | 해소 | socket과 listener 종료 뒤 context dispose가 직접 수명을 마치도록 중복 shutdown/yield 제거 |
| channel `flow_origin` wire 표현 불일치 | 해소 | public 문자열 union은 유지하고 channel envelope wire는 공통 숫자 값 1~4로 encode/decode |
| raw session escape hatch | 해소 | typed session handler와 registry 경로로 제한 |
| typed packet identity 중복 결정 | 해소 | channel/route/Spot/fanout는 `@ZLinkPacket`의 own metadata 또는 생성자 이름만 사용하고 payload method와 call override를 제거 |
| test/sample 전용 public 우회 | 없음 | package consumer, sample, E2E가 배포 public API를 사용 |

검토 중 확인한 위험 신호에는 public registration 노출과 종료 타이머 잔존이 있었다. registration은
공개 root에서 감추는 방법과 별도 public builder로 유지하는 방법을 비교해 전자를 선택했다. 정식
interface에 없는 등록 구현을 호출자에게 유지할 이유가 없기 때문이다. 종료 타이머는 `unref()`만
호출하는 방법과 정상·오류 종료에서 명시적으로 해제하는 방법을 비교해 후자를 선택했다. 명시적
해제가 타이머 자원 소유권을 종료 경로 안에 유지한다.

Node 20에서 fanout subscriber 종료가 멈추는 위험 신호도 확인했다. poller를 먼저 파괴해 loop를
중단하는 방법과 loop가 최대 10ms poll에서 복귀한 뒤 poller를 파괴하는 방법을 비교해 후자를
선택했다. 또한 context `shutdown()` 뒤 별도 event-loop turn을 기다리는 방법과 모든 child
resource를 닫은 뒤 context를 직접 dispose하는 방법을 비교해 후자를 선택했다. 후자는 BUSY retry와
context close 책임을 backend context 한 곳에 유지하며 Node 20과 Node 22 반복 실행에서 같은 종료
동작을 보였다.

cross-language 실행에서 Node가 문자열 `flowOrigin`을 wire에 기록하고 .NET이 숫자를 기록하는 불일치를
확인했다. public 타입을 숫자로 바꾸는 방법과 codec 경계에서 공통 wire 값으로 변환하는 방법을 비교해
후자를 선택했다. 언어별 public 사용성을 유지하면서 wire 결정은 codec 안에 숨길 수 있기 때문이다.

최종 재검토에서는 typed packet 이름을 payload의 `packetName()`이나 call builder의
`packetName(...)`으로 덮어쓸 수 있는 위험 신호를 확인했다. 기존 override를 유지하면서 우선순위만
정하는 방법과, 정식 계약대로 decorator의 own metadata 또는 생성자 이름에 결정 책임을 모으는 방법을
비교해 후자를 선택했다. 후자는 packet identity 결정이 payload와 전송 호출부로 퍼지지 않으며,
subclass가 부모 decorator metadata를 의도하지 않게 상속하는 문제도 함께 없앤다. Stream Connector의
명시적 frame packet name은 별도 공개 계약이므로 유지했다. 변경 뒤 전체 G3, Node 20/22 runtime
matrix, sample, 181개 E2E와 cross-language 검증을 다시 통과했다.

## 2차 adversarial review

2차 검토에서는 one-way call이 runtime 미시작과 bounded queue 거부 오류를 제거하는 문제를
확인했다. 호출부에서 `catch`만 제거하는 안과 queue owner가 동기 수락 여부를 반환하는 안을
비교했고, queue owner에 `submitCommandOneWay(...)`을 두는 후자를 선택했다. 이 방식은 capacity와
socket readiness 결정을 호출자에게 노출하지 않으면서 `submit(): void` 계약을 지킨다.

또한 정식 Node spec과 guide에 제거된 `publishToChannel`, channel `packetName(...)`, public
`yield(...)` 설명이 남은 문제와 Node↔`.NET` fanout/session-closing/route 역방향 누락을 확인했다.
문서를 실제 interface에 맞추고 public package만 사용하는 양방향 runner stage를 추가했다.

## browser 전환 뒤 최종 재검토

browser 전환 뒤에는 두 가지 위험 신호를 추가로 확인했다. 첫째, browser E2E 공용 코드의 TypeScript
source 옆에 과거 생성 JavaScript가 남아 있었고 두 파일의 HTTP method 지원도 달랐다. 생성
JavaScript를 계속 동기화하는 방법과 TypeScript만 source로 두고 browser bundler가 직접 읽는 방법을
비교해 후자를 선택했다. 같은 동작 지식이 두 파일에 중복되지 않고, 실제 E2E가 소비하는 경로와
검토 대상이 일치하기 때문이다.

둘째, Bingo Protobuf 생성기가 마지막 빈 항목과 별도 줄바꿈을 함께 기록해 생성 명령을 실행할 때마다
추적 파일 끝이 바뀌었다. 생성 결과의 불필요한 빈 줄을 정본으로 받아들이는 방법과 생성기에서 마지막
빈 항목을 제거하는 방법을 비교해 후자를 선택했다. 생성기는 실행 전후에 동일한 작업 트리를 보장해야
하며, 출력 형식 결정도 생성기 한 곳이 소유해야 하기 때문이다.

Stream Connector의 browser transport, 명시적 `flowFrom(message)`, package root, codec adapter와
framework reply routing도 다시 검토했다. browser transport 세부 정보는 connector 내부에 있고,
flow 문맥은 browser 전역 상태에 저장되지 않으며, codec의 server 등록 책임은 `./framework`에
분리되어 있다. actor 응답의 fallback route는 이동 중 바뀔 수 있는 actor 상태를 다시 조회하는 대신
요청 수신 시점의 내부 route snapshot을 유지하며 public interface에는 노출되지 않는다.

Bingo 반복 검증에서는 actor 소유권 갱신과 bound-session 응답 전달이 겹치면 논리적 Session route가
native binding 교체 중 잠시 제거되는 위험 신호를 확인했다. sample runner의 대기시간을 늘리는 방법과
binding coordinator가 기존 논리 route를 새 native binding 준비가 끝날 때까지 유지하는 방법을
비교해 후자를 선택했다. 응답 전달은 native binding 교체 절차를 알 필요가 없고, route 교체의 원자성은
binding coordinator가 보장해야 하기 때문이다. 교체 중 응답을 전달하는 회귀 테스트와 실제 Chromium
Bingo 10회 반복 실행으로 응답 누락이 해소됐는지 확인했다.

release 반복 검증에서는 Redis 컨테이너 내부 PING 준비와 Docker의 host port publish 준비를 하나의
상태로 취급하는 시간 결합을 확인했다. 각 sample runner에서 endpoint 조회 실패를 재시도하는 방법과
컨테이너 lifecycle helper가 host port가 게시될 때까지 제한 시간 안에서 기다리는 방법을 비교해
후자를 선택했다. Docker 상태 해석과 준비 완료의 정의를 공용 helper 한 곳에 유지하고 sample은 준비된
endpoint만 받도록 하기 때문이다. release 재검증에서 port 번호가 게시된 뒤 host 연결이 아직
열리지 않은 경우도 확인해, 같은 helper가 실제 loopback TCP 연결까지 확인한 뒤 endpoint를 반환하도록
준비 완료 정의를 보완했다. DeliveryDispatch 실제 Chromium 시나리오 10회 반복과 sample regression으로
변경 뒤 경계를 확인했다.

같은 반복 검증에서 sample runner가 동적 포트를 확인한 직후 다른 프로세스가 그 포트를 선점하면 역할
프로세스는 `Address already in use`를 파일에만 남기고, 상위 runner에는 readiness timeout만 보이는
정보 은닉 오류도 확인했다. 모든 포트를 고정하거나 대기시간을 늘리는 방법과, 역할 실패 로그를 sample
경계의 stderr로 전달해 상위 runner의 기존 bind 오류 전용 재시도 정책이 판별하게 하는 방법을 비교해
후자를 선택했다. 고정 포트는 병렬 실행을 깨뜨리고 대기시간은 이미 종료된 역할을 복구하지 못한다.
GameQuest 보존 실행에서 `spot_node_set_router_bind`의 `errno=98`을 재현해 원인을 확정했다.

bind 전용 재시도를 적용한 뒤에도 외부 작업과 포트 선택 범위가 겹치면 연속 경합할 수 있었다. 모든
endpoint를 TCP 동적 포트로 유지하는 방법과, browser가 접근하는 HTTP/WebSocket만 TCP에 두고 같은
호스트의 Spot·route 연결은 실행 디렉터리별 IPC endpoint를 사용하는 방법을 비교해 후자를 선택했다.
이 방법은 public sample의 역할 구성을 바꾸지 않으면서 runner 내부 transport의 포트 선점 경쟁을
제거한다. IPC 전환 뒤 보존 실행에서 `errno=98`이 다시 발생하지 않는지 확인했다.

이후 상태 callback을 추가하지 않은 반복 실행에서 GameQuest 완료 알림이 간헐적으로 누락됐고,
browser 오류 event의 `Frame length does not match prefix`와 서버의 연속 알림 전송 기록을 함께
보존해 별도 원인을 확인했다. core WebSocket 송신을 STREAM frame마다 나누는 방법과 connector의
wire protocol 경계에서 길이 prefix로 한 transport chunk의 모든 frame을 분리하는 방법을 비교해
후자를 선택했다. WebSocket 송신 경계를 업무 frame 경계로 가정하지 않으며, self-delimiting wire
형식의 해석 책임을 protocol 안에 유지할 수 있기 때문이다. 응답·진행 알림·완료 알림을 하나의
WebSocket message에 넣는 결정적 contract test와 실제 Chromium GameQuest 20회 연속 실행을 통과했다.

최종 판정: **NO DDD/POSD FINDINGS**
