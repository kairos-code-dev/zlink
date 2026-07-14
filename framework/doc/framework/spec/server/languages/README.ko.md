# 언어별 Framework 공개 계약

이 디렉토리는 공통 framework 의미가 각 언어의 public API로 보이는 정확한 형태를
정리한다. 여기에 기록한 시그니처는 해당 언어 구현이 따라야 하는 정식 계약이다.
공통 의미와 계약 변경 절차는 각각
[공통 스펙](../../README.ko.md)과
[공개 계약 관리](../../00-public-contract-governance.ko.md)를 따른다.

| 언어 | 공개 계약 |
|------|-----------|
| `.NET` | [dotnet](dotnet/README.ko.md) |
| Java | [java](java/README.ko.md) |
| Kotlin | [kotlin](kotlin/README.ko.md) |
| Node.js framework | [node](node/README.ko.md) |
| TypeScript browser connector | [typescript](../../stream-connector/languages/typescript/README.ko.md) |
| C++ | [cpp](cpp/README.ko.md) |

언어별 스펙은 서로의 시그니처를 복사하는 문서가 아니다. 같은 공통 동작을 해당
언어 사용자가 자연스럽게 사용할 수 있는 public contract로 고정한다. 현재 구현과
다른 부분은 언어별 스펙을 축소하지 않고 [구현 차이](../../90-implementation-gap.ko.md)에
기록한다.
