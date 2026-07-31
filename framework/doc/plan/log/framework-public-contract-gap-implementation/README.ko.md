# Framework public contract gap 구현 로그

이 디렉터리는
[`route-mesh-11.0.0-execution-ledger.ko.md`](../../v11.0/route-mesh-11.0.0-execution-ledger.ko.md)를
실행하면서 생성되는 시간순 기록을 보관한다. 계획 문서는 작업 절차, 완료 조건과 현재 진행 상태만
유지하고, 명령 실행 결과와 반복 리뷰 이력은 이 디렉터리의 언어별 로그에 기록한다.

언어별 로그는 다음 항목을 포함한다.

- 실행 시각, 대상 gate와 기준 commit
- 실행한 정확한 명령과 exit code
- 성공한 테스트 수 또는 실패 원인
- 실제 로그나 marker 위치
- Codex 리뷰 회차, finding 수와 반영 상태

새 기록에서 working tree를 기준으로 실행하면 마지막 commit과 `working tree`를 함께 쓰고,
변경 범위 또는 후속 반영 commit을 결과에 연결한다. 이 디렉터리로 옮기기 전에 작성된 과거 기록은
정확한 명령이나 기준 commit이 남아 있지 않을 수 있다. 이때 기억으로 내용을 만들지 않고 기존 표현을
그대로 보존하며 `이관된 과거 기록`임을 밝힌다.

로그는 과거 결과를 보존하는 기록이다. 현재 gate 완료 여부는 계획 문서의 진행표에서 확인한다.
production source, package, sample 또는 E2E fixture가 바뀌어 이전 결과가 무효화되면 과거 행을
삭제하지 않고 새 검증 행을 추가한다.

## 언어별 로그

- [.NET](./dotnet.ko.md)
- [Java](./java.ko.md)
- [Kotlin](./kotlin.ko.md)
- [Node.js](./node.ko.md)
- [C++](./cpp.ko.md)

## 언어별 G4 finding ledger

G4 finding의 위험 신호, 비교 대안, 선택, 테스트와 상태는 시간순 실행 로그와 분리한다.
계획 문서에는 상세 finding을 넣지 않는다.

- [.NET](./dotnet-g4-refactoring-ledger.ko.md)
- Java, Kotlin, Node.js, C++: 각 언어의 G4를 시작할 때 `<lang>-g4-refactoring-ledger.ko.md`를 만든다.

## 언어별 G0 공개 계약 ledger

정식 spec의 규범 문장, 실제 public symbol, 구현 차이와 검증 증거는 계획 문서에 누적하지 않고
언어별 G0 ledger에서 관리한다.

- [.NET](./dotnet-g0-contract-ledger.ko.md)
- [Java](./java-g0-contract-ledger.ko.md)
- Kotlin, Node.js, C++: 각 언어의 G0를 시작할 때 `<lang>-g0-contract-ledger.ko.md`를 만든다.

## 언어별 G6 E2E 시나리오 ledger

공통 E2E spec의 모든 scenario ID와 언어별 fixture, selector, 실행 로그와 marker를 한 행씩 대응한다.

- [.NET](./dotnet-g6-e2e-ledger.ko.md)
- Java, Kotlin, Node.js, C++: 각 언어의 G6를 시작할 때 `<lang>-g6-e2e-ledger.ko.md`를 만든다.
