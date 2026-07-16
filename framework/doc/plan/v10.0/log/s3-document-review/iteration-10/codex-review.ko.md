# Codex 문서 리뷰 — iteration 10

시작·종료 시 181/181 파일 hash, aggregate
`0b54af9ba0b55fc32860d809e3c3b6e3c09d136719fd1e7cd17210e31fb1ef9d`와 file-list
`e254cb16291ee79220c53296a32d2442a3f6ac29ba448dcd2a9c95b15a91b60e`가 일치했다. 파일은 수정하지
않았고 최종 판정은 `NOT CLEAN`이다.

| ID | 축·severity | 1차 위치 | finding |
|---|---|---|---|
| C10-01 | 1차 소스·high | `framework/doc/framework/cpp/guide/07-channel-messaging.ko.md:12` | 제거 대상 `add_client_server_channel`, `enable_server/client`, `SpotMesh`와 자동 bridge를 현재 API처럼 설명한다. 같은 문서 32~34, 107~110, 247~265, 317~318, 412~414와 `cpp/guide/10-stream.ko.md:151-152`를 RouteMesh·MeshNode·ChannelName 계약으로 전환해야 한다. |
| C10-02 | 1차 소스·high | `framework/doc/framework/spec/server/52-message-flow-tracing.ko.md:46-50` | 공통 flow event의 닫힌 field·outcome·reason 계약과 C++ dispatch error event, Java phase/outcome, Node actor error/action 표면이 서로 다르다. 공통 owner와 observer 경로를 하나로 정한 뒤 다섯 언어 exact interface를 맞춰야 한다. |
| C10-03 | 1차 소스·high | `framework/doc/framework/spec/server/languages/java/01-system-structure.ko.md:942-966` | 전역 `ZLinkDrainControl`·`ZLinkDrainResult`와 `java/02-handler-interfaces.ko.md:1272-1328`의 mesh별 `ZLinkRouteMeshRuntime`·`ZLinkMeshDrainResult`가 충돌한다. Kotlin은 mesh별 모델을 사용한다. |
| C10-04 | 1차 소스·high | `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md:311-319` | RL-D2가 exact interface에 없는 runtime error sink와 내부 이름을 요구한다. 공통 공개 sink와 다섯 언어 exact 표면을 먼저 고정하거나 public-contract gap으로 분리해야 한다. |
| C10-05 | 1차 소스·medium | `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md:280` | payload decode 실패를 `HandlerException`으로 분류하지만 공통 tracing 계약은 `decode_error`와 `handler_exception`을 분리한다. |
| C10-06 | 1차 소스·medium | `framework/doc/framework/spec/server/languages/java/02-handler-interfaces.ko.md:1351-1353` | timeout 우선순위가 MeshNode 설정을 채널별이라고 설명한다. 실제 owner는 호출별, MeshNode별, framework 전역 순서다. |
| C10-07 | 1차 소스·medium | `framework/doc/framework/spec/04-async-execution-policy.ko.md:107-112` | Java·Kotlin·Node·C++ exact async 표현을 구현 단계로 미룬다. 다섯 exact owner와 고정된 반환형·ordering·오류 표현을 현재 목표 계약으로 연결해야 한다. |
| C10-08 | 1차 소스·medium | `framework/doc/framework/spec/90-implementation-gap.ko.md:235-253` | 존재하지 않는 `05-framework-api` 절 번호와 owner를 인용한다. 같은 drift가 547행과 C++ exact·guide에 반복되며 dispatch action 공통 owner도 없다. |
| C10-09 | 원칙·low | `framework/doc/framework/cpp/guide/10-stream.ko.md:81` | `09-actor-session.ko.md#4-session-actor-바인딩` fragment가 실제 heading slug와 다르다. |
| C10-10 | 원칙·low | `framework/doc/framework/spec/90-implementation-gap.ko.md:469` | 694·698행, ShoppingMall 550행, `.NET` stream guide 30행, TicTacToe 여러 행에 객체·연결·상태 의인화와 구어체가 남아 있다. 중립적 기술 표현으로 바꿔야 한다. |
