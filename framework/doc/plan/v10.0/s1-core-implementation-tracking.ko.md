# Core 10.0.0 구현 일치 추적

## 0. 문서 상태와 범위

이 문서는 Core 10.0.0 정식 spec과 현재 checkout의 공개 header, export, runtime, test와 package 사이의
차이를 구현 단계까지 추적하는 임시 문서다. 공개 계약은 `core/doc/spec/`만 소유한다. 정식 spec, guide와
internals는 이 문서를 참조하지 않는다.

대상 독자는 S4 Core 구현 담당자와 S5 reviewer다. 이 문서는 “10.0.0 공개 계약을 구현하려면 현재
checkout에서 무엇을 추가·교체·정리하고 어떤 증거로 닫아야 하는가?”에 답한다.

## 1. 판정 기준

각 항목은 다음 다섯 증거가 모두 일치할 때만 완료한다.

1. `core/include/zlink.h`가 정식 spec의 exact C ABI를 노출한다.
2. 설치 public header와 동적 library export가 같은 symbol을 제공한다.
3. public contract test가 성공·실패·ownership·thread-safety 계약을 검증한다.
4. 제거 대상 symbol, source branch, build entry와 package entry가 검색되지 않는다.
5. 한국어·영문 정식 spec과 생성 API snapshot이 일치한다.

공개 API와 export의 정확한 이름별 판정은
[`Core 공개 API inventory`](./s1-core-public-api-inventory.ko.md)가 소유한다.

## 2. 구현 차이

| ID | 정식 계약 owner | 현재 checkout 차이 | S4 완료 증거 | 상태 |
|---|---|---|---|---|
| CI-01 | `core/service/mesh-node` | 공개 header와 runtime이 MeshNode lifecycle, membership, peer admission, node·channel messaging을 제공하지 않음 | header API snapshot, peer/admission contract test, export 검사 | open |
| CI-02 | `core/service/dispatch` | ready index, domain별 claim, ready·receive batch와 one-shot reply token이 없음 | lost-wakeup, single-consumer, batch capacity, shutdown contract test | open |
| CI-03 | `core/service/spot` | complete multipart direct messaging, channel-scoped Logical Multicast와 publish option 계약이 없음 | local ref-count fanout, remote node당 단일 submit, NODROP atomicity test | open |
| CI-04 | `core/service/actor` | Actor mailbox claim, Node-origin·Actor-origin completion owner와 transfer fence 계약이 없음 | Actor ordering, claim 독립성, prepare/commit/activate/abort test | open |
| CI-05 | `core/service/stream-session` | 명시적 service handle, complete multipart 양방향 전송과 transfer barrier가 없음 | lifecycle, binding CAS, FIFO barrier와 disconnect test | open |
| CI-06 | `core/socket/router` | raw ROUTER의 기존 `*_part` ABI를 유지하면서 service envelope과 bridge 분기를 제거해야 함 | raw `*_part` request/reply/recv contract test, service envelope no-leak와 제거 branch no-hit | open |
| CI-07 | `core/socket/stream` | raw STREAM complete multipart API와 STREAM session 분리가 header/runtime에 반영되지 않음 | raw recv/callback/packet mode 배타성 및 session 분리 test | open |
| CI-08 | `core/socket/pub`·`sub`·`xpub`·`xsub` | raw fanout API가 service handle을 함께 처리하는 branch와 문서화 흔적이 있음 | raw handle만 허용하는 type/error test, service branch no-hit | open |
| CI-09 | `core/polling` | MeshNode poll source와 ready handler 상호 배제, 독립 `POLLOUT` 의미가 없음 | poller/handler conflict와 domain progress test | open |
| CI-10 | `core/monitoring`·`events` | MeshNode event, status snapshot과 source kind가 없음 | event ordering, snapshot consistency와 bounded label test | open |
| CI-11 | `core/errors`·`errno-map` | 10.0.0 result 값, portable errno와 모든 service 함수 mapping이 header/runtime에 없음 | enum ABI static assert, result/errno matrix test | open |
| CI-12 | `core/service/mesh-node` §9·`spot` §10 | option/handle 조합과 기본 `NODROP=1`이 runtime에 없음 | set/get, lifecycle, unsupported-combination test | open |
| CI-13 | `core/errors` §7 | public version macro, runtime version, SOVERSION과 package 이름이 10.0.0으로 일치하지 않음 | header/runtime/CMake/package version 검사 | open |
| CI-14 | 전체 service 계약 | 폐기 대상 SpotNode, route bridge, service PUB/SUB plane, part recv/send와 callback runtime이 남아 있음 | inventory의 제거 목록 전체 source/export/package no-hit | open |
| CI-15 | 전체 Core 계약 | 설치 header, generated export 목록, bindings native payload와 API snapshot이 정식 spec을 반영하지 않음 | clean build artifact와 package content 검사 | open |

## 3. 정리 범위

구현은 이름만 바꾸는 wrapper를 추가하지 않는다. MeshNode가 routing, mailbox, operation과 readiness를 직접
소유하도록 service runtime을 구성하고 다음 자산을 함께 정리한다.

- 폐기한 public declaration, enum, macro와 callback type
- 폐기한 export와 export를 유지하기 위한 forwarding function
- SpotNode mode와 service PUB/SUB socket 배선
- route bridge, channel-dealer completion과 remote subject inventory
- part 단위 service send·receive 상태
- Core dispatch worker option과 worker pool
- source, test, benchmark, CMake entry, generated file과 package payload의 죽은 항목

raw ROUTER, DEALER, PUB/SUB, STREAM과 message API는 각 raw socket 정식 계약에 맞게 유지한다. 이름이
비슷하다는 이유로 raw socket 기능을 service 정리 대상에 포함하지 않는다.

## 4. 검증 명령 계약

S4에서 실제 경로와 target 이름을 확정한 뒤 명령을 실행 기록에 고정한다. 최소 검증은 다음 범위를 가진다.

```text
public header -> declarations and ABI values
shared library -> exported symbols and SOVERSION
contract tests -> result, errno, ownership, ordering, lifecycle
source tree -> removed-name and dead-file no-hit
packages -> installed header, native payload, API snapshot
```

명령, 종료 코드와 artifact SHA-256은 `framework/doc/plan/v10.0/log/s4/`의 iteration별 verification 문서에
기록한다. 정식 spec에는 구현 상태나 실행 log 링크를 추가하지 않는다.

## 5. 완료 조건

- [ ] CI-01부터 CI-15까지 모두 closed다.
- [ ] 공개 API inventory의 모든 항목이 10.0.0 header/export/package와 일치한다.
- [ ] 제거 목록이 source, build, test, bindings와 package에서 모두 검색되지 않는다.
- [ ] contract, structural, failure-path와 성능 회귀 test가 통과한다.
- [ ] 구현과 structural test가 끝난 뒤에만 `doc/internals/`를 현재 구조로 갱신한다.
- [ ] S5의 두 독립 reviewer가 같은 고정 revision을 검토한다.
