# RouteMesh 11.0 구현 기준과 migration crosswalk

이 디렉터리는 RouteMesh 11.0 migration 대조 자료와 구현 진행 상태를 관리한다. Application이 관찰하는
공개 계약은 [Framework 정식 spec](../../framework/spec/README.ko.md)이 소유하고, 네 언어 runtime이 지켜야 하는
내부 불변 조건은 [Framework 공통 internals](../../framework/common/internals/README.ko.md)가 소유한다.

## 문서 구성

| 문서 | 역할 |
|---|---|
| [Framework 정식 spec](../../framework/spec/README.ko.md) | Service 공개 동작과 package별 계약의 단일 기준 |
| [Framework 공통 internals](../../framework/common/internals/README.ko.md) | Protocol, queue, ownership, fencing, recovery와 resource 불변 조건의 단일 기준 |
| [Service 공개 계약 migration crosswalk](target-spec/README.ko.md) | Core 10 service 의미를 정식 Framework spec의 소유 문서와 연결하는 대조 자료 |
| [Service runtime 구현 crosswalk](target-internals/README.ko.md) | Core 10 구현·test를 네 언어 runtime의 정식 internals와 구현 lane에 연결하는 대조 자료 |
| [다섯 언어 exact interface](../../framework/spec/server/languages/README.ko.md) | C++·.NET·Java·Kotlin·Node.js public signature의 정식 계약. 각 언어의 `interfaces/`가 정확한 선언을 소유한다. |
| [통합 execution ledger](route-mesh-11.0.0-execution-ledger.ko.md) | 선행 조건, 병렬 lane, 상태, 구현 차이와 완료 증거의 단일 기준 |

Target 문서는 정식 계약을 별도로 정의하지 않는다. 정식 문서와 내용이 다르면 정식 spec 또는 정식 internals를
적용하고, 같은 candidate에서 target crosswalk를 수정한다. 현재 구현과의 차이, review finding, test 결과와
package 증거는 execution ledger에만 기록한다.

## 작업 순서

1. 삭제 전 Core service spec·internals의 각 절과 공개 type·symbol, 구현·test·build·package 입력을 전수
   분류한다.
2. Framework 정식 spec·internals와 다섯 언어 exact interface에서 공개 의미와 내부 불변 조건을 먼저 확정하고,
   target crosswalk로 Core 10 의미와 구현 입력이 빠지지 않았는지 확인한다.
3. Core raw-only 경계와 ZMP heartbeat가 없는 transport 계약을 Core 정식 spec·internals에서 확정한다.
4. 10.x oracle를 별도 process와 normalized trace로 봉인한 뒤 Core service·ZMP heartbeat를 먼저 제거하고,
   POSD·DDD review를 통과한 Core 11 local/internal package를 만든다.
5. C++·.NET·Java·Node bindings의 service·heartbeat projection을 제거하고 public raw capability와 package를
   검증한다.
6. 기존 Framework service adapter를 fail-closed compile scaffold로 교체한 뒤 C++·.NET·JVM·Node.js runtime을
   같은 계약 snapshot에서 병렬 구현한다. Java와 Kotlin은 JVM runtime과 build 파일을 공유한다.
7. Vertical slice별 E2E와 review, 전체 contract·race·crash·`4 x 4` E2E·sample·smoke, Framework cleanup을
   통과한 뒤 final local/internal package를 검증한다.

정식 spec과 주요 내부 불변 조건이 확정되기 전에는 runtime 구현을 시작하지 않는다. 한 언어를 먼저 완성해
나머지 언어가 번역하는 방식은 사용하지 않는다. 각 lane은 같은 schema와 fixture를 사용하고 기능 block의
contract test와 cross-language E2E에서 합류한다.

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

작업을 시작할 때는 execution ledger에서 담당 ID, 선행 조건, 소유 파일과 완료 gate를 먼저 확인한다. 상태와
증거를 다른 문서에 복제하지 않는다.
