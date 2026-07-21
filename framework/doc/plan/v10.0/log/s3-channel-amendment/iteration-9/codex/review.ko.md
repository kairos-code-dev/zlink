# Iteration 9 Codex review

## 판정

`DOC REVIEW NOT CLEAN`

## Finding

- `[계약][high] framework/doc/framework/dotnet/guide/06-spot.ko.md:95` — 제거된 endpoint 문자열,
  `ChannelName(...)`과 weight 0 client 표현이 남아 있다. `Listen(port)`와
  `Channel(name).Client()/Server()` 계약으로 바꿔야 한다.
- `[계약][high] framework/doc/framework/dotnet/guide/06-spot.ko.md:704` — Logical Multicast와 Channel
  호출에 MeshName을 노출한다. ChannelName-only 호출로 바꿔야 한다.
- `[계약][high] framework/doc/framework/dotnet/guide/12-operations.ko.md:127` — 존재하지 않는 전역
  `IZLinkDrainControl`, `ZLinkDrainResult`와 health-check 확장을 사용한다. MeshName별
  `IZLinkRouteMeshRuntime` 계약으로 바꿔야 한다.
- `[계약][high] framework/doc/framework/dotnet/guide/12-operations.ko.md:173` — runtime Channel option에
  MeshName과 ChannelName을 함께 전달한다. ChannelName 하나만 사용해야 한다.
- `[계약][high] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:46` — Channel 호출에
  MeshName과 ChannelName을 함께 전달한다. ChannelName-only egress 선택으로 바꿔야 한다.
- `[계약][high] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:93` — classic fanout이
  transport topic과 Logical Multicast call·handler를 사용한다. Typed event와 전용 fanout call·handler로
  바꿔야 한다.
- `[계약][high] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:233` — MeshNode, fanout과
  STREAM builder가 이전 endpoint·ChannelName 표면을 사용한다. 다섯 exact builder 계약과 맞춰야 한다.
- `[계약][high] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:402` — Spot catalog가
  ChannelName 없는 publish와 존재하지 않는 `PublishSpot`, `FindAsync`, `CloseAsync`를 제시한다. Exact
  Spot 계약으로 교체해야 한다.
- `[계약][high] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:671` — 제거된 전역 drain
  API와 event를 다시 공개한다. MeshName별 runtime과 result·event로 통일해야 한다.
- `[계약][medium] framework/doc/framework/dotnet/internals/regression-test-matrix.ko.md:81` — Channel
  회귀가 호출자의 MeshName 인자를 전제로 한다. Process-local ChannelName owner 선택을 검증해야 한다.
- `[계약][medium] framework/doc/framework/dotnet/internals/regression-test-matrix.ko.md:386` — drain
  회귀표가 제거된 전역 drain 타입을 고정한다. MeshName별 runtime 타입으로 바꿔야 한다.
- `[계약][medium] framework/doc/framework/common/e2e/config-3-pubsub.ko.md:56` — 언어 공통 classic
  fanout 시나리오가 `.Async()`라는 특정 종결자를 요구한다. 언어 중립적인 bounded submit 완료로
  표현해야 한다.
- `[원칙][medium] scripts/verify-framework-doc-contracts.sh:1886` — exact interface만 검사해 guide의
  MeshName Channel 호출, fanout topic, 이전 builder와 전역 drain API를 놓친다. Guide 금지 패턴과 필수
  호출 형태를 구조적으로 검사해야 한다.

시작·종료 파일 집합 hash와 파일 목록 hash는 manifest와 일치했고 96개 파일 hash, verifier와
`git diff --check`가 통과했다.
