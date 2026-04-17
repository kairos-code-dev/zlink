[스펙 목차](../README.ko.md)

# Draft -- RoutingId Draft Alignment

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더, 바인딩 테스트가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 문서는 아래 세 draft 문서의 관계를 정리한다.

- [routing-id-bytes-and-conversions.ko.md](routing-id-bytes-and-conversions.ko.md)
- [binding-routing-id-marshaling.ko.md](binding-routing-id-marshaling.ko.md)
- [routing-id-borrowed-routingid.ko.md](routing-id-borrowed-routingid.ko.md)

기존 두 문서는 `routing_id`를 bytes canonical로 정리하고 helper 규칙을 맞추는 데
초점을 두었다. 새 문서는 한 걸음 더 나아가, **binding receive 경로에서 할당 없이
`RoutingId`를 가져오기 위해 public 수명 규칙과 borrowed 계약까지 다시 정의한다.**

또한 구현 계획 관점에서는 "binding 변환 기능을 별도 helper 계층으로 둘 것인가"
문제도 새 초안 기준으로 다시 정리해야 한다.

따라서 이 문서의 역할은 "기존 초안의 어떤 내용이 계속 유효하고, 어떤 내용은 새
초안으로 대체되는지"를 분명히 적는 것이다.

## 2. 문서별 역할

### 2.1 `routing-id-bytes-and-conversions.ko.md`

이 문서의 핵심 역할은 아래 두 가지다.

- `RoutingId`의 canonical 의미를 bytes로 두는 방향 설명
- `u32`, `text`, `hex` helper 규칙 정리

이 문서는 helper 의미와 검증 규칙을 설명하는 초안으로는 여전히 유효하다.
다만 C ABI와 storage 모델에 대해서는 새 borrowed `RoutingId` 초안을 우선 기준으로
봐야 한다.

### 2.2 `binding-routing-id-marshaling.ko.md`

이 문서의 핵심 역할은 아래와 같다.

- binding surface도 bytes canonical을 유지해야 한다는 점
- `STREAM`도 별도 정수 canonical 타입으로 분리하지 않는다는 점
- `from_u32`, `to_u32`, `from_text`, `to_text`, `to_hex` helper가 binding마다
  같은 의미를 가져야 한다는 점

이 문서도 helper naming과 binding helper 의미를 정리하는 참고 초안으로는
유효하다. 다만 receive 경로에서 owned `RoutingId` wrapper를 기본처럼 다루는
전제와, 별도 public helper 계층을 두는 해석은 더 이상 기준이 아니다.

### 2.3 `routing-id-borrowed-routingid.ko.md`

이 문서는 앞의 두 문서가 다루지 않던 영역을 추가로 고정한다.

- core C ABI를 borrowed `RoutingId`로 바꾸는 방향
- receive, callback, monitor, event 경로의 수명 규칙
- binding public receive 경로도 borrowed `RoutingId`를 노출해야 no-allocation이
  가능하다는 점
- Java, .NET, Rust, Python, Node에서 public 시그니처가 어떤 형태여야 하는지

즉 이 문서는 helper 규칙만 설명하는 문서가 아니라, **소유권과 수명까지 포함한
상위 draft**다.

## 3. 유지되는 내용

기존 두 문서에서 아래 내용은 그대로 유지된다.

### 3.1 bytes canonical 의미

- `RoutingId`의 본질은 길이가 있는 바이트 열이다.
- `STREAM`도 public canonical을 별도 정수 타입으로 분리하지 않는다.
- routed/service 경로도 문자열 canonical 타입으로 고정하지 않는다.

### 3.2 helper 의미

- `from_u32`: 4-byte big-endian bytes 생성
- `to_u32`: 길이가 정확히 4일 때만 성공
- `from_text`: UTF-8 인코딩 bytes 생성
- `to_text`: 유효한 UTF-8일 때만 성공
- `to_hex`: 항상 성공

### 3.3 helper 실패 규칙

- invalid length: 실패
- invalid UTF-8: 실패
- invalid input pointer 또는 invalid output buffer: 실패
- 조용한 truncate, zero-fill, lossy decode 금지

### 3.4 sample / guide 방향

- `STREAM` 예제는 `to_u32()`를 사용
- routed/service 예제는 `to_text()`를 우선 쓰고, 실패 시 `to_hex()` 사용
- raw bytes 경로는 여전히 escape hatch로 유지

## 4. 대체되는 내용

기존 두 문서에서 아래 내용은 새 borrowed `RoutingId` 초안으로 대체된다.

### 4.1 C `zlink_routing_id_t` storage 가정

기존 초안은 사실상 아래 가정을 깔고 있다.

- `zlink_routing_id_t`는 inline value carrier를 유지한다.
- `from_*` helper는 그 value object를 바로 채운다.

새 초안에서는 이 가정을 쓰지 않는다.

- `zlink_routing_id_t`는 borrowed bytes carrier로 바뀐다.
- `from_*` helper는 caller-provided backing storage를 받아야 한다.

### 4.2 binding receive 기본 모델

기존 초안은 helper와 bytes canonical을 강조하지만, receive 경로에서 binding이
owned `RoutingId`를 기본으로 노출하는 현재 모델과도 크게 충돌하지 않는다.

새 초안은 여기서 방향을 바꾼다.

- no-allocation receive를 목표로 할 때
- binding public receive surface는 owned copy가 아니라 borrowed `RoutingId`를
  기본으로 둔다.

즉 새 초안에서는 "binding이 routing id를 받을 때마다 새 값을 만든다"는 전제를
버린다.

### 4.3 binding 시그니처 해석

기존 marshaling 초안의 helper shape는 유지 가능하지만, 아래처럼 다시 읽어야 한다.

- `RoutingId.toBytes()`류는 항상 cheap accessor가 아닐 수 있다.
- bytes를 독립 보관하려면 명시적으로 copy가 일어날 수 있다.
- receive surface의 `RoutingId`는 owner 수명에 종속될 수 있다.

즉 helper 이름은 유지될 수 있어도, lifetime 의미는 새 초안 기준으로 다시 적어야
한다.

### 4.4 binding 변환 API 배치

기존 두 문서는 helper 규칙 자체에는 문제가 없지만, 구현 단계에서는 별도 public
helper 계층으로 읽힐 여지가 있다.

새 초안 기준에서는 아래처럼 정리한다.

- core C만 free function helper를 public으로 둔다.
- binding은 public 변환 기능을 `RoutingId` 타입에 흡수한다.
- 별도 `RoutingIdHelper`, `routing_id_utils`, codec 모듈은 public 기준에서
  정리 대상이다.

## 5. 구현 시 적용 우선순위

실제 구현 단계에서는 아래 우선순위로 해석한다.

1. `routing-id-borrowed-routingid.ko.md`
   - ABI, 수명, receive/public surface 결정 기준
2. `routing-id-bytes-and-conversions.ko.md`
   - helper 의미, round-trip, 실패 규칙 기준
3. `binding-routing-id-marshaling.ko.md`
   - binding `RoutingId` 메서드 naming, sample/perf 적용 방향 기준

즉 새 borrowed `RoutingId` 초안이 상위 기준이고, 나머지 두 문서는 그 아래에서
helper와 적용 예시를 보강하는 역할로 읽는다.

## 6. 정식 spec 반영 시 정리 원칙

정식 spec으로 올릴 때는 아래처럼 정리한다.

- C ABI와 수명 규칙은 `routing-id-borrowed-routingid.ko.md` 기준으로 흡수
- helper 의미와 실패 규칙은 `routing-id-bytes-and-conversions.ko.md` 기준으로 흡수
- binding별 helper 이름, sample/perf 적용 방향은
  `binding-routing-id-marshaling.ko.md` 기준으로 흡수
- 중복되거나 충돌하는 설명은 남기지 않는다

특히 아래 표현은 정식 spec 반영 전에 제거하거나 다시 써야 한다.

- receive 경로에서 `RoutingId`가 항상 독립 owned 값처럼 보이는 설명
- `toBytes()`가 항상 단순 accessor처럼 보이는 설명
- helper만 추가하면 no-allocation receive가 자동으로 달성되는 것처럼 읽히는 설명
- binding에서 별도 public helper 계층을 유지해도 된다고 읽히는 설명

## 7. 정리

세 문서의 관계는 아래 한 문장으로 요약할 수 있다.

- 기존 두 초안은 `RoutingId`를 bytes canonical로 맞추는 문서이고,
  새 초안은 그 위에 **borrowed 수명 규칙과 no-allocation receive 모델**을
  얹는 상위 문서다.

따라서 구현자는 helper 규칙을 버리면 안 되지만, ABI와 binding receive 모델은
반드시 새 borrowed `RoutingId` 초안을 우선 기준으로 읽어야 한다.
