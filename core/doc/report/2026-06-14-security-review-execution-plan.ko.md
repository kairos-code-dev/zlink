# 보안 검토 실행 순서

- 작성일: 2026-06-14
- 대상 범위: `core/doc/report/*.ko.md`
- 상태: 실행 전 문서 순서 계획. 코드 수정 없음.

## 목적

이 문서는 보안 검토 결과를 실제 수정 작업으로 옮길 때 **어떤 리포트부터 처리할지**만 정리한다. 구체적인 취약점 설명, 코드 위치, 수정 방향, 기능·성능 영향은 각 리포트에 이미 있으므로 여기서 반복하지 않는다.

각 단계는 해당 리포트를 열고 현재 코드와 다시 대조한 뒤 진행한다. 리포트와 코드가 다르면 먼저 리포트를 고친다.

이 문서를 goal로 두고 진행할 때는 아래 순서를 빠뜨리지 않는다. 작업이 길어져 중단되거나 재개되더라도, 마지막으로 완료한 문서와 독립 리뷰 판정이 기록되어 있어야 한다.

2026-06-14부터 새 리뷰 게이트는 Codex 에이전트 리뷰로 진행한다. 이전에 이미 완료한 항목의 Claude 리뷰 기록은 당시 검증 결과로 보존한다.

## Goal 진행 규칙

1. 한 번에 하나의 리포트만 진행한다.
2. 다음 리포트로 넘어가기 전에 현재 리포트의 코드 수정, 테스트, 자체 리뷰, Codex 에이전트 리뷰, 문서 갱신을 모두 끝낸다.
3. Codex 에이전트 리뷰가 "추가 이슈 없음"이라고 명확히 판정하지 않은 리포트는 완료로 표시하지 않는다.
4. 테스트를 실행하지 못한 경우에는 완료로 표시하지 않는다. 실행 불가 사유와 대체 확인 방법을 해당 리포트에 남긴다.
5. 수정 중 새 이슈가 발견되면 현재 리포트 또는 관련 리포트에 항목을 추가하고, 실행 순서가 바뀌어야 하는지 이 문서도 갱신한다.
6. core runtime 또는 core public header를 수정한 경우에는 `bindings/dev_sync_local_core_libs.sh`로 바인딩용 native 산출물을 동기화한 뒤 바인딩 검증을 진행한다.
7. 작업을 재개할 때는 아래 진행 현황 표에서 첫 번째 미완료 항목부터 시작한다.

## 진행 현황

상태 값은 `대기`, `진행 중`, `수정 완료`, `테스트 완료`, `Codex 리뷰 통과`, `문서 갱신 완료`, `종결` 중 하나로 기록한다.

| 순서 | 리포트 | 현재 상태 | 마지막 확인 |
|------|--------|-----------|-------------|
| 1 | [README.ko.md](README.ko.md) | 종결 | 2026-06-14: 링크·심각도·우선순위 대조, Claude 추가 이슈 없음 |
| 2-1 | [2026-06-14-cpp-framework-security-review.ko.md](2026-06-14-cpp-framework-security-review.ko.md) | 종결 | 2026-06-14: CR1 수정·테스트·Claude 추가 이슈 없음 |
| 2-2 | [2026-06-14-node-framework-security-review.ko.md](2026-06-14-node-framework-security-review.ko.md) | 종결 | 2026-06-14: C1/C2/S1/S2 수정·테스트·Claude 추가 이슈 없음 |
| 2-3 | [2026-06-14-java-framework-security-review.ko.md](2026-06-14-java-framework-security-review.ko.md) | 종결 | 2026-06-14: F1/F2 수정·테스트·Claude 추가 이슈 없음 |
| 2-4 | [2026-06-14-dotnet-framework-security-review.ko.md](2026-06-14-dotnet-framework-security-review.ko.md) | 종결 | 2026-06-14: D1/D2/D5 수정·관련 테스트 통과·Claude 추가 이슈 없음 |
| 3-1 | [2026-06-14-cpp-framework-security-review.ko.md](2026-06-14-cpp-framework-security-review.ko.md) | 종결 | 2026-06-14: CR2/H1 수정·관련 테스트 통과·Claude 추가 이슈 없음 |
| 3-2 | [2026-06-14-java-framework-security-review.ko.md](2026-06-14-java-framework-security-review.ko.md) | 종결 | 2026-06-14: F3 수정·F5 문서 경고·stream connector test 통과·Claude 추가 이슈 없음 |
| 4 | [2026-06-13-core-src-security-review.ko.md](2026-06-13-core-src-security-review.ko.md) | 진행 중 | 2026-06-14: #1 mtrie, #2 WS/WSS buffering, #3 포트 파싱, #4 IPC unlink, #5 decoder allocator, #6 message/send 가드 수정·core/C++ binding 검증 통과, #1/#2/#3/#4/#6 Claude 추가 이슈 없음, #5 Codex 에이전트 추가 이슈 없음, C binding은 기존 C-BINDING-001 버전 테스트 불일치로 실패 |
| 5-1 | [2026-06-14-cpp-framework-security-review.ko.md](2026-06-14-cpp-framework-security-review.ko.md) | 대기 | 동시성·리소스 항목 |
| 5-2 | [2026-06-14-java-framework-security-review.ko.md](2026-06-14-java-framework-security-review.ko.md) | 대기 | 동시성·리소스 항목 |
| 5-3 | [2026-06-14-node-framework-security-review.ko.md](2026-06-14-node-framework-security-review.ko.md) | 대기 | 동시성·리소스 항목 |
| 5-4 | [2026-06-14-dotnet-framework-security-review.ko.md](2026-06-14-dotnet-framework-security-review.ko.md) | 대기 | 동시성·리소스 항목 |
| 6-1 | [2026-06-14-bindings-dotnet-security-review.ko.md](2026-06-14-bindings-dotnet-security-review.ko.md) | 대기 | - |
| 6-2 | [2026-06-14-bindings-java-security-review.ko.md](2026-06-14-bindings-java-security-review.ko.md) | 대기 | - |
| 6-3 | [2026-06-14-bindings-python-security-review.ko.md](2026-06-14-bindings-python-security-review.ko.md) | 대기 | - |
| 6-4 | [2026-06-14-bindings-rust-security-review.ko.md](2026-06-14-bindings-rust-security-review.ko.md) | 대기 | - |
| 6-5 | [2026-06-14-bindings-go-security-review.ko.md](2026-06-14-bindings-go-security-review.ko.md) | 대기 | - |
| 6-6 | [2026-06-14-bindings-c-security-review.ko.md](2026-06-14-bindings-c-security-review.ko.md) | 대기 | - |
| 6-7 | [2026-06-14-bindings-node-security-review.ko.md](2026-06-14-bindings-node-security-review.ko.md) | 대기 | - |
| 6-8 | [2026-06-14-bindings-cpp-security-review.ko.md](2026-06-14-bindings-cpp-security-review.ko.md) | 대기 | - |

## 실행 순서

### 1. 전체 인덱스 확인

- [README.ko.md](README.ko.md)

먼저 전체 리포트 목록과 공통 우선순위를 확인한다. 이후 단계에서 개별 문서를 처리하면서 README의 상태도 함께 갱신한다.

### 2. Framework 공통 원격 DoS 항목

아래 4개 문서를 먼저 처리한다. 여러 언어에 같은 수신 크기 상한·압축 해제 상한 문제가 반복되어 있으므로, 옵션 의미와 실패 동작이 언어별로 갈라지지 않게 순서를 묶는다.

1. [2026-06-14-cpp-framework-security-review.ko.md](2026-06-14-cpp-framework-security-review.ko.md)
2. [2026-06-14-node-framework-security-review.ko.md](2026-06-14-node-framework-security-review.ko.md)
3. [2026-06-14-java-framework-security-review.ko.md](2026-06-14-java-framework-security-review.ko.md)
4. [2026-06-14-dotnet-framework-security-review.ko.md](2026-06-14-dotnet-framework-security-review.ko.md)

### 3. Framework 인증·TLS·HTTP 항목

2단계에서 같은 문서들을 처리하더라도, 인증·TLS·HTTP 항목은 별도 검증이 필요하므로 같은 문서를 다시 확인한다.

1. [2026-06-14-cpp-framework-security-review.ko.md](2026-06-14-cpp-framework-security-review.ko.md)
2. [2026-06-14-java-framework-security-review.ko.md](2026-06-14-java-framework-security-review.ko.md)

### 4. Core 런타임 항목

- [2026-06-13-core-src-security-review.ko.md](2026-06-13-core-src-security-review.ko.md)

Core는 하위 계층이라 영향 범위가 넓다. Framework의 원격 입력 상한과 인증 문제를 먼저 정리한 뒤, core 리포트의 항목을 순서대로 처리한다.

Core 수정이 끝나면 바인딩이 참조하는 local core library를 반드시 갱신한다.

1. core를 빌드한다.
2. `/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh`를 실행한다.
3. 동기화된 native library를 사용하는 바인딩 테스트를 실행한다.
4. 실행 결과와 동기화 여부를 core 리포트와 이 실행 순서 문서에 기록한다.

### 5. Framework 동시성·리소스 항목

원격 입력과 인증 항목을 처리한 뒤, 각 framework 문서의 동시성·리소스 항목을 다시 확인한다.

1. [2026-06-14-cpp-framework-security-review.ko.md](2026-06-14-cpp-framework-security-review.ko.md)
2. [2026-06-14-java-framework-security-review.ko.md](2026-06-14-java-framework-security-review.ko.md)
3. [2026-06-14-node-framework-security-review.ko.md](2026-06-14-node-framework-security-review.ko.md)
4. [2026-06-14-dotnet-framework-security-review.ko.md](2026-06-14-dotnet-framework-security-review.ko.md)

### 6. 바인딩 라이브러리 항목

바인딩 리포트는 대부분 native library 로딩 전제, native buffer 수명, 공개 계약 정리에 가깝다. Framework와 core의 원격 트리거 항목을 먼저 처리한 뒤 진행한다.

1. [2026-06-14-bindings-dotnet-security-review.ko.md](2026-06-14-bindings-dotnet-security-review.ko.md)
2. [2026-06-14-bindings-java-security-review.ko.md](2026-06-14-bindings-java-security-review.ko.md)
3. [2026-06-14-bindings-python-security-review.ko.md](2026-06-14-bindings-python-security-review.ko.md)
4. [2026-06-14-bindings-rust-security-review.ko.md](2026-06-14-bindings-rust-security-review.ko.md)
5. [2026-06-14-bindings-go-security-review.ko.md](2026-06-14-bindings-go-security-review.ko.md)
6. [2026-06-14-bindings-c-security-review.ko.md](2026-06-14-bindings-c-security-review.ko.md)
7. [2026-06-14-bindings-node-security-review.ko.md](2026-06-14-bindings-node-security-review.ko.md)
8. [2026-06-14-bindings-cpp-security-review.ko.md](2026-06-14-bindings-cpp-security-review.ko.md)

## 문서 갱신 규칙

각 문서를 처리한 뒤에는 다음 순서로 확인한다.

1. 해당 리포트의 항목을 기준으로 코드를 수정한다.
2. 관련 테스트와 최소 재현 검증을 실행한다.
3. Codex 에이전트에 해당 리포트와 수정된 코드를 함께 주고, 문서만 보지 말고 실제 코드를 직접 대조해 리뷰하도록 요청한다.
4. Codex 에이전트 리뷰에서 이슈가 남으면 코드를 다시 수정하고, 테스트를 다시 실행한 뒤 Codex 에이전트 리뷰를 반복한다.
5. Codex 에이전트가 해당 순서에 대해 추가 이슈가 없다고 판정하면 기능·성능 영향이 리포트의 예상과 다른지 확인한다.
6. 남은 이슈가 있으면 같은 리포트에 다시 반영하고, 닫힌 항목은 상태를 갱신한다.

## 수정 후 리뷰 항목

각 리포트 처리 후에는 아래 항목을 반드시 확인한다.

1. 리포트가 지적한 코드 경로가 실제로 바뀌었는가.
2. 정상 입력의 공개 동작이 의도치 않게 바뀌지 않았는가.
3. 원래 문제를 재현하던 입력이 이제 할당, 전송, 로딩, callback 등록 전에 안전하게 실패하는가.
4. 실패 시 반환값, 예외, errno, 로그가 언어별 공개 계약과 맞는가.
5. 새 상한이나 검증이 hot path 성능을 불필요하게 악화시키지 않는가.
6. 기존 테스트뿐 아니라 리포트 항목에 맞는 회귀 테스트가 추가되었는가.
7. 같은 패턴이 다른 언어 또는 같은 계층의 다른 파일에 남아 있지 않은가.
8. README와 해당 리포트의 상태가 실제 코드 상태와 일치하는가.

## 리포트별 완료 체크리스트

각 리포트는 아래 체크리스트가 모두 채워져야 완료된다.

| 항목 | 확인 내용 |
|------|-----------|
| 코드 대조 | 리포트의 파일·라인·호출 경로가 현재 코드와 맞는지 확인했다. |
| 수정 범위 | 리포트의 모든 열린 항목을 수정, 오탐 종결, 또는 별도 문서 이관 중 하나로 처리했다. |
| 테스트 | 항목별 회귀 테스트 또는 최소 재현 검증을 실행했다. |
| Core 배포 | core runtime/header 수정 시 `bindings/dev_sync_local_core_libs.sh`를 실행했고 바인딩 검증을 시작했다. |
| 자체 리뷰 | 기능·성능 영향과 같은 패턴 잔존 여부를 직접 다시 확인했다. |
| Codex 리뷰 | Codex 에이전트가 실제 코드와 리포트를 대조했고 "추가 이슈 없음" 판정을 냈다. |
| 문서 갱신 | 해당 리포트, README, 이 실행 순서 문서의 상태를 갱신했다. |
| 재개 정보 | 마지막 테스트 명령, 독립 리뷰 요약, 남은 이슈가 문서에 남아 있다. |

체크리스트 중 하나라도 빠지면 해당 리포트는 `종결` 상태가 아니다.

## Codex 리뷰 게이트

각 실행 순서는 Codex 에이전트 리뷰를 통과해야 다음 순서로 넘어갈 수 있다.

Codex 에이전트에는 다음 내용을 반드시 전달한다.

1. 현재 처리 중인 리포트 링크
2. 수정된 코드 파일 목록
3. 실행한 테스트 명령과 결과 요약
4. core 수정 후 `bindings/dev_sync_local_core_libs.sh`를 실행했는지 여부
5. 리포트의 어떤 항목을 닫으려는지
6. 문서만 보지 말고 실제 코드와 테스트를 직접 확인하라는 지시

Codex 에이전트 리뷰 요청은 아래 형식을 따른다.

```text
작업 위치: /home/hep7/project/kairos/zlink

역할: 코드 리뷰어. 파일을 수정하지 말고 리뷰 결과만 출력해줘.

검토 대상 리포트:
- <현재 처리 중인 core/doc/report/... 문서>

검토 대상 코드:
- <이번 수정에서 바뀐 파일 목록>

실행한 검증:
- <테스트 명령과 결과 요약>

Core 배포:
- <core 수정이 있으면 bindings/dev_sync_local_core_libs.sh 실행 여부와 결과>

중요 지시:
- 문서만 읽고 판단하지 말고 반드시 실제 코드를 직접 열어서 확인해줘.
- 리포트가 지적한 문제가 수정된 코드에서 실제로 닫혔는지 확인해줘.
- 기능 또는 성능 회귀 가능성이 있으면 구체적인 파일과 근거를 들어 지적해줘.
- 같은 패턴이 같은 언어 또는 같은 계층의 다른 파일에 남아 있는지 확인해줘.
- 이슈가 있으면 심각도, 근거 파일/라인, 수정 제안을 적어줘.
- 더 이상 이슈가 없으면 "추가 이슈 없음"이라고 명확히 적어줘.
- 한국어로 답해줘.
```

반복 규칙:

1. Codex 에이전트가 이슈를 지적하면 해당 순서는 완료된 것으로 보지 않는다.
2. 지적된 이슈를 수정하고 관련 테스트를 다시 실행한다.
3. 같은 리포트와 수정 파일을 대상으로 Codex 에이전트 리뷰를 다시 요청한다.
4. Codex 에이전트가 "추가 이슈 없음"이라고 명확히 판정할 때까지 반복한다.
5. 그 뒤에만 다음 리포트 또는 다음 순서로 넘어간다.

리뷰가 끝나면 다음 문서를 갱신한다.

1. 해당 리포트의 상태와 결론
2. [README.ko.md](README.ko.md)의 최고 심각도와 처리 우선순위
3. 이 실행 순서 문서의 남은 순서가 여전히 맞는지 여부

## 완료 기준

모든 리포트가 다음 중 하나의 상태를 가져야 한다.

- 코드 수정 완료
- 문서·테스트 보강 완료
- 오탐 또는 의도된 동작으로 종결
- 별도 설계 문서로 이관
