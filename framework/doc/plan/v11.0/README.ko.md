# RouteMesh 11.0 구현 기준과 migration crosswalk

이 디렉터리는 RouteMesh 11.0 migration 대조 자료와 구현 진행 상태를 관리한다. Application이 관찰하는
공개 계약은 [Framework 정식 spec](../../framework/common/spec/README.ko.md)이 소유하고, 네 언어 runtime이 지켜야 하는
내부 불변 조건은 [Framework 공통 internals](../../framework/common/internals/README.ko.md)가 소유한다.

## 문서 구성

| 문서 | 역할 |
|---|---|
| [Framework 정식 spec](../../framework/common/spec/README.ko.md) | Service 공개 동작과 package별 계약의 단일 기준 |
| [Framework 공통 internals](../../framework/common/internals/README.ko.md) | Protocol, queue, ownership, fencing, recovery와 resource 불변 조건의 단일 기준 |
| [Core service migration inventory](../../contract-inventory/route-mesh-v11-core-service-migration-inventory.json) | Core 10 service 공개 의미와 구현 입력을 정식 Framework spec·internals owner에 연결하는 machine-readable 대조 자료 |
| [다섯 언어 exact interface](../../framework/common/spec/server/languages/README.ko.md) | C++·.NET·Java·Kotlin·Node.js public signature의 정식 계약. 각 언어의 `interfaces/`가 정확한 선언을 소유한다. |
| [언어별 public contract source 경계 감사](framework-public-contract-source-layout-audit.ko.md) | Server, HTTP client와 Stream Connector의 `Contracts/contracts` 이탈 현황, 이동 근거와 복구 조건 |
| [통합 execution ledger](route-mesh-11.0.0-execution-ledger.ko.md) | 선행 조건, 병렬 lane, 상태, 구현 차이와 완료 증거의 단일 기준 |
| [blocked issue log](blocked-issue-log.md) | 자율 실행 중 발생한 blocker의 원인, 수정 내용과 재검증 결과 기록. 진행 상태는 소유하지 않는다 |

정식 spec과 정식 internals가 현재 계약과 runtime 구조를 소유한다. Core 10 migration의 no-loss 대조와 분류는
machine inventory가 소유하고, 현재 구현과의 차이, review finding, test 결과와 package 증거는 execution ledger에
기록한다.

## 작업 순서

1. 삭제 전 Core service spec·internals의 각 절과 공개 type·symbol, 구현·test·build·package 입력을 전수
   분류한다.
2. Framework 정식 spec·internals와 다섯 언어 exact interface에서 공개 의미와 내부 불변 조건을 먼저 확정하고,
   machine inventory로 Core 10 의미와 구현 입력이 빠지지 않았는지 확인한다.
3. Core raw-only 경계와 ZMP heartbeat가 없는 transport 계약을 Core 정식 spec·internals에서 확정한다.
4. 10.x oracle를 별도 process와 normalized trace로 봉인한 뒤 Core service·ZMP heartbeat를 먼저 제거하고,
   POSD·DDD review를 통과한 Core 11 local/internal package를 만든다.
5. C++·.NET·Java·Node bindings의 service·heartbeat projection을 제거하고 public raw capability와 package를
   검증한다.
6. M5 foundation 뒤 global identity와 remote placement contract amendment를 정식 공통·server spec, 다섯 언어
   exact interface와 protocol/schema에 먼저 반영한다. 변경될 E2E·sample·registration과 유지할 regression을
   impact manifest에 분류하고 review한다.
7. Review가 끝나면 임시 변경 제안의 채택 내용을 정식 문서에 모두 흡수했는지 확인하고 제안 문서를
   삭제한다. 이후 작업은 정식 spec·internals, exact interface, protocol/schema, impact manifest와 execution
   ledger만 참조한다.
8. E2E·sample source와 registration은 유지한 채 실행 graph에서만 격리하고, 같은 계약 snapshot에서 .NET
   runtime을 기준으로 구현한 뒤 C++·JVM·Node.js runtime이 그 형태를 미러링한다. Java와 Kotlin은 JVM runtime과 build 파일을 공유한다. 이 구간은
   internal unit·contract·resource·protocol regression만 실행한다.
9. 네 runtime과 production placeholder 제거가 끝나면 공통 E2E와 sample spec을 최종 확정한다.
   Codex `gpt-5.6-sol high` 단독 reviewer가 candidate를 독립 review하고 assertion·coverage·다섯 언어 parity를 승인한
   뒤에만 E2E source와 registration을 변경한다.
10. E2E는 topology, stateful object, maintenance, race·`4 x 4` 순서로 활성화하고 전체 E2E 통과 뒤 sample을
   실행한다.
11. Correctness·smoke와 Framework cleanup을 통과한 뒤 final local/internal package를 검증한다.

정식 spec과 주요 내부 불변 조건이 확정되고 임시 변경 제안이 제거되기 전에는 runtime 구현을 시작하지 않는다.
.NET lane이 기준 구현을 먼저 완성하고 나머지 언어가 그 형태를 미러링한다. 각 lane은 같은 schema와 fixture를
사용하고 runtime 구현 중에는 internal contract test, 실행 재활성화 뒤에는 cross-language E2E에서 합류한다.

## 구현 경계

C++·.NET·JVM·Node.js는 각각 자기 package 안에 service runtime을 구현한다. Framework 전용 공통 native
runtime, private C SPI와 언어 공통 service ABI를 만들지 않는다. 각 runtime은 설치된 해당 언어 binding의
public raw socket API만 사용한다. 필요한 raw 기능이 없으면 Core 또는 binding의 일반 공개 transport 계약을
spec-first로 보완하며 private member나 내부 symbol로 우회하지 않는다.

이번 bindings 범위는 C++, .NET, Java와 Node.js다. Kotlin Framework는 Java binding과 JVM runtime을 사용한다.
C, Python, Go와 Rust는 마지막 지원 Core 10.x 조합으로 격리하고 Core 11 build·package·CI와 호환 표기에서
제외한다. 별도 11.x 전환 계획도 만들지 않는다.

## 검증과 배포 경계

- Package는 local/internal 위치에만 생성한다. 외부 registry에는 배포하지 않는다.
- 성능은 네 runtime과 Kotlin consumer의 build, startup, 최소 operation, provenance와 cleanup smoke까지만
  검증한다. 처리량·latency·CPU·memory 판정과 개선은 후속 작업으로 분리한다.
- Core·bindings API를 제거한 뒤에는 export, include, build graph, generated output, test discovery, package
  manifest와 clean consumer를 확인한다. 문자열 검색만으로 제거 완료를 판단하지 않는다.
- POSD·DDD review는 공개 인터페이스가 구현 복잡성을 감추는지, domain 책임이 중복되지 않는지, 제거한 API를
  되살리는 compatibility helper가 없는지 확인한다.
- Review 1~4회차에는 모든 유효 finding을 반영한다. 5회차부터 `Medium` 이상이 없으면 남은 `Low`를 ledger에
  기록하고 clean으로 종료할 수 있다.
- 문서나 aggregate hash 변화만으로 작업을 막지 않는다. 바뀐 의미와 직접 영향받는 계약·fixture·lane을 다시
  확인한다.
- Runtime 구현 중 E2E·sample은 `pending-disabled-by-contract-amendment` 상태로 관리한다. 이 상태를 skip이나
  성공으로 집계하지 않으며 source·scenario ID·registration을 삭제하거나 주석 처리하지 않는다.
- Contract amendment가 영향을 주는 E2E·sample만 impact manifest의 승인한 old→new hash에 따라 변경한다.
  영향받지 않은 source와 registration은 diff 0을 유지한다.

작업을 시작할 때는 execution ledger에서 담당 ID, 선행 조건, 소유 파일과 완료 gate를 먼저 확인한다. 상태와
증거를 다른 문서에 복제하지 않는다.
