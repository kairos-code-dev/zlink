# Node.js G4 DDD/POSD 리팩터링 ledger

검토 기준일은 2026-07-13이다. public contract 구현 뒤 framework, NestJS, Stream Connector의
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

현재 판정: **독립 read-only reviewer의 최종 판정 대기**
