# S3 문서 독립 리뷰 — iteration 24 (Codex)

## 실행 증거

- provider/model: OpenAI / GPT-5.6 Codex
- session ID: `019f6e8e-a34d-7571-9f4e-9f7085562af3`
- 시작·종료 HEAD: `169c458ed238228d7a23cea089c8c467c96b953c`
- `scope-files.txt` SHA-256: `06f3695c8571e2c253c8f7af522dbe7d9fd54596c94eb5f724743c392087470d`
- `scope-files.sha256` SHA-256: `517016d8ad95ca1c334b2ec747e3219864a4a5ee99ef5c971a32da4aec0ae4a9`
- 파일별 hash: 205/205 일치
- 205개 파일 47,284행 전수 검토, contract verifier CLEAN
- read-only 실행, 작성 파일 0개, 기존 dirty worktree 보존

## Findings

[1차소스][high] framework/doc/framework/spec/stream-connector/32-stream-connector.ko.md:441 — `RemoteError`를 request ID가 없거나 부합하지 않는 경우로만 정의해 matching Error의 오류 종류가 계약에서 빠진다 — 같은 문서 §5.2는 matching Error가 pending request를 실패시킨다고 규정하고, C++·Java·Node·.NET 구현은 유효한 matching Error도 `RemoteError`로 처리한다 — `RemoteError`를 유효한 서버 Error payload 전체의 오류 종류로 정의하고, matching 여부는 pending request 실패와 error event 전달 경로만 구분하도록 고친다.

[1차소스][high] framework/doc/framework/spec/stream-connector/32-stream-connector.ko.md:452 — 모든 `FrameDecodeFailed`가 연결을 종료한다고 규정했지만 invalid Error JSON의 실제 처리는 네 언어 모두 해당 request 실패 또는 error event에 그치며 연결을 종료하지 않는다 — Java·Node·C++·.NET 구현과 Java gap의 완료 기록이 같은 비종료 동작을 증명한다 — invalid Error payload도 terminal로 만들 계약이면 네 언어 gap을 열고, 비종료가 의도라면 operation matrix에 이 경우를 명시적으로 분리한다.

[1차소스][high] framework/doc/framework/spec/stream-connector/languages/java/03-stream-connector.ko.md:223 — Java exact interface는 네 call builder에 `flowFrom(...)`을 요구하지만 Java gap은 그 공개 표면을 배제한 내부 flow context 구현을 충족으로 닫았다 — 같은 선언이 :233, :243, :253에도 있으며 동결 HEAD의 네 Java call interface에는 `flowFrom`이 없다 — 공통 async-context 계약에 따라 내부 전파를 정식 선택할 경우 exact interface에서 네 선언과 명시적 전달 설명을 고치고, exact interface를 유지할 경우 gap을 다시 연다.

[1차소스][high] framework/doc/framework/spec/http-client/09-error-model.ko.md:17 — Node timeout을 프로그램적으로 식별할 수 있어야 한다는 계약을 Node 표현이 충족하지 못한다 — Node 표현은 timeout을 일반 transport 실패와 동일한 `requestFailed(isRetriable=true)`로만 규정한다 — 새 공통 kind 없이 유지하려면 Node의 안정적인 cause type·code 같은 timeout discriminator를 exact interface에 고정한다.

[1차소스][medium] framework/languages/node/e2e/SpotService/feature-map.ko.md:81 — `SM-F3`와 `SM-F5`를 포함한 묶음을 구현 완료로 판정해 같은 문서의 개별 상태와 모순된다 — 개별 행은 두 시나리오를 모두 `전환 필요`로 기록하며 완료 증거도 제시하지 않는다 — 묶음 판정을 혼합 상태로 바꾸거나 실제 검증 증거를 추가한 뒤 개별 행과 함께 완료로 전환한다.

[1차소스][medium] framework/languages/node/e2e/SpotService/feature-map.ko.md:55 — `SM-D10`을 완료로 표시했지만 새 send를 버린다는 queue admission 계약을 검증하지 않고 `.NET`과 다른 계약인 것처럼 서술한다 — 공통 connector 계약은 기존 메시지를 유지하고 새 메시지를 버리도록 모든 언어에 요구하지만 Node 시나리오는 drop 횟수와 총수만 검사한다 — Node도 첫 메시지 보존과 이후 메시지 거부를 payload identity로 단언할 때까지 전환 대상으로 표시한다.

[원칙][low] framework/doc/framework/spec/gaps/cpp.ko.md:940 — `IMP-CP-35` 내부 링크가 존재하지 않는 `#상세_1` anchor를 사용한다 — 두 번째 `### 상세`의 pymdownx anchor는 `#상세-1`이며 같은 잘못된 링크가 :1023에도 반복된다 — 두 링크를 실제 anchor로 고치거나 상세 제목을 고유하게 명명한다.
