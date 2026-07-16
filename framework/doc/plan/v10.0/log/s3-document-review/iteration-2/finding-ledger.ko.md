# S3 문서 리뷰 finding — iteration 2

## 1. 판정

Codex가 원칙 축 9건과 1차 소스 축 14건을 발견했다. Claude Sonnet 실행은 유효한 결과 없이 종료되었으므로
iteration 2는 clean이 아니다. 모든 finding을 수정하고 전체 범위를 새 revision으로 동결한 뒤 두 reviewer를
모두 다시 실행한다.

## 2. finding 목록

| ID | 축 | 심각도 | 대상 | 상태 |
|---|---|---|---|---|
| S3-I2-C01 | 원칙 | 높음 | C++ exact interface의 내부 runtime 구조 | 완료 |
| S3-I2-C02 | 원칙 | 높음 | Embedded HTTP 정식 spec의 구현 계획 | 완료 |
| S3-I2-C03 | 원칙 | 낮음 | Embedded HTTP 잘못된 문서 경로 | 완료 |
| S3-I2-C04 | 원칙 | 낮음 | Java handler 잘못된 문서 경로 | 완료 |
| S3-I2-C05 | 원칙 | 중간 | Node SpotService 표 구분자 | 완료 |
| S3-I2-C06 | 원칙 | 낮음 | Core SUB의 control plane 표기 | 완료 |
| S3-I2-C07 | 원칙 | 낮음 | Core socket 목차의 data plane 표기 | 완료 |
| S3-I2-C08 | 원칙 | 낮음 | Config 2 actor 위치 의인화 | 완료 |
| S3-I2-C09 | 원칙 | 낮음 | GameQuest 연결 구어체 | 완료 |
| S3-I2-C10 | 1차 소스 | 높음 | C++ Config 8의 이전 ID·경로 | 완료 |
| S3-I2-C11 | 1차 소스 | 높음 | Java Config 8의 이전 ID·경로 | 완료 |
| S3-I2-C12 | 1차 소스 | 높음 | Kotlin Config 8의 이전 ID·경로 | 완료 |
| S3-I2-C13 | 1차 소스 | 중간 | Node Config 8의 ID 범위 행 | 완료 |
| S3-I2-C14 | 1차 소스 | 중간 | Kotlin Config 10의 ID 범위 행 | 완료 |
| S3-I2-C15 | 1차 소스 | 중간 | Node Config 10의 ID 병합 행 | 완료 |
| S3-I2-C16 | 1차 소스 | 높음 | C++ Config 1의 RM-B3 | 완료 |
| S3-I2-C17 | 1차 소스 | 높음 | Kotlin Config 1의 RM-B3 | 완료 |
| S3-I2-C18 | 1차 소스 | 중간 | Kotlin Config 6의 SF-E1 | 완료 |
| S3-I2-C19 | 1차 소스 | 높음 | C++ Config 2의 SM-C6 | 완료 |
| S3-I2-C20 | 1차 소스 | 높음 | .NET Config 2의 SM-C6 | 완료 |
| S3-I2-C21 | 1차 소스 | 높음 | Java Config 2의 SM-C6 | 완료 |
| S3-I2-C22 | 1차 소스 | 높음 | Kotlin Config 2의 네 시나리오 | 완료 |
| S3-I2-C23 | 1차 소스 | 높음 | Node Config 2의 SM-C6 | 완료 |

상세 근거와 수정 제안은 [`codex-raw-output.txt`](./codex-raw-output.txt)를 따른다.

## 3. 수정 검증

- C++ exact interface에서 내부 runtime 파일 대응과 구현 절차를 제거하고 공개 계약 경계만 남겼다.
- Embedded HTTP spec에서 단계별 계획, 내부 component 구조와 잘못된 문서 경로를 제거하고 공개 계약과
  고정 기본값을 명시했다.
- 문서 용어·표 구분자·연결 표현 7건을 원칙에 맞게 고쳤다.
- Config 8은 네 언어에서 canonical `TD-*` 27개, Config 10은 Kotlin과 Node에서 canonical
  `ST-*` 20개를 각각 독립 행으로 기록했다.
- Config 1·2·6의 누락 ID를 추가하고 구현 증거가 없는 항목은 `전환 필요`로 기록했다.
- `verify-framework-doc-contracts.sh`에 55개 feature map과 공통 Config 1~11의 scenario 행 누락 검사를
  추가했다.
- 검증 결과는 `FRAMEWORK DOC CONTRACTS CLEAN`과 `git diff --check`다.
