# S3 문서 리뷰 finding — iteration 3

## 1. 판정

동결 뒤 coordinator의 scoped scan에서 iteration 2와 같은 원칙 위반이 다른 C++ 정식 spec 절에 남아
있는 것을 확인했다. 두 reviewer 결과를 기다리지 않고 iteration을 무효화했으며, 수정 뒤 새 revision에서
두 reviewer를 모두 처음부터 실행한다.

## 2. finding과 조치

| ID | 축 | 심각도 | 대상 | 문제 | 상태 |
|---|---|---|---|---|---|
| S3-I3-C01 | 원칙 | 높음 | `cpp/02-framework-interfaces.ko.md` | binding 구현 대응표, `src/runtime` 경로와 공개 계약이 아닌 runtime type 목록이 정식 public interface spec에 남아 있음 | 완료 |
| S3-I3-C02 | 원칙 | 높음 | `cpp/60-http-hosting.ko.md` | runtime source 파일, 구현 library, worker/executor와 내부 시작 순서가 정식 public HTTP 계약에 남아 있음 | 완료 |
| S3-I3-C03 | 원칙 | 중간 | C++ exact interface·HTTP hosting | application 전체 sample과 TicTacToe 사용 흐름이 public contract 문서에 들어 있음 | 완료 |
| S3-I3-C04 | 원칙 | 낮음 | Java·Node system structure | 정식 package 배포 계약과 지원 기능이 `배포 계획`, `목표`로 표현됨 | 완료 |

## 3. 수정 검증

- C++ interface spec은 binding public dependency 경계, 설치 header 제약과 실제 public type만 남겼다.
- C++ HTTP hosting spec은 설치 header, lifecycle, handler 우선순위와 공개 계약 결정만 남겼다.
- runtime source 경로, 구현 library 선택, worker·executor와 비계약 type 목록을 제거했다.
- application sample은 언어별 guide·sample 문서가 소유하도록 정식 spec에서 제거하고 목차 번호와 교차
  참조를 다시 맞췄다.
- Java·Node package 문구를 현재 10.0.0 `배포 계약`과 `지원 기능`으로 고쳤다.
- exact code fixture와 declaration inventory를 갱신했다.
- scoped 내부 경로 검색은 no-hit이며 `FRAMEWORK DOC CONTRACTS CLEAN`과 `git diff --check`가 통과한다.
