# Framework E2E .NET 기준 재포팅 수정 목록

## 목적

이 문서는 Java, Kotlin, C++, Node.js framework E2E 재포팅 수정 문서의 색인이다.
상세 수정 항목과 체크리스트는 언어별 문서에서 관리한다.

기준은 scenario marker 통과 여부가 아니라 `.NET` E2E의 실행 책임, role 분리, 파일 분류, client driver
방식이다. Client는 `.NET` 기준에서 HTTP client나 stream connector로 server role을 호출하는 driver이면
같은 방식으로 포팅해야 한다. Client가 framework application, spot, actor, registry participant로 직접
구동되면 완료로 보지 않는다. Server 쪽도 `.NET`이 role별 process와 source tree를 나누면 대상 언어도
같은 의미의 role 분리를 가져야 한다.

## 언어별 문서

- [Java 수정 목록](framework-java-e2e-dotnet-porting-repair-list.ko.md)
- [Kotlin 수정 목록](framework-kotlin-e2e-dotnet-porting-repair-list.ko.md)
- [C++ 수정 목록](framework-cpp-e2e-dotnet-porting-repair-list.ko.md)
- [Node.js 수정 목록](framework-node-e2e-dotnet-porting-repair-list.ko.md)

## 공통 수정 기준

1. 각 config는 먼저 `.NET` 기준 source-only inventory를 다시 만든다.
   - `bin`, `obj`, `logs` 산출물은 기준 role에서 제외한다.
   - `.NET`에 source가 없는 `Server/Control` 같은 산출물 흔적은 기준 role로 세지 않는다.
2. `Client`는 `.NET` Client의 책임을 따른다.
   - HTTP client 기반 config는 대상 언어의 ZLink HTTP client나 같은 의미의 test HTTP support를 사용한다.
   - stream 기반 config는 stream connector를 사용한다.
   - Client가 framework host, Spring Boot framework participant, C++ `app_t`, spot manager, route mesh
     server로 떠서 server role을 대신하면 수정 대상이다.
3. `Server/<Role>`은 `.NET`의 role/process 의미를 따른다.
   - 하나의 server app에서 mode나 option으로 여러 role을 바꾸는 방식은 `.NET`이 role을 분리한 config의
     완료 형태가 아니다.
   - 대상 언어에 extra role이 있으면 공통 E2E나 `.NET` feature-map 근거가 있는지 확인하고, 근거가
     없으면 제거하거나 별도 gap으로 분리한다.
4. scenario 파일 분류는 `.NET Client/Scenarios`와 공통 E2E scenario ID를 기준으로 맞춘다.
   - 하나의 큰 `Program`이나 `basic-*.ts`에 여러 scenario가 모여 있으면 구조 수정 대상이다.
   - 기능이 빠진 scenario는 `feature-map.ko.md`와 `porting-inventory.ko.md`에 gap으로 남기고, 통과한
     것처럼 표시하지 않는다.
5. public API가 없어 같은 동작을 구현할 수 없으면 내부 helper, raw frame, 테스트 전용 adapter로 메우지
   않는다.

## Framework 기능 누락과 버그 처리 원칙

누락된 E2E 기능을 구현하는 중 framework 자체의 public 기능이 없거나 framework 버그가 드러나면, E2E
코드에서 우회하지 않는다. 먼저 원인을 확인하고, 필요한 framework 기능을 같은 public contract 기준으로
추가하거나 framework 버그를 수정한다.

완료 조건은 다음을 모두 포함한다.

1. 문제 원인이 E2E harness, language binding, framework runtime, public API 중 어디에 있는지 확인한다.
2. framework 기능 누락이면 spec, 공통 framework 문서, 기존 public API 근거를 확인한 뒤 같은 수준의
   public 기능으로 추가한다.
3. framework 버그이면 실패를 재현하는 회귀테스트를 먼저 추가하거나, 같은 변경 안에서 테스트와 수정을 함께
   남긴다.
4. E2E code에 raw frame, private helper, test-only adapter, extra sleep, retry-only workaround를 넣어
   통과시키지 않는다.
5. 수정 뒤에는 framework 회귀테스트와 해당 E2E runner를 함께 실행하고 결과를 문서에 반영한다.

## 공통 작업 순서

1. 해당 언어 문서의 체크리스트에서 고칠 config 하나를 고른다.
2. `.NET` source-only inventory와 대상 언어 inventory를 같은 기준으로 다시 비교한다.
3. Client 책임, server role, scenario 파일 분류, feature-map 상태를 함께 고친다.
4. 해당 config runner를 실제로 실행한다.
5. 결과와 남은 gap을 `porting-inventory.ko.md`, `feature-map.ko.md`, 언어별 수정 문서에 맞춘다.
6. 마지막에 Codex 에이전트로 언어별 문서 전체를 반복 리뷰해 누락 항목이 없다는 결과를 받을 때까지
   다시 대조한다.
