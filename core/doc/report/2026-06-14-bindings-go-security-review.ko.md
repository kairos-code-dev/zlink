# Go 바인딩 보안 검토 보고서

- 작성일: 2026-06-14
- 대상 범위: `bindings/go/internal/native/ffi.go`, `bindings/go/internal/native/message.go`
- 검토 방식: cgo 링크 설정, routing id 변환, 메시지 생성·복사·close 경로를 코드 기준으로 확인했다.
- 상태: 주의 항목 1건

## 요약

Go 바인딩은 cgo로 core C API를 호출한다. 이번 검토에서는 Go slice와 C 메시지 버퍼 사이의 복사, routing id 크기 제한, close 상태 관리를 확인했다.

현재 확인한 범위에서는 Go 바인딩 자체의 명백한 기능 회귀 위험은 확인되지 않았다. 다만 `Message.Data()`는 native buffer를 복사하지 않고 slice로 노출하므로, 호출자가 메시지를 닫은 뒤 기존 slice를 계속 쓰지 않아야 한다.

## 확인된 이슈

### GO-BINDING-001: `Message.Data()`가 반환한 slice는 메시지 수명에 의존한다

- 심각도: 낮음
- 근거:
  - `bindings/go/internal/native/message.go:234-244`는 `zlink_msg_data` 결과를 `unsafe.Slice`로 감싸 반환한다.
  - `bindings/go/internal/native/message.go:246-252`의 `Bytes()`는 `Data()` 결과를 새 `[]byte`로 복사해 snapshot을 만든다.
- 영향:
  - `Data()` 호출 뒤 메시지를 닫고 기존 slice를 계속 사용하면 이미 닫힌 native buffer를 참조할 수 있다.
  - 정상적으로 메시지 객체가 살아 있는 동안만 slice를 쓰면 기능 문제는 없다.
  - 복사를 피하므로 성능에는 유리하지만, 수명 규칙을 호출자가 지켜야 한다.
- 권장 조치:
  - `Data()`가 반환한 slice는 메시지를 닫기 전까지만 유효하다고 공개 문서에 명시한다.
  - 안전한 보관이 필요한 호출자는 `Bytes()`를 쓰도록 안내한다.

## 확인 결과

- `bindings/go/internal/native/ffi.go:5-10`은 패키지에 포함된 native library 경로와 rpath를 사용한다. 별도 환경 변수로 임의 library를 직접 로드하는 경로는 확인되지 않았다.
- `bindings/go/internal/native/message.go:87-93`은 `RoutingID.Bytes()`에서 내부 배열을 복사해 반환한다.
- `bindings/go/internal/native/message.go:142-148`은 Go routing id를 C 구조체로 복사할 때 저장된 size만큼만 복사한다.
- `bindings/go/internal/native/message.go:151-158`은 C routing id를 Go 값으로 복사한다. C 구조체의 size 필드가 `uint8_t`라서 현재 구조체 정의에서는 최대 255바이트 범위를 넘지 않는다.
- `bindings/go/internal/native/message.go:173-181`은 새 메시지를 만들 때 C 메시지 저장소를 만든 뒤 Go 입력 데이터를 복사한다.
- `bindings/go/internal/native/message.go:223-231`은 `Close()` 이후 closed 상태를 기록한다.

## 기능 영향 검토

Go API는 메시지를 만들 때 입력 slice를 native 메시지로 복사한다. 호출자가 원본 slice를 이후 변경해도 이미 생성된 메시지 payload가 바뀌지 않는 구조다.

Routing id도 public API에서는 복사본을 반환하므로 외부 호출자가 내부 저장소를 직접 바꾸는 문제는 확인되지 않았다.

## 성능 영향 검토

메시지 생성 시 Go slice에서 native buffer로 한 번 복사한다. 이는 cgo 경계에서 수명 문제를 줄이는 안정적인 선택이다. 반대로 `Data()`는 복사 없는 view를 반환하므로 성능에는 유리하지만 메시지 수명에 묶인다.

## 결론

Go 바인딩에서 즉시 수정해야 할 기능 결함은 확인하지 못했다. 다만 `Data()`의 slice 수명 규칙은 문서와 테스트로 고정하는 편이 좋다. 남은 위험은 core C API와 cgo callback 경계의 계약에 종속된다.
