[English](README.md) | [한국어](README.ko.md)

[스펙 목차](https://kairos-code-dev.github.io/zlink/core/ko/spec/) · [바인딩 정책](../README.ko.md)

# Go 바인딩 구현 청사진

이 문서는 기대되는 Go 라이브러리 형태를 정의한다. 모든 export 식별자를 빠짐없이
나열한 목록은 아니다. 구체적인 공개 계약 소스는 `bindings/go/contracts/`이다.

Go 구현은 `contracts` projection, 구현 소유자 파일, 테스트, 샘플, perf runner,
런타임 동작이 이 청사진을 따르고 `core/include/zlink.h`의 안정된 기능을 Go
관용적 API로 매핑할 때 정합된 상태가 된다.

이 README는 `../README.md`의 공통 정책에 정합된 후의 완성된 Go 바인딩 형태를
설명하며, Go 리팩터링 작업의 가이드 역할도 한다. 리팩터링 동안 각 공개 계약, 구현
소유자, cgo/네이티브 브리지 헬퍼, 테스트, 샘플, perf import가 어디에 속하는지
결정할 때 이 문서를 사용한다. Go 바인딩이 정합되었다고 선언되면 export 식별자,
GoDoc, 테스트, 샘플, perf, 런타임 동작은 이 문서와 일치해야 한다.

Go 리팩터링은 호환성을 깨는 정리 작업이다. 리팩터링 이전의 공개 표면을 유지하기
위한 호환성 shim, deprecated wrapper, 중복 생성 경로, 옛 export alias는 남기지
않는다.

이 바인딩은 공통 바인딩 아키텍처 맵을 Go 명명으로 따른다. 공개 `contracts`
패키지가 consumer projection이며 런타임 구현은 `internal/` 아래에 남는다.
Java/.NET 스타일의 깊은 패키지 트리를 강제하지 않는다. Go 사용자가 의존해서는
안 되는 공개 import 경로가 만들어지기 때문이다.

## 공개 계약 소스

- 공개 계약 소스: `bindings/go/contracts/` 아래의 공개 패키지.
- 모듈 projection: `bindings/go/go.mod`, 공개 `zlink.systems/zlink/contracts`
  패키지, export 식별자에 대한 GoDoc.
- 런타임 구현: `bindings/go/internal/` 아래 private 패키지. 기존 루트 구현 파일은
  마이그레이션 입력이며 완성된 형태가 아니다. 루트 패키지의 export 이름이 두 번째
  공개 표면이 되기 때문이다.
- 네이티브 브리지: cgo 브리지 코드, 콜백 trampoline, request 진행 헬퍼,
  `bindings/go/include/`, `bindings/go/native/` 아래의 플랫폼 네이티브 아티팩트.
- 문서 역할: 이 README는 형태와 의미 커버리지를 정의한다. export Go 식별자가 정확한
  공개 멤버 목록을 소유한다. 각 공개 식별자는 여전히 공통 계약 카테고리 중 하나에
  매핑되어야 한다.

Perf, 샘플, 외부 공개 표면 테스트는 공개 `contracts` 패키지를 import해야 하며
구현 전용 헬퍼나 네이티브 브리지 세부에 접근하지 않는다. 구현 테스트는 private
구현 패키지 옆에 둘 수 있지만, consumer 테스트는 공개 `contracts` projection을
검증한다.

## 저장소 레이아웃

Go 바인딩을 수정할 때 다음 경로를 일관되게 사용한다.

- 공개 계약: `bindings/go/contracts/`.
- 런타임 구현: private 구현 패키지를 위한 `bindings/go/internal/`. 기존 루트 구현
  파일은 그곳으로 이동하거나, 바인딩이 정합되었다고 선언되기 전에 구현 전용
  이름의 export를 중단해야 한다.
- 네이티브 브리지/아티팩트: `bindings/go/internal/native/`,
  `bindings/go/native/`, `bindings/go/include/`.
- 코덱 모듈: 제공하지 않는다. Go 바인딩은 raw `Message`와 byte payload API만
  유지한다.
- 테스트: `bindings/go/tests/`와 `bindings/go/*_test.go`.
- 샘플: `bindings/go/samples/`.
- Perf: `bindings/go/perf/`.

Go import 경로는 공개 API의 일부다. 현재 공개 consumer projection은 통합된
`zlink.systems/zlink/contracts` 패키지다. 최상위 `runtime/` 패키지를 만들지 않는다.
그러면 런타임 구현이 `zlink.systems/zlink/runtime`으로 노출되기 때문이다. 모듈
루트에 구현용 export 이름을 병렬 API로 남겨두지 않는다. 모듈 루트를 import
경로로 유지한다면, 그것은 구현 소유자가 아니라 동일한 공개 계약의 의도적
projection이어야 한다.

다음 트리는 정합된 구현 구조다. Export 타입, 함수, 에러, enum, builder 계약은
공개 `contracts` 패키지에 속한다. Go 공개 import 정책이 바뀌지 않는 한
`contracts/core`나 `contracts/sockets` 패키지를 만들지 않는다. 그런 경로가 사용자
대상 API가 되기 때문이다. Private `internal/` 패키지가 cgo 브리지와 런타임
세부를 소유한다. cgo 선언, raw 포인터, 네이티브 struct mirror, 콜백 trampoline,
request 진행 헬퍼, marshalling은 consumer 진입점이 되어서는 안 된다.

파일 단위는 `../README.md`의 공통 정책을 따른다. 독립적인 공개 개념 하나, 또는
긴밀한 operation/모델 그룹 단위로 파일 하나를 유지한다. 아주 작은 콜백, enum,
에러, 통과 헬퍼 파일은 공개 형태를 읽기 쉽게 만든다면 근처의 계약 파일에
병합한다.

```text
bindings/go/
+-- go.mod
+-- doc.go
+-- contracts/
|   +-- core.go
|   +-- messaging.go
|   +-- sockets.go
|   +-- eventing.go
|   +-- service_spot.go
|   +-- errors.go
+-- internal/
|   +-- core/
|   +-- handles/
|   +-- messaging/
|   +-- buffers/
|   +-- sockets/
|   +-- eventing/
|   +-- service/
|   +-- options/
|   +-- errors/
|   +-- native/
+-- include/
+-- native/
+-- tests/
+-- samples/
+-- perf/
```

공개 consumer projection은 `contracts` 패키지다. 샘플, perf, 공개 표면 테스트는
그 공개 패키지만 import해야 한다. Export 심볼이 추가되면 리뷰어는 그것의 계약
카테고리 소유자를 지목할 수 있어야 한다. cgo를 호출하거나 네이티브 수명을
관리하는 용도로만 존재하는 코드는 `internal/` 아래 구현 소유자 파일에서
unexported로 유지한다.

## API 변경 워크플로

새로운 core 기능을 매핑할 때.

1. 공개 동작을 소유하는 공통 계약 카테고리를 선택한다.
2. 공개 `contracts` 패키지에 export 타입, 메서드, 함수를 추가하고 소스 리뷰에서
   카테고리 소유권을 명확히 유지한다.
3. cgo 호출과 네이티브 상태를 공개 시그니처와 consumer 코드 밖에 둔다.
4. 일반적인 Go 스타일로 `error` 또는 typed 에러를 반환한다.
5. 호출자의 실질적 복잡도를 줄이지 않는 한 provider 소유 인터페이스를 정의하지
   않는다.
6. 공개 패키지 테스트를 추가하고 export API를 통해서만 샘플/perf를 갱신한다.
7. 가능하면 `go vet` 스타일 검사를 실행하고 cgo 포인터 소유권을 명시적으로
   유지한다.

기존 코드를 이 형태로 리팩터링할 때.

1. 공개 동작 선언을 `contracts` 패키지로 옮긴다.
2. cgo 기반 런타임 구현을 `internal/<category>/` 패키지로 옮긴다.
3. cgo 선언, 네이티브 로딩, raw 핸들은 private 파일 또는 `internal/native/`에
   유지한다.
4. 사용자 대상 코드에서 구현 소유자를 직접 생성하는 부분을, 공개 생성자 또는 계약
   개념으로 타입화된 메서드로 교체한다.
5. 구현 헬퍼를 공개 API로 노출하는 호환성 export를 제거한다.
6. Deprecated wrapper, 중복 operation start 이름, 옛 명명 alias를 shim으로
   남기지 말고 제거한다.
7. 샘플, perf, 공개 표면 테스트가 공개 `contracts` 패키지만 import하도록
   갱신한다.

아래의 Go 전용 단축 경로가 제거되어야 리팩터링이 완료된다.

- cgo 핸들 소유자, 네이티브 브리지 헬퍼, request 진행 헬퍼, raw part-loop
  헬퍼는 export되지 않는다.
- 샘플, perf, 공개 표면 테스트가 구현 전용 패키지를 import하거나 `contracts`를
  우회하는 루트 패키지 단축 경로를 사용하지 않는다.
- 공개 생성자와 헬퍼 함수는 cgo/네이티브 구현 세부가 아니라 계약 대응 concrete
  타입 또는 좁은 인터페이스를 반환한다.
- 공개 `runtime` 패키지를 도입하지 않는다. Private 구현 패키지는 `internal/`을
  사용한다.

## 라이브러리 형태

Go 바인딩은 일반적인 Go 관례를 따른다.

- 리소스와 값에는 concrete export 타입을 선호한다.
- 모든 리소스 타입마다 큰 provider 소유 인터페이스를 정의하지 않는다.
- 작은 인터페이스는 호출 지점을 단순하게 만들 때에만 consumer 또는 패키지가
  정의할 수 있다.
- 메서드는 Go 호출자가 실패를 예상하는 곳에서 `(value, error)` 또는 `error`를
  반환한다.
- 데이터 없음과 일시적 backpressure는 hard error와 구분되게 표현한다.
- cgo 핸들, raw 포인터, 콜백 userdata, part-loop 순서, request pump는 unexported로
  유지한다.
- 리소스 수명에는 `Close()`를 사용한다. 어떤 타입이 백그라운드 goroutine을
  시작한다면, close 의미를 명시적으로 정하고 테스트한다.

메시지, routing id, received metadata, topic message, result value, snapshot,
option struct 같은 DTO와 값 타입은 concrete로 유지한다.

## 계약/런타임 배치 규칙

- Export 공개 타입, 메서드 계약, enum, 에러, builder 계약은 공개 `contracts`
  패키지에 속한다.
- Export 패키지 함수, 헬퍼 메서드, builder 편의 헬퍼는 호출자가 직접 사용할 수
  있다면 `contracts`에 속한다.
- cgo 핸들 소유자, request pump, 콜백 adapter, part-loop 헬퍼는 `internal/`
  아래에서 unexported로 유지한다.
- cgo 선언, raw 포인터, C struct mirror, marshalling 헬퍼, 플랫폼 로딩 코드는
  `internal/native` 또는 private 네이티브 헬퍼에 둔다.
- Export 계약 패키지는 계약 카테고리를 projection해야 하며 런타임 패키지를
  import 경로로 노출하지 않는다.
- 공개 생성자는 private 런타임 구현을 호출할 수 있지만, 공개 시그니처가
  구현 전용 타입을 노출해서는 안 된다.

## 계약 파일 레이아웃

Go는 import 경로가 공개 API이므로 공개 통합 `contracts` 패키지 하나를 유지한다.
그 패키지 안의 소스 파일을 사용해 공개 서브패키지를 만들지 않고 공통 카테고리
맵을 미러링한다.

- `core.go`: context, option, version/capability 조회 헬퍼, routing id, 패키지 레벨
  유틸리티 계약.
- `messaging.go`: message, received metadata, topic message, subscription
  event, 공통 payload 헬퍼.
- `sockets.go`: socket family, typed option, 콜백, request/reply,
  publish/subscribe, stream packet API, operation builder.
- `eventing.go`: monitor, poller, poll event, timer, handler 계약.
- `service_spot.go`: 서비스 계층 계약과 도메인 모델.
- `errors.go`: export 에러 값, 에러 타입, result 도메인.

작은 콜백 타입, enum 값, result 헬퍼는 의미를 부여하는 근처 계약 파일에 둘 수
있다. 이름이 실제 도메인을 가리는 `types.go`, `models.go`, `common.go`,
`utils.go`는 사용하지 않는다.

## 런타임 파일 레이아웃

런타임 소스는 [.NET 바인딩 청사진](../dotnet/README.ko.md)의 런타임 분류를 미러링하되 private으로 유지한다.

- `internal/core`, `internal/messaging`, `internal/sockets`,
  `internal/eventing`, `internal/service`, `internal/errors`가 네이티브 런타임
  구현 위의 private facade를 소유한다.
- `internal/native`가 cgo 선언, 네이티브 로딩, raw 핸들, marshalling, 콜백
  trampoline, request 진행 헬퍼, option marshalling, buffer 변환 헬퍼를 소유한다.

Private 런타임 코드는 공개 계약 타입에 의존할 수 있다. 공개 계약 코드는 private
구현 세부에 의존해서는 안 된다.

## 생성 진입점

Go 생성은 공개 생성자와 리소스 메서드로 노출된다.

- `NewContext(...)`는 런타임 context 구현을 생성한다.
- `Context.PairSocket()`, `DealerSocket()`, `RouterSocket()`, `PubSocket()`,
  `SubSocket()`, `XPubSocket()`, `XSubSocket()`, `StreamSocket()`이 런타임 소켓
  구현을 생성한다.
  `SpotNode()`, `SpotNodeWithOptions(...)`가 서비스 계층 구현을 생성한다.
- `Spot` 핸들은 `SpotNode.Spot()`, `EntrySpot()`, `GetOrCreateSpot(...)`,
  `SpotLookup(...)`을 통해 얻는다. `Spot` 직접 생성은 공개되지 않는다.
- Actor 핸들은 `SpotNode.Actor(...)`로 생성한다. Actor 직접 생성은 공개되지
  않는다.
- `NewPoller()`, `NewTimer()`, `NewTimerFromSpot(...)`이 eventing 리소스를
  생성한다.
- `NewAtomicCounter()`, `NewStopwatch()`, `NewThread(...)`는 호출자가 소유하는
  유틸리티 리소스를 생성한다.
- Version, capability 조회, strerror, proxy, sleep, multipart cleanup 헬퍼는 공개
  계약 함수다. 이 함수들 뒤의 cgo 호출은 private으로 유지한다.

## 계약 카테고리 맵

이 카테고리는 통합된 `bindings/go/contracts/` 패키지의 export 식별자에 매핑된다.
이들은 리뷰 소유권 라벨이며 Go 서브패키지 이름이 아니다.

- `Core`: context, context option, routing id, version/capability 조회 헬퍼, 런타임
  유틸리티 계약.
- `Messaging`: message, received metadata, topic message, subscription event,
  stream packet 콜백, builder payload 헬퍼.
- `Sockets`: socket 동작, socket family, typed option, request/reply,
  publish/subscribe 표면.
- `Eventing`: monitor, monitor snapshot/event, poller, poll event, timer, 공개
  poll 헬퍼.
- `Service`: SPOT node, SPOT 핸들, topology 모델, Actor
  ref, Actor 수명, operation builder.
- `Errors`: export 에러 값 또는 typed 에러 도메인.
- Enum, flag, result 식별자는 의미를 정의하는 카테고리에 둔다. 단순히 문법으로
  선언을 묶기 위해 별도의 `enums` 패키지를 만들지 않는다.

## 표준 인터페이스 규칙

- 데이터 평면의 `Recv`, routed recv, `Subscribe`, subscription-event receive는
  호출자가 제공한 `*Received`, `*TopicMessage`, `*SubscriptionEvent` 값을 채우고
  `(bool, error)`를 반환한다.
- `RecvPart`, `SubscribePart`, `Spot.RecvRoutedPart` 같은 part 단위 receive API는
  공개 계약 멤버가 아니다. 런타임은 내부적으로 `*_part` C substrate를 사용할 수
  있지만, 호출자는 통합된 `Received`/`TopicMessage` 값을 받는다.
- Spot relay 헬퍼를 노출한다면, routed Spot 메시지 하나를 소비하고 payload를
  호출자에게 노출하지 않은 채 소스 Spot 경로로 다시 forward한다. payload를
  검사하거나 수정하지 않는 relay 경로 전용이다. payload 접근이 필요한 호출자는
  `RecvRouted(...)`와 `SendToSpot(...)`을 사용한다.
- Send, routed send, publish, request, reply, SPOT operation, Actor
  location/session operation은 fluent builder를 반환한다.
- Builder start 메서드는 대상 identity, topic, channel, routing id, request
  sequence만 받는다. payload, flag, timeout, 콜백, async submit 선택은 builder
  단계다.
- SPOT channel 대상 operation은 `SendToChannel(...)`과
  `RequestToChannel(...)`을 사용한다. SPOT topic publish는 `Publish(topic)`을
  유지한다.
- operation start 메서드와 같은 이름의 단일 payload 단축 메서드를 추가하지
  않는다. `Send(message)`, `Send(routingID, message)`,
  `Publish(topic, message)`, `SendToChannel(channel, message)`,
  `SendToSpot(..., message)`는 공개 계약 멤버가 아니다. 호출자는
  `Send(...).Message(message).Submit(...)`을 사용한다.
- 멀티파트 payload는 `Message(...)`, `MoveMessage(...)`, `Bytes(...)`의 반복
  호출로 누적된다. `Bytes(...)`는 `Submit(...)` 동안 호출자 소유 슬라이스를 읽고,
  `Submit(...)`이 반환된 뒤에는 보유하지 않는다. 동일한 builder 계약에 위임하고
  공개 패키지 카테고리에 선언된다면 `Messages(...)` 편의 메서드는 허용된다.
- Dealer 소켓은 `RequestFrame(...)`이나 `Reply(requestToken, parts)` 같은
  프로토콜 envelope 헬퍼를 노출해서는 안 된다. dealer는 `Request()`로 request를
  시작할 수 있지만, API 레벨의 peer routing id가 없으므로 임의의 토큰에 대해
  reply할 수 없다.
- Send builder는 submit 시점에 소유권을 이전할 수 있는 hot path를 위해
  `MoveMessage(...)`도 노출한다. `Message(...)`는 기존 계약을 유지한다: 호출자의
  메시지는 submit 실패 시 보존되고 성공 시 소비된다. `MoveMessage(...)`는
  명시적인 opt-in이다. `Submit(...)`이 반환된 뒤에는 submit이 에러를 보고하더라도
  호출자는 그 메시지를 재사용해서는 안 된다.
- Message payload factory는 from-source 의미를 보존하면서 Go 생성자 명명을
  사용한다. `NewMessage(...)`는 주요 생성자이고 `NewMessageString(...)`은
  UTF-8 문자열 입력을 처리한다. `NewMessageFrom(...)`과
  `NewMessageFromBytes`는 공개 계약의 일부가 아니다.
- `SendNoWait`, `PublishWithFlags`, `RequestAsync` 같은 operation start 메서드
  family를 추가하지 않는다. operation 이름 하나를 유지하고 builder가 변형을
  흡수하게 한다. 종료 builder 메서드는 관용적 이름을 사용할 수 있다.
  `context.Context`는 builder start가 아니라 submit 시점에 전달한다.

## 패키지 형태

공개 `contracts` 패키지 트리를 훑어보기 쉽게 유지한다.

- Core 식별자는 context, version/capability 조회 헬퍼, option, 런타임 유틸리티를
  포함한다.
- Messaging 식별자는 message, routing id, received metadata, topic message,
  subscription event 타입을 포함한다.
- Socket 식별자는 pair, dealer, router, pub, sub, xpub, xsub, stream, option,
  콜백, request/reply, publish/subscribe, stream packet API를 포함한다.
- Eventing 식별자는 monitor, monitor snapshot/event, poller, poll event, timer를
  포함한다.
- Service 식별자는 SPOT node, SPOT 핸들, topology
  snapshot, Actor ref, Actor 수명, operation builder를 포함한다.
- Error 식별자는 core result 도메인을 보존한다.
- Enum 식별자는 패키지 간에 공유되는 공개 enum 도메인을 포함한다.

cgo 호출, 네이티브 메모리 관리, request 진행을 위해서만 존재하는 헬퍼는 export하지
않는다.

## 필수 기능 커버리지

Go 패키지는 공통 .NET 표준 정책에 정합될 때 다음 안정된 사용자 대상 기능을
커버해야 한다.

- Context 수명, context option, shutdown, auto-HWM 재계산, version, capability 조회,
  strerror.
- Message ownership, 멀티파트 payload, routing id, received metadata, topic
  message, subscription event, stream packet 콜백.
- 모든 socket family와 typed option.
- Monitor, poller, timer, readiness 의미.
- SPOT node, SPOT 핸들, topology snapshot, Actor, stream
  Actor 바인딩.

공개 Go 형태는 API를 관용적으로 묶거나 이름을 바꿀 수 있지만, core operation의
의미를 바꾸지 않는다.

## Spot Get-Or-Create

Go는 `SpotNode.GetOrCreateSpot(spotRID RoutingID) (*Spot, bool, error)`를
노출한다. 이는 `zlink_spot_node_spot_get_or_new(...)`에 직접 매핑되며, lookup
경로와 별도의 create 경로를 조합해서 구현해서는 안 된다.

반환된 `*Spot`은 호출자 소유이며 정상적으로 close되어야 한다. boolean은 논리적
spot을 생성한 호출에 대해서만 `true`다.

## Receive 및 Subscribe 형태

- 데이터 평면 receive와 subscribe API는 호출자 소유의 재사용 가능한 결과
  저장소를 사용해야 한다.
- 논블로킹 no-data는 hard failure와 혼동되어서는 안 된다.
- SPOT dispatch readable 이벤트는 readiness 알림이다. 호출자는 해당 receive
  API를 no-data가 될 때까지 drain한다.
- Monitor와 timer의 컨트롤 평면 API는 hot path 할당을 추가하지 않고 더
  관용적일 때 값 반환 형식을 사용할 수 있다.
- Actor join request receive 같은 서비스 컨트롤/admission receive 경로는
  `(value, bool, error)` 형식의 값 반환이나 동등한 typed 결과를 사용할 수 있다.
  그래도 no-data와 hard receive failure는 구분해야 한다.

## 에러 및 검증 정책

- routing id, Actor id, endpoint, channel 이름, topic은 cgo 경계를 넘기 전에
  검증한다.
- 네이티브 고정 크기 값을 조용히 잘라내지 않는다.
- submit, request, recv, handler, close, bind, connect, config 에러 도메인을
  보존한다.
- 공개 에러는 네이티브 errno 지식 없이 검사 가능해야 한다.

## 성능 정책

- Hot path는 reflection, 문자열 기반 동적 dispatch, 피할 수 있는 할당, 피할 수
  있는 byte 슬라이스 복사, 숨겨진 sleep, busy wait, 광범위한 lock, goroutine
  join을 사용하지 않는다.
- cgo 브리지 코드는 core part substrate에서 공개 Go 값을 직접 materialize 해야
  한다.
- per-handle 진행이 공유될 수 있을 때 request마다 goroutine이나 timer를 하나씩
  시작하지 않는다.
- Perf 측정 의미는 `bindings/c/perf`와 일치해야 한다.

## 구현 체크리스트

- 공개 API는 `contracts` 패키지에서 export된다.
- cgo 세부가 공개 시그니처로 새지 않는다.
- 리소스 수명은 `Close`로 명시적이다.
- Provider 소유 인터페이스는 신중히 사용된다.
- Export 헬퍼 함수와 builder 편의 메서드는 런타임 헬퍼에만이 아니라 일치하는
  공개 계약 패키지에 선언된다.
- Receive/subscription 의미는 공통 바인딩 정책과 일치한다.
- 서비스 컨트롤/admission receive 예외는 데이터 평면의 호출자 제공 저장소와
  다른 부분이 있다면 문서화된다.
- Perf, 샘플, 공개 표면 테스트는 export 패키지 API만 사용한다.
- 구현 전용 파일이나 `internal/` 패키지가 공개 시그니처로 새지 않는다.
- 호환성 목적으로만 유지되는 옛 alias, 중복 operation start 이름, deprecated
  wrapper가 없다.

Go 리팩터링 후 필수 검증. 다음 명령은 `bindings/go/`에서 실행한다.

- `go test ./...` 실행.
- `./tests/run_tests.sh` 실행.
- 공개 예제나 생성 경로가 변경되면 `./samples/run_samples.sh` 실행.
- hot path, receive, send, request, poller, timer, 서비스 동작이 변경되면 smoke
  gate로 `./perf/run_benchmarks.sh`와 `./perf/run_benchmarks_multi.sh` 실행.
- 변경된 패키지에 대해 가능한 경우 `go vet ./...` 실행.
- 샘플, perf, 공개 표면 테스트, `contracts`에서 구현 전용 패키지, cgo 브리지
  헬퍼, raw 핸들, 네이티브 브리지 심볼에 대한 import나 참조를 검색한다. 구현
  테스트는 private 패키지에 한정되어야 하며 consumer import의 예시가 되어서는
  안 된다.

## Actor 및 Spot 경로 결과

Go는 Actor와 Spot 경로 lookup 결과를 export 값 타입으로 노출한다.

- `ActorRoute`는 resolve된 Actor ref, Actor node RID, 현재 Spot RID, 현재 Spot
  kind를 보존한다.
- `SpotRoute`는 Spot RID, 소유자 node RID, Spot kind를 보존한다.
- `SpotKind`는 Entry Spot과 사용자 Spot을 구분한다. 잘못된 kind는 성공적인 경로
  결과가 아니다.
- SpotNode snapshot 엔트리는 core snapshot과 동일한 Spot kind/현재 Spot 필드를
  노출한다.

Go는 resolve된 Actor ref를 인자로 받는 `SpotNode.SendToActor(ActorRef)`와
`SpotNode.RequestToActor(ActorRef)`를 노출한다. send operation은 submit이
성공하면 하나 이상의 message part 소유권을 넘기고, Actor 소유자 mailbox가 인계를 받으면
완료된다. request operation은 submit이 성공하면 요청 part의 소유권을 넘기고,
Actor handler가 만든 reply part를 전달한다. Go는 제거된 Discovery route table이나
resolver API를 compatibility helper로 되살리면 안 된다.
