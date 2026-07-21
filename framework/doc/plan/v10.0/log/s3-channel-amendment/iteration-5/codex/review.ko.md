# Codex 독립 리뷰 결과

- 시작 파일 집합 SHA-256: `1484fde035833fab82798f57bdbf1a35563d594f1ca39b7c17ad7cb5281ed0d0`
- 시작 파일 목록 SHA-256: `00bba02f27b40d650b02d659bcd613d4e69097220565dd42419fa2a2937f7818`
- 검토 파일 수: 71개
- 종료 hash: 시작 hash와 일치
- verifier 결과: `scenario_rows=995`를 포함해 통과

## Findings

[원칙][high] `core/doc/spec/core/service/01-mesh-node.ko.md:7` — 검토 prompt가 전체 범위를 하나의
10.0.0 계약으로 표현하지만 Core 정식 문서는 10.1.0 계약으로 선언한다 — Core와 Framework의 독립적인
version 관계를 manifest와 prompt에 명시하거나, 같은 version을 의도했다면 Core 문서를 바로잡아야 한다.

[계약][medium] `framework/doc/framework/spec/server/50-runtime-monitoring.ko.md:152` — observer의
격리와 capacity는 정의하지만 취소와 종료의 공개 의미가 없다 — .NET `CancellationToken`, Node
`AbortSignal`, Java `Flow.Subscription.cancel()`, C++ `close()`에서 대기 event, 실행 중 callback,
terminal notification과 다른 observer에 미치는 영향이 달라질 수 있다 — 공통 종료 의미와 언어별
표현을 고정해야 한다.

[계약][high] `framework/doc/framework/common/e2e/config-3-pubsub.ko.md:278` — fanout observer의 느린
소비, capacity 초과와 coalescing, sequence gap 복구, 취소, manual endpoint mutation 격리를 검증하는
scenario가 없다 — public monitoring 계약 위반을 완료 gate가 발견하지 못한다 — 전용 scenario와 다섯
언어 feature-map 행을 추가해야 한다.

[계약][high] `scripts/verify-framework-doc-contracts.sh:1517` — Config 3에 적용한 public automatic
fanout evidence 검사를 다섯 Pub/Sub feature map에는 적용하지 않는다 — PS-D2~D5도 `ConnectionReady`
또는 모호한 actual connection set만 요구하므로 private 상태를 증거로 사용할 수 있다 — public
snapshot과 닫힌 event variant를 요구하고 verifier가 다섯 map을 같은 기준으로 검사해야 한다.

[계약][medium] `framework/doc/framework/common/e2e/config-3-pubsub.ko.md:255` — Publisher identity
오류 matrix가 fixed RID와 allocation을 동시에 설정한 경우만 검증한다 — store에 등록하면서 fixed RID와
allocation을 모두 생략한 publisher도 시작에 실패해야 한다 — 별도 host와 다섯 feature-map 검증을
추가해야 한다.

DOC REVIEW NOT CLEAN
