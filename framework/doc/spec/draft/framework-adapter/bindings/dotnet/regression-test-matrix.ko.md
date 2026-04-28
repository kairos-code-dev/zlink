[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [Behavior Matrix](./behavior-matrix.ko.md) | [Lifecycle](./lifecycle-and-failure-semantics.ko.md) | [use case validation](../../usecase-validation.ko.md)

# Draft -- ZLink Framework .NET Regression Test Matrix

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` framework 구현에서 항상 유지해야 할 회귀 테스트
> 기준을 정리한다.

## 1. 목적

use case validation은 설계 설명 범위를 보는 문서다. 반면 이 문서는 구현이 바뀌어도
"무엇이 깨지면 회귀인가"를 테스트 항목으로 고정한다.

## 2. CI 계층

회귀 테스트는 아래 세 계층으로 나눈다.

| 계층 | 목적 | 예시 |
|------|------|------|
| `unit` | registration validation, dispatch lookup, option parsing | duplicate registration, builder validation |
| `integration-single-process` | same-host runtime 조합 확인 | channel request/send, embedded registry, monitoring attach |
| `integration-multi-process` | 실제 topology와 reconnect 확인 | remote registry query, discovery change, spot peer 변화 |

## 3. 최소 CI 매트릭스

| 항목 | 기준 |
|------|------|
| target framework | `net8.0`, `net10.0` |
| runtime RID | `win-x64`, `win-arm64`, `linux-x64`, `linux-arm64`, `osx-x64`, `osx-arm64` |
| test mode | debug, release |

최소 지원 버전이 `net8.0`이므로, 회귀 테스트도 최소 이 두 target framework를 같이
돌려야 한다. 또한 현재 저장소의 `bindings/dotnet/runtimes/` 패키징 대상과
`.github/workflows/build.yml`이 만드는 native artifact 조합이 위 여섯 runtime RID를
기준으로 하고 있으므로, framework CI gate도 같은 범위를 기본으로 본다.

즉 `.NET` framework 회귀 테스트는 특정 OS 하나만 대표로 돌리고 끝내지 않는다.
현재 계획 기준의 필수 플랫폼 범위는 아래와 같다.

- Windows x64
- Windows ARM64
- Linux x64
- Linux ARM64
- macOS x64
- macOS ARM64

## 4. Channel Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| duplicate channel 이름 등록 | `unit` | startup validation 예외 |
| server capability에 bind endpoint 없음 | `unit` | startup validation 예외 |
| `EnableClient()` + discovery 경로 | `integration-single-process` | request/send 성공 |
| `EnableClient()` + manual 경로 | `integration-single-process` | request/send 성공 |
| client capability에 peer acquisition 경로 없음 | `unit` | startup validation 예외 |
| same capability에 discovery/manual 혼용 | `unit` | startup validation 예외 |
| publisher capability에 bind endpoint 없음 | `unit` | startup validation 예외 |
| publisher-only channel | `integration-single-process` | publish submit 성공 |
| subscriber discovery attach | `integration-multi-process` | remote publish 수신 |
| HTTP handler에서 `IZLinkClient` 사용 | `integration-single-process` | route handler와 same DI container에서 정상 동작 |

## 5. Spot Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| duplicate `spotName` factory | `unit` | startup validation 예외 |
| `UseSpotDiscovery(...)` 없이 `AddSpotNode(...)` | `unit` | startup validation 예외 |
| `CreateAsync(spotName)` | `integration-single-process` | `SpotRid`, `SpotName`, `Created` 일관성 확인 |
| `GetAsync(...)`, `ListAsync(...)` | `integration-single-process` | manager 조회 결과 일관성 확인 |
| `OnInitializeAsync(...)` handler resolve | `integration-single-process` | per-spot scope DI 정상 동작 |
| local spot publish | `integration-single-process` | subscriber 수신 |
| outbound-only 외부 publish client | `integration-multi-process` | target SPOT channel publish 성공 |
| spot 제거 후 scope 정리 | `integration-single-process` | 이후 callback 미발생, dispose 완료 |

## 6. Stream Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| 같은 node에 raw/packet 동시 등록 | `unit` | startup validation 예외 |
| packet session node | `integration-single-process` | `OnDispatchAsync(...)` 호출 |
| raw session node | `integration-single-process` | `OnDispatchAsync(...)` 호출 |
| `OnConnectedAsync(...)` | `integration-multi-process` | `ConnectionReady` 이후 1회 호출 |
| `OnErrorAsync(...)` 범위 | `integration-multi-process` | transport error만 session callback으로 전달 |
| peer metadata 표면 | `integration-single-process` | `SessionId`, `RoutingId`, `LocalAddr`, `RemoteAddr` 값 확인 |

## 7. Registry / Monitoring Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| embedded registry startup 순서 | `integration-single-process` | framework discovery가 registry bind 뒤 시작 |
| registry query DI | `integration-single-process` | `IZLinkRegistryQuery` resolve 및 snapshot 조회 성공 |
| remote query client | `integration-multi-process` | topology snapshot 조회 성공 |
| monitoring source name mismatch | `unit` | startup validation 예외 |
| registry polling diff | `integration-multi-process` | topology/status/service summary event 발생 |
| spot polling diff | `integration-multi-process` | status/peers/subjects event 발생 |

## 8. Release Gate

아래 조건을 모두 만족해야 구현 완료로 본다.

1. `unit`, `integration-single-process`, `integration-multi-process` 전부 통과
2. `net8.0`, `net10.0` 둘 다 통과
3. 위 여섯 runtime RID 전체에서 CI gate가 통과
4. happy-path 샘플과 대표 failure-path가 각각 한 번 이상 커버됨
5. `behavior-matrix.ko.md`에 있는 비허용 조합이 모두 테스트로 고정됨

즉 샘플이 한 번 실행되는 것만으로는 충분하지 않고, startup validation과 runtime
failure semantics가 테스트로 같이 고정돼야 한다. native backend가 이미 해당
platform을 지원하더라도, framework는 그 위에서 registration, lifecycle, DI,
monitoring 계층이 추가되므로 platform gate를 별도로 유지해야 한다.
