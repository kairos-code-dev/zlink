[English](README.md) | [한국어](README.ko.md)

[스펙 목차](https://zlink.systems/core/ko/spec/) · [바인딩 정책](../README.ko.md)

# Python 바인딩 구현 청사진

이 문서는 Python 라이브러리가 갖춰야 할 형태를 정의한다. 모든 클래스나 메서드를
빠짐없이 나열한 목록은 아니다. 구체적인 공개 계약 소스는
`bindings/python/src/zlink/contracts/`에 있다. `bindings/python/src/zlink/__init__.py`
에서 내보내는 `zlink` 패키지가 사용자가 import하는 공개 프로젝션이다.

Python 구현은 `zlink.contracts`, 비공개 런타임 패키지, 타입 힌트, 테스트, 샘플,
perf 러너, 런타임 동작이 이 청사진을 따르고 안정적인
`core/include/zlink.h` 기능을 Python 관용 API로 매핑할 때 정렬된 것으로 본다.

이 README는 `../README.md`에 정의된 공통 정책에 정렬된 이후의 Python 바인딩
형태를 기술하며, Python 리팩토링 작업의 지침이기도 하다. 리팩토링 동안 이 문서를
사용해 각 공개 계약, 런타임 구현, 네이티브 브리지 헬퍼, 테스트, 샘플, perf import가
어디에 속하는지 판단한다. Python 바인딩이 정렬되었다고 선언된 이후에는 패키지
export, 타입 힌트, 생성된 API 레퍼런스, 테스트, 샘플, perf, 런타임 동작이 모두
이 문서와 일치해야 한다.

Python 리팩토링은 호환성을 단절하는 정리 작업이다. 호환성 shim, deprecated 래퍼,
중복된 생성 경로, 리팩토링 이전의 공개 표면만을 유지하기 위한 비공개 모듈
재-export 별칭을 남기지 않는다.

이 바인딩은 공통 바인딩 아키텍처 맵을 Python 네이밍으로 따른다. 공개 이름은
`zlink`에서 프로젝션되고, 공개 계약 소스는 소문자 `contracts` 아래에 있으며,
구현 세부는 `_runtime`, `_native`처럼 언더스코어 접두 패키지에 둔다.

Python은 물리적 패키지 트리를 .NET 카테고리 맵에 가깝게 유지한다.
`contracts/core`, `contracts/messaging`, `contracts/sockets`,
`contracts/eventing`, `contracts/service`, `contracts/errors`가 공개 계약
소유자다. `_runtime/`은 같은 카테고리를 미러링하고, `handles`, `buffers`,
`options`, `_native` 같은 구현 전용 경계를 둔다. 네이티브 기반 리소스와 operation
계약은 `typing.Protocol` 기반의 구조적 인터페이스로 선언한다. 구체 런타임 클래스는
`_runtime` 아래에 있고, 호출자는 명시적인 `create_*` 팩토리나 공개 계약 메서드로
생성한다.

## 공개 계약 소스

- 공개 계약 소스: `bindings/python/src/zlink/contracts/`.
- 패키지 프로젝션: `zlink`에서 내보내는 이름들.
- 공개 리소스 계약: 네이티브 기반 리소스, 빌더, operation 핸들은
  `typing.Protocol` 선언이다. 공개 표면을 설명하지만 생성자는 아니다.
- 공개 생성: `create_context()`, `create_pair_socket(...)`,
  `create_message(...)`, `create_poller()`, `create_spot_node(...)` 같은
  패키지 루트 팩토리와 `SpotNode.create_spot()` 같은 공개 소유자 메서드가 담당한다.
- 내부 구현: `_runtime`, `_native`처럼 언더스코어 접두 패키지, 비공개 확장 모듈,
  콜백 브리지 코드, request 진행 헬퍼, raw part-loop 헬퍼.
- 문서 역할: 이 README는 형태와 의미 범위를 정의한다.
  `zlink/contracts/`, `zlink.__init__`, 타입 힌트가 정확한 공개 멤버 목록을 소유한다.

Perf, 샘플, 테스트는 언더스코어 모듈이 아니라 `zlink`에서 import한다.

## 저장소 레이아웃

Python 바인딩을 변경할 때 다음 경로를 일관되게 사용한다.

- 공개 계약: `bindings/python/src/zlink/contracts/`.
- 런타임 구현: `bindings/python/src/zlink/_runtime/`.
- 네이티브 브리지/아티팩트: 비공개 브리지 코드는
  `bindings/python/src/zlink/_native/`, 패키징된 네이티브 바이너리는
  `bindings/python/src/zlink/native/`.
- 코덱 패키지: 제공하지 않는다. Python 바인딩은 raw `Message`와 byte payload API만
  유지한다.
- 테스트: `bindings/python/tests/`.
- 샘플: `bindings/python/samples/`와 `bindings/python/examples/`.
- Perf: `bindings/python/perf/`.

언더스코어 접두 모듈은 구현 세부다. 사용자가 어떤 이름을 필요로 한다면 의도적으로
`zlink`에서 재-export하고 공개 동작을 문서화한다. `__init__.py`, 타입 힌트, 생성된
API 레퍼런스가 계약의 Python 패키지 프로젝션이다. `zlink.Contracts`나
`zlink.Runtime`을 공개 import 경로로 노출하지 않는다. 대문자
`src/zlink/Contracts`나 `src/zlink/Runtime`을 만들지 않는다. Python 패키지 이름은
소문자를 유지한다. 다음 트리가 정렬된 구현 구조다. 공개 클래스, 함수, 예외, enum,
타입 alias, 빌더 계약은 `contracts/`에 속하며 `zlink`에서 의도적으로 재-export된다.
네이티브 확장 호출, 핸들 소유자, 콜백 트램폴린, marshalling, request 진행 헬퍼는
`_runtime`이나 `_native` 아래에 둔다. 데이터 전송 핵심 경로는 Python 코드가
`ctypes`나 CFFI로 part마다 코어 함수를 반복 호출하는 구조가 아니라, 비공개
컴파일된 확장 모듈이 코어 C API를 묶어서 호출하는 구조를 목표로 한다.

파일 단위는 `../README.md`의 공통 정책을 따른다. 독립적인 공개 개념 하나 또는
긴밀한 operation/model 그룹마다 파일 하나를 유지한다. 매우 작은 프로토콜, 콜백,
enum, 통과형 헬퍼 모듈은 공개 형태를 더 읽기 쉽게 만들 수 있을 때 인접한 계약
파일에 병합한다.

```text
bindings/python/
+-- src/
|   +-- zlink/
|   |   +-- __init__.py
|   |   +-- contracts/
|   |   |   +-- core/
|   |   |   |   +-- context.py
|   |   |   |   +-- zlink.py
|   |   |   |   +-- routing_id.py
|   |   |   +-- messaging/
|   |   |   |   +-- message.py
|   |   |   |   +-- received.py
|   |   |   |   +-- topic_message.py
|   |   |   |   +-- subscription_event.py
|   |   |   +-- sockets/
|   |   |   |   +-- socket.py
|   |   |   |   +-- message_socket_contracts.py
|   |   |   |   +-- routed_socket_contracts.py
|   |   |   |   +-- pubsub_socket_contracts.py
|   |   |   |   +-- stream_socket.py
|   |   |   |   +-- socket_options.py
|   |   |   +-- eventing/
|   |   |   |   +-- monitor.py
|   |   |   |   +-- poller.py
|   |   |   |   +-- timer.py
|   |   |   +-- service/
|   |   |   |   +-- spot/
|   |   |   |   |   +-- spot_node.py
|   |   |   |   |   +-- spot.py
|   |   |   |   |   +-- actor.py
|   |   |   |   |   +-- spot_operations.py
|   |   |   |   |   +-- spot_models.py
|   |   |   +-- errors/
|   |   |   |   +-- errors.py
|   |   |   |   +-- results.py
|   |   +-- _runtime/
|   |   |   +-- core/
|   |   |   |   +-- context.py
|   |   |   +-- handles/
|   |   |   |   +-- native_support.py
|   |   |   +-- messaging/
|   |   |   |   +-- message_materializer.py
|   |   |   +-- buffers/
|   |   |   |   +-- payload_buffers.py
|   |   |   +-- sockets/
|   |   |   |   +-- socket_base.py
|   |   |   |   +-- socket_base_impl.py
|   |   |   +-- eventing/
|   |   |   |   +-- poller.py
|   |   |   |   +-- timer.py
|   |   |   +-- service/
|   |   |   |   +-- spot/
|   |   |   |   |   +-- spot_node.py
|   |   |   |   |   +-- spot.py
|   |   |   |   |   +-- actor.py
|   |   |   +-- errors/
|   |   |   |   +-- native_errors.py
|   |   |   +-- options/
|   |   |   |   +-- option_mapping.py
|   |   +-- _native/
|   |   |   +-- _zlink_native.*
|   |   +-- native/
+-- tests/
+-- samples/
+-- examples/
+-- perf/
```

공개 import 표면은 `zlink` 패키지 프로젝션이다. 그 프로젝션의 소스는
`zlink/contracts/`이다. 테스트, 샘플, examples, perf는 `zlink`에서 import한다.
바인딩이 소유하는 JSON, Protobuf, MessagePack codec package는 Python 공개 표면에
포함하지 않는다. 비공개 언더스코어 모듈이 사용자 코드에 필요해진다면, 공개 계약을
추가하고 의도적으로 export한다. 비공개 모듈 자체를 문서화하지 않는다.

## API 변경 워크플로

새로운 코어 기능을 매핑할 때.

1. 공개 Protocol, 구체 값 클래스, 함수, enum, 예외, 타입 alias를 올바른 공개
   패키지 카테고리에 추가한다.
2. 호출자가 생성할 수 있는 네이티브 기반 리소스에는 명시적인 `create_*` 팩토리를
   추가하거나 연결한다.
3. `zlink` 패키지 export, 타입 힌트, API 레퍼런스 프로젝션을 갱신한다.
4. 네이티브 확장/FFI 호출과 request 진행 헬퍼는 비공개 모듈에 둔다.
5. 비공개 모듈이 아니라 `zlink`를 import하는 테스트를 추가한다.
6. 샘플과 perf는 공개 export만을 통해 갱신한다.
7. 비공개 확장 객체가 반환 값이나 예외로 새지 않는지 확인한다.

기존 코드를 이 형태로 리팩토링할 때.

1. 공개 동작 선언을 `zlink/contracts/<category>/`로 옮긴다.
2. 네이티브 기반 구현을 `zlink/_runtime/<category>/`로 옮긴다.
3. 네이티브 확장 로딩과 FFI 호출은 `zlink/_native/` 아래에 유지한다.
4. 공개 코드에서 비공개 런타임을 직접 생성하던 부분을 패키지 루트 팩토리나
   계약 메서드로 대체한다.
5. 비공개 모듈을 공개 API로 노출하던 호환성 export를 제거한다.
6. deprecated 래퍼, 중복된 operation-start 이름, 옛 네이밍 별칭은 shim으로
   남기지 말고 제거한다.
7. 테스트, 샘플, examples, perf가 `zlink`에서만 import하도록 갱신한다.
8. 타입 힌트와 API 레퍼런스를 재생성하거나 점검해, 비공개 구현 모듈이 공개
   표면에 나타나지 않도록 한다.

아래 Python 고유 단축이 모두 제거되었을 때에만 리팩토링이 완료된다.

- `zlink.contracts`는 `_runtime`이나 `_native` 모듈을 재-export하지 않는다.
- 계약 파일은 공개 서비스 모델을 기술하기 위해 런타임 리소스 타입을 import하지
  않는다.
- 비공개 모듈 집계가 공개되어 리소스 동작의 원천으로 남지 않는다. 선언을 명명된
  계약 파일과 런타임 구현 파일로 분리한다.
- `zlink.__init__`은 비공개 구현 모듈이 아니라 계약 이름과 팩토리를 export한다.
- 타입 힌트와 생성된 API 레퍼런스는 `_runtime`이나 `_native` 구현 경로를 공개
  타입으로 언급하지 않는다.

## 라이브러리 형태

바인딩은 네이티브 백엔드를 가진 Python 패키지처럼 느껴져야 한다.

- 네이티브 기반 공개 리소스, 빌더, 핸들러, 재사용 저장소 표면은
  `zlink.contracts` 안의 `typing.Protocol` 계약으로 선언한다.
- 계약 클래스는 호출 가능한 형태만 설명한다. `__new__` 안에 팩토리 로직을 숨기지
  않고, 호출자는 계약 클래스를 직접 인스턴스화하지 않는다.
- 구체 런타임 클래스는 네이티브 리소스 수명을 소유하고 `close()`를 제공하며,
  가능한 경우 context manager 사용을 지원한다.
- routing id, topology entry, enum 값, result 도메인, 예외처럼 순수 Python
  데이터인 값과 스냅샷은 구체 타입으로 유지한다. 네이티브 기반 메시지와 수신
  저장소는 런타임 구현이 구체 Python 클래스여도 팩토리로 생성한다.
- 타입 힌트는 공개 호출 형태를 기술하지만, 비공개 네이티브 상태는 숨긴다.
- 네이티브 핸들, raw FFI 포인터, 콜백 userdata, request 펌프, part-loop
  시퀀싱은 비공개 모듈에 둔다.

perf나 샘플의 편의를 위해 비공개 확장 객체를 노출하지 않는다.

## 정적 타입 원칙

Python 바인딩은 엄격한 정적 타입 검사를 적용하는 라이브러리로 유지한다. 이 원칙은
Python의 동적 실행 모델을 없애기 위한 것이 아니다. 공개 계약을 사용하는 코드가 IDE와
정적 타입 검사기에서 정확한 인자, 반환값, 콜백과 소유권 경계를 확인할 수 있게 하는
것이 목적이다.

- 공개 함수, 메서드, 속성, 생성자 역할의 팩토리, 콜백과 비동기 문맥 관리자는
  모든 인자와 반환 타입을 빠짐없이 선언한다. 공개 타입에서 암시적인 `Any`가 발생하면
  타입 계약이 완료된 것으로 보지 않는다.
- 타입 정보는 가능한 한 구현 소스에 직접 선언한다. 배포 패키지에는 PEP 561의
  `py.typed` 표시 파일을 포함하고, wheel과 sdist를 설치한 외부 프로젝트에서도 같은 타입
  정보를 확인할 수 있어야 한다.
- `Protocol`은 호출자가 구현을 대체할 수 있는 구조적 계약이나 비공개 구현을 숨겨야 하는
  공개 역할에 사용한다. 실제로 `isinstance()` 또는 `issubclass()` 검사가 필요한 계약만
  `runtime_checkable`로 선언한다. 이 검사는 메서드 존재 여부만 확인하므로 런타임 값 검증을
  대신하지 않는다.
- 입력 타입은 구현이 허용하는 범위 안에서 `Iterable`, `Sequence`, `Mapping`과 Python
  buffer protocol처럼 넓은 계약을 사용한다. 구체 결과를 만들어 반환하는 API는 `list`,
  `dict`, 구체 값 객체처럼 호출자가 바로 사용할 수 있는 반환 타입을 선언한다.
- 설정, snapshot, result와 다른 순수 Python 값은 타입이 없는 `dict` 대신 dataclass,
  `TypedDict`, enum 또는 명명된 값 타입을 사용한다. 어느 형태를 선택할지는 값의 변경
  가능성, 런타임 객체 여부와 공개 계약 의미로 결정한다.
- `Any`는 네이티브 콜백의 userdata, 아직 타입을 알 수 없는 외부 객체, 의도적인 raw
  payload 경계처럼 타입 체계로 정확히 표현할 수 없는 곳에만 둔다. 모든 Python 값을 받을
  뿐 반환 후 타입 정보를 사용하지 않는 인자는 `Any` 대신 `object`를 우선한다.
- 타입 annotation은 네이티브 입력, 네트워크 payload와 고정 크기 값의 런타임 검증을
  대신하지 않는다. 바인딩은 기존 에러와 검증 정책에 따라 실제 값을 별도로 검증한다.
- 지원하는 최소 Python version에서 해석할 수 없는 타입 문법을 사용하지 않는다.
  `pyproject.toml`의 `requires-python`, wheel metadata, CI 버전 조합과 타입 검사기의
  대상 버전은 항상 일치해야 한다.

엄격한 타입 정책이 다른 언어의 설계 관습을 그대로 가져오는 근거가 되어서는 안 된다.
불필요한 인터페이스 계층, getter/setter 반복, 복잡한 제네릭 중첩과 단순 호출을 길게 만드는
builder를 타입 검사만을 위해 추가하지 않는다. Python의 타입 추론과 기본 자료형으로 충분한
구현은 간결하게 유지하고, 공개 API와 네이티브 호출 경계를 우선해서 명확하게 선언한다.

## 계약 / 런타임 배치 규칙

- 공개 클래스, 타입 alias, 예외, enum, 빌더 계약은 대응하는 `zlink/contracts/`
  카테고리에 속하며, 사용자가 직접 import해야 하는 경우 `zlink`에서 재-export
  한다.
- 공개 모듈 함수, 클래스/정적 헬퍼, 편의 메서드, 빌더 헬퍼 함수는 호출자가 직접
  사용할 수 있다면 공개 패키지 모듈에 속한다.
- Python 런타임 구현, 핸들 소유자, request 펌프, 콜백 어댑터, part-loop 헬퍼는
  `_runtime`에 속한다.
- 네이티브 확장 바인딩, FFI 선언, 네이티브 struct mirror, marshalling 헬퍼,
  플랫폼 로딩 코드는 `_native`에 속한다.
- `zlink.__init__`, 타입 힌트, 생성된 API 레퍼런스는 공개 패키지 카테고리를
  프로젝션하며 비공개 런타임 모듈을 노출하지 않는다.
- 런타임의 구체 클래스는 패키지 루트 팩토리 뒤에 있는 생성 대상이다. 호출자는
  비공개 모듈을 직접 import하지 않는다.
- 계약 이름은 숨은 `__new__` 팩토리 dispatch를 구현하지 않는다. 대문자로 시작하는
  계약 이름은 타입 표면이지 생성 단축 경로가 아니다.
- 패키지 루트 팩토리는 런타임 구현을 연결하기 위해서만 `_runtime`을 import할 수
  있다. 그 공개 어노테이션은 비공개 런타임 클래스가 아니라 계약 이름을 사용한다.

## 계약 파일 레이아웃

계약 소스는 [.NET 바인딩 청사진](../dotnet/README.ko.md)과 같은 분류를
Python 네이밍으로 유지한다. 다른 바인딩을 아는 개발자가 동일한 공개 개념을
Python에서 빠르게 찾을 수 있도록 동일한 개념적 파일 그룹화를 유지한다.

- `core/`: `context.py`, `zlink.py`, `routing_id.py`와 코어 옵션/값 파일.
- `messaging/`: `message.py`, `received.py`, `topic_message.py`,
  `subscription_event.py`와 공통 operation payload 타입.
- `sockets/`: 소켓 클래스/프로토콜, 소켓 옵션 타입, send/request/reply 빌더
  계약, 스트림 패킷 핸들러 계약, 소켓 플래그.
- `eventing/`: 모니터, monitor event/status, poller, poll event, timer, 이벤트
  핸들러 계약.
- `service/`: SPOT node, Spot, Actor, topology 모델, service operation builder를
  담는 `spot/` 서브패키지로 둔다.
- `errors/`: 공개 예외 클래스, result 도메인, error 코드 매핑.

공개 리소스 동작을 위한 단일 집계 `models.py`나 비공개 런타임 export barrel을
피한다. 작은 DTO 형 dataclass와 literal 타입은 의미를 부여하는 계약과 함께
그룹화할 수 있지만, 네이티브 기반 리소스와 operation 빌더는 명명된 계약 파일이
필요하다.

## 런타임 파일 레이아웃

런타임 소스는 [.NET 바인딩 청사진](../dotnet/README.ko.md)의 런타임 분류를
미러링하지만 구현만을 담는다.

- `core/`: context 구현과 context 옵션 헬퍼.
- `messaging/`: 메시지 materialization, request 실행, 네이티브 버퍼 변환 헬퍼.
- `sockets/`: 소켓 베이스 클래스, 소켓 커널, 모든 소켓 패밀리의 소켓 구현, 콜백
  어댑터, operation 구현 클래스.
- `eventing/`: poller/timer/monitor 구현과 이벤트 materialization 헬퍼.
- `service/`: SPOT 노드, Spot, Actor, topology, 서비스
  operation 구현.
- `errors/`: 네이티브 에러 변환과 검증 헬퍼.
- `_native/`: 확장 로딩, 플랫폼 lookup, 네이티브 바인딩 표면.

런타임 파일은 계약 타입을 import할 수 있지만, 계약 파일은 런타임 파일을
import하지 않는다. 패키지 루트는 팩토리에서 런타임 구현을 인스턴스화할 수
있지만, 비공개 구현 모듈이 아니라 계약 이름을 export한다.

## 네이티브 브리지 구현 규칙

Python 바인딩의 목표 구현은 얇은 Python 공개 표면과 비공개 컴파일된 네이티브
브리지다. 공개 API는 `zlink` 패키지와 `contracts/`가 소유하고, 성능에 민감한
코어 호출 경계는 `_native` 아래의 컴파일된 확장 모듈이 소유한다.

`ctypes`나 CFFI ABI 모드는 플랫폼 로딩, 진단, 전이 기간의 대체 경로처럼 낮은 빈도
경로에만 허용한다. send, routed send, publish, recv, subscribe, router recv,
request/reply, SPOT data-plane처럼 메시지 수에 비례해 반복되는 경로는 컴파일된
확장 모듈을 통해야 한다. 이 규칙은 Python 호출 횟수, 동적 marshalling, part별
객체 생성을 줄이기 위한 것이다.

컴파일된 확장 모듈은 공개 API가 아니다. 모듈 이름, capsule 타입, 네이티브 소유자
타입, 에러 보조 함수는 `zlink.__init__`, 타입 힌트, API 레퍼런스, 샘플, perf에
노출하지 않는다. 사용자는 계속 `zlink` 공개 팩토리와 공개 객체만 사용한다.

### 핵심 경로 브리지

네이티브 브리지는 최소한 아래 작업을 한 번의 Python 확장 호출 안에서 처리해야
한다.

- Python buffer protocol을 지원하는 payload를 검증하고 `zlink_msg_t` part 배열로
  변환한다.
- `zlink_send_part`, `zlink_send_part_rid`, `zlink_publish_part` 계열을 part 수만큼
  native 루프에서 호출한다.
- `zlink_recv_part`, `zlink_subscribe_part`, `zlink_router_recv_part` 계열을 native
  루프에서 호출하고, 비공개 수신 소유자와 메타데이터를 하나의 결과로 반환한다.
- 실패한 send/recv 시 남은 native part를 닫고 errno/result를 Python 공개 예외
  도메인으로 변환할 수 있는 정보를 반환한다.
- blocking send/recv/request 구간에서 Python 객체를 만지지 않는 동안 GIL을
  release한다.

Python 런타임 계층은 이 비공개 결과를 `Received`, `TopicMessage`,
`SubscriptionEvent`, request 결과 같은 공개 객체로 감싼다. 비공개 수신 소유자는
native-backed owner나 bytes-backed owner가 될 수 있지만 공개 객체의 lifetime
규칙은 같아야 한다. raw pointer나 capsule을 사용자가 만질 수 있게 하지 않는다.

### 버퍼와 복사 정책

송신 payload는 `bytes`, `bytearray`, `memoryview`처럼 Python buffer protocol을
지원하는 값을 기본 입력으로 받는다. 네이티브 브리지는 메모리가 연속인지와 읽기
전용인지 검사하고 필요한 경우에만 복사한다. core가 메시지 소유권을 가져가는
호출에서는 native part lifetime을 명확히 하고, Python buffer를 빌려 쓰는 경우에는
Python 객체 lifetime이 native 제출 완료 시점까지 유지되도록 소유자가 보존해야
한다.

수신 payload는 공개 수신 객체가 열려 있는 동안 유효한 buffer view로 노출할 수
있어야 한다. native-backed owner는 core buffer를 직접 노출할 수 있고,
bytes-backed owner는 비공개 bytes 저장소를 노출할 수 있다. `to_bytes()`는 명시적
복사 또는 bytes-backed 저장소 반환 경로다. 공개 `Received` 객체가 닫힌 뒤에는
기존과 같이 수신 part 접근이 실패해야 한다.

### 콜백과 스레드 정책

네이티브 callback trampoline은 `_runtime`과 `_native` 사이의 비공개 경계에 둔다.
callback에서 Python handler를 호출해야 할 때만 GIL을 획득하고, handler 실행은
기존 dispatcher 규칙을 따른다. callback 경로도 비공개 소유자를 통해 공개 수신
객체가 lifetime을 관리하게 한다.

### 빌드와 패키징

Python 패키지는 컴파일된 확장 모듈을 wheel에 포함해야 한다. 확장 모듈은 패키징된
`libzlink` artifact와 같은 runtime lookup 규칙을 사용해야 하며, 사용자가 별도의
환경 변수를 설정하지 않아도 기본 wheel이 동작해야 한다.

소스 빌드에서는 필요한 C/C++ compiler와 Python 개발 헤더가 없을 수 있다. 이 경우
명확한 빌드 오류를 내야 하며, 조용히 순수 Python 핵심 경로로 떨어져 성능 수치를
왜곡하면 안 된다. 대체 경로가 필요한 경우에도 perf runner는 대체 경로 사용 여부를
출력하고, 공식 성능 수치로 취급하지 않는다.

## 생성 진입점

공개 생성은 패키지 루트 팩토리와 공개 소유자 메서드가 제공한다.

- `create_context()`는 네이티브 기반 context 구현을 생성한다.
- `create_pair_socket(...)`, `create_dealer_socket(...)`,
  `create_router_socket(...)`, `create_pub_socket(...)`,
  `create_sub_socket(...)`, `create_xpub_socket(...)`,
  `create_xsub_socket(...)`, `create_stream_socket(...)`은 네이티브 기반 소켓 구현을
  생성한다.
- `create_spot_node(...)`는 서비스 계층 구현을 생성한다.
- `Spot` 핸들은 `SpotNode.create_spot()`, `entry_spot()`,
  `get_or_create_spot(...)`, `spot_lookup(...)`을 통해 얻는다. `Spot`의 직접
  생성은 공개되지 않는다.
- Actor 핸들은 `SpotNode.create_actor(...)`를 통해 생성한다. Actor의 직접 생성은
  공개되지 않는다.
- `create_poller()`, `create_poll_events(...)`, `create_timer()`,
  `create_timer_from_spot(...)`은 eventing 리소스를 생성한다.
- `create_message(...)`, `allocate_message(...)`, `create_received()`,
  `create_topic_message()`, `create_subscription_event()`는
  재사용 가능한 messaging 저장소를 생성한다.
- 버전, capability 조회, strerror, proxy, sleep, multipart cleanup 헬퍼는 공개 패키지
  함수다. 이 함수들 뒤의 네이티브 호출은 비공개 모듈에 둔다.

## 계약 카테고리 맵

`zlink/contracts/` 패키지가 `zlink`에서 export되는 이름의 소유권 맵이다.

- `core/`: context, context 옵션, routing id, version/capability 조회 헬퍼, 유틸리티
  계약.
- `messaging/`: 메시지, 수신 메타데이터, 토픽 메시지, subscription 이벤트,
  스트림 패킷 데이터, 빌더 payload 헬퍼.
- `sockets/`: 소켓 동작, 소켓 패밀리, 타입드 옵션, request/reply, publish/
  subscribe 표면.
- `eventing/`: 모니터, monitor snapshot/event, poller, poll event, timer, 공개
  poll 헬퍼.
- `service/`: SPOT 노드, SPOT 핸들, topology 모델, actor
  ref, actor 생명주기, operation 빌더.
- `errors/`: 타입드 예외 도메인.
- enum, flag, result 타입은 그 의미를 정의하는 카테고리에 둔다. 단지 문법으로
  선언을 묶기 위해 `enums` 패키지를 만들지 않는다.

## 표준 인터페이스 규칙

- 데이터-플레인 `recv_into`, routed recv, `subscribe_into`, subscription-event
  수신은 호출자가 제공한 `Received`, `TopicMessage`, `SubscriptionEvent` 객체를
  채우고 `bool`을 반환한다.
- send, routed send, publish, request, reply, SPOT operation, Actor 위치/세션
  operation은 fluent 빌더를 반환한다.
- 빌더 시작 메서드는 대상 identity, 토픽, 채널, routing id, request sequence만
  받는다. payload, flag, timeout, callback, 비동기 submit 선택은 빌더 단계다.
- SPOT 채널 대상 operation은 `send_to_channel(...)`과
  `request_to_channel(...)`을 사용한다. SPOT 토픽 publish는 `publish(topic)`을
  유지한다.
- operation 시작 메서드와 같은 이름의 단일-payload 단축 메서드를 추가하지
  않는다. `send(message)`, `send(routing_id, message)`,
  `publish(topic, message)`, `send_to_channel(channel, message)`,
  `send_to_spot(..., message)`는 공개 계약 멤버가 아니다. 호출자는
  `send(...).message(message).submit()`을 사용한다.
- multipart payload는 반복적인 `message(...)` 호출로 누적한다. Python 스타일의
  `messages(*parts)` 편의는 동일 빌더에 위임할 수 있다. 이 편의는 export될 때
  공개 계약이며 공개 패키지 카테고리에 속한다.
- dealer 소켓은 `request_frame(...)`이나 `reply(request_token, parts)` 같은
  프로토콜 envelope 헬퍼를 노출하지 않는다. dealer는 `request()`로 request를
  시작할 수 있지만, API 수준의 피어 routing id가 없으므로 임의의 토큰에 답할 수
  없다.
- 메시지 payload 팩토리는 `Message.from_(...)`을 사용한다. Python에서는
  `from`이 예약어이므로 뒤에 밑줄을 붙인다. `create_message_from`,
  `copy_from`, `from_bytes`는 공개 계약의 일부가 아니다.
- `send_no_wait`, `publish_with_flags`, `request_async` 같은 operation-start
  메서드 패밀리를 추가하지 않는다. operation 이름은 하나로 유지하고 변이는
  빌더가 흡수하도록 둔다. request 완료는 `submit(callback)`으로 전달하고,
  Python coroutine 표면은 framework가 별도로 제공한다.

## 공개 패키지 형태

`zlink` 패키지는 도메인 단위 그룹을 노출한다.

- Core: context, version/capability 조회 헬퍼, 옵션, 유틸리티.
- Messaging: 메시지, routing id, 수신 메타데이터, 토픽 메시지, subscription
  event, 스트림 패킷 데이터.
- Sockets: pair, dealer, router, pub, sub, xpub, xsub, stream, 타입드 옵션,
  콜백, request/reply, publish/subscribe, 스트림 패킷 API.
- Eventing: 모니터, monitor snapshot/event, poller, poll event, timer.
- Service: SPOT 노드, SPOT 핸들, topology 스냅샷, actor
  ref, actor 생명주기, operation 빌더.
- Errors: 코어 result 도메인을 보존하는 타입드 예외 클래스.

## 필수 기능 범위

바인딩이 공통 .NET-표준 정책에 정렬되면, 공개 패키지는 다음 안정적인 사용자
대상 기능을 모두 포함한다.

- context 생명주기, 옵션, shutdown, auto-HWM 재계산, version, capability 조회,
  strerror.
- 메시지 ownership, multipart payload, routing id, 수신 메타데이터, 토픽 메시지,
  subscription event, 스트림 패킷 콜백.
- 모든 소켓 패밀리와 타입드 옵션. `SubSocket.subscription_at(index)`와
  `XSubSocket.subscription_at(index)`는 해당 인덱스의 subscription filter와
  pattern 여부를 반환한다. 해당 인덱스가 없으면 `None`을 반환한다.
- 모니터, poller, timer, readiness 의미.
- SPOT 노드, SPOT 핸들, topology 스냅샷, actor, 스트림
  actor 바인딩.

Python 이름은 Python 스타일을 따를 수 있지만, 동작은 코어 기능의 의미와
일치해야 한다.

## Spot Get-Or-Create

Python은 `SpotNode.get_or_create_spot(spot_rid)`를 노출한다. 이 메서드는
`zlink_spot_node_spot_get_or_new(...)`에 직접 매핑한다. `spot_lookup`과
`create_spot`을 조합해 구현하지 않는다.

이 메서드는 `(spot, created)`를 반환한다. 반환된 `Spot`은 호출자 소유이며
일반적인 방식으로 close되어야 한다. `created`는 논리적 spot을 실제로 생성한
호출에서만 `True`다.

## Receive와 Subscribe 형태

- 데이터-플레인 receive와 subscribe API는 재사용 가능한 저장소를 위해 호출자가
  제공하는 결과 객체를 사용한다.
- 비차단 호출에서 데이터가 없을 때는 `False`를 반환하며, 이는 강한 수신 실패와
  구분된다.
- 강한 수신 실패는 문서화된 zlink 예외를 발생시킨다.
- SPOT readable dispatch 이벤트는 readiness 알림이다. 호출자는 데이터가 없을
  때까지 일치하는 receive API를 비운다.
- Actor join request 수신 같은 서비스 control/admission 수신 경로는 재사용
  가능한 데이터-플레인 저장소보다 더 명확할 때 `None`, optional, 또는 타입드
  result-return 형태를 사용할 수 있다. 이 경우에도 데이터가 없는 상황과 강한
  수신 실패는 여전히 구분되어야 한다.

## 에러와 검증 정책

- 네이티브 고정 크기 id와 문자열은 확장이나 FFI 코드를 호출하기 전에 검증한다.
- routing id, actor id, 엔드포인트, 채널 이름, 토픽을 조용히 자르지 않는다.
- submit, request, recv, handler, close, bind, connect, config 에러 도메인을
  타입드 예외에 보존한다.
- 공개 API는 호출자가 네이티브 errno를 직접 검사하도록 요구하지 않는다.

## 성능 정책

- hot path는 reflection 스타일의 attribute lookup, 문자열 기반 동적 dispatch,
  회피 가능한 allocation, 회피 가능한 `bytes` 복사, 숨은 sleep, busy wait, 넓은
  lock, thread join을 사용하지 않는다.
- 메시지 수에 비례하는 핵심 경로는 비공개 컴파일된 확장 모듈을 통과한다.
  `ctypes`/CFFI ABI 호출을 part별 Python 루프에서 반복하는 구조는 공식 성능
  구현으로 보지 않는다.
- 네이티브 확장 코드는 공개 Python 값을 코어 part 기반에서 직접 materialize한다.
- blocking native 작업은 Python 객체 접근 구간과 분리하고, 가능한 동안 GIL을
  release한다.
- 핸들 단위로 진행을 공유할 수 있는 경우, 요청마다 별도의 polling 스레드나
  타이머를 사용하지 않는다.
- perf, 샘플, 테스트는 공개 `zlink` export만 사용한다.

## 구현 체크리스트

- `zlink.__all__` 또는 패키지 export 표면이 의도한 공개 계약과 일치한다.
- 언더스코어 모듈이 공개 시그니처를 통해 새지 않는다.
- 리소스 클래스에는 명시적인 close 의미가 있다.
- export된 모듈 함수와 빌더 편의 메서드는 런타임 헬퍼에만 두지 않고 공개 패키지
  모듈에 선언한다.
- receive/subscription 의미가 공통 바인딩 정책과 일치한다.
- 서비스 control/admission 수신 예외는 데이터-플레인의 호출자 제공 저장소와
  다른 지점을 문서화한다.
- perf 의미가 `bindings/c/perf`와 일치한다.
- `zlink/contracts`는 `zlink/_runtime`이나 `zlink/_native`에 대한 import나
  export 의존성을 갖지 않는다.
- `zlink.__init__`은 팩토리 wiring을 위해서만 비공개 런타임 모듈을 import하며,
  비공개 구현 타입 이름을 export하지 않는다.
- 테스트, 샘플, examples, perf는 언더스코어 모듈을 import하지 않는다.
- 네이티브 기반 리소스는 패키지 루트 팩토리나 계약 메서드를 통해 생성되며
  계약 클래스/프로토콜로 타이핑된다.
- 공개 함수, 메서드, 속성, 콜백과 비동기 문맥 관리자의 타입이 완전하며,
  공개 계약에 암시적인 `Any`가 남아 있지 않다.
- wheel과 sdist에 `py.typed`가 포함되고, 설치한 패키지를 사용하는 외부 프로젝트의
  정적 타입 검사가 엄격한 설정으로 통과한다.
- `requires-python`, 실제 annotation 문법, CI의 최소 버전과 타입 검사 대상 버전이
  서로 일치한다.
- 공식 perf 경로가 컴파일된 확장 모듈을 사용하며, 대체 경로가 조용히 성능
  결과에 섞이지 않는다.
- 호환성만을 위한 옛 별칭, 중복된 operation-start 이름, deprecated 래퍼가
  남아있지 않다.

Python 리팩토링 이후 필수 검증. `bindings/python/`에서 다음 명령을 실행한다.

- 로컬 테스트에 필요한 경우 `python -m pip install -e .` 또는 저장소의 표준
  editable 빌드 명령을 실행한다.
- `./tests/run_tests.sh`를 실행한다.
- 공개 examples나 생성 경로가 변경된 경우 `./samples/run_samples.sh`를 실행한다.
- hot path, receive, send, request, poller, timer, 서비스 동작이 변경된 경우
  smoke 게이트로 `./perf/run_benchmarks.sh`와
  `./perf/run_benchmarks_multi.sh`를 실행한다.
- 저장소가 선택한 정적 타입 검사기를 엄격한 설정으로 실행한다. 공개 타입 추론을
  확인하는 외부 프로젝트 검사와 패키지 설치 후 타입 검사를 함께 실행한다.
- `src/zlink/contracts`, `tests`, `samples`, `examples`, `perf`에서
  `zlink._runtime`, `zlink._native`, 비공개 확장 모듈, 생성된 비공개 파일로부터의
  import를 검색한다. `src/zlink/__init__.py`는 별도로 확인해, 비공개 import가
  팩토리 wiring에 한정되며 공개 타입 힌트에 등장하지 않는지 확인한다.

## Actor와 Spot 경로 결과

Python은 Actor와 Spot 경로 lookup 결과를 공개 result 객체로 노출한다.

- `ActorRoute`는 resolve된 Actor ref, Actor 노드 RID, 현재 Spot RID, 현재 Spot
  종류를 보존한다.
- `SpotRoute`는 Spot RID, 소유 노드 RID, Spot 종류를 보존한다.
- `SpotKind`는 Entry Spot과 사용자 Spot을 구분한다. 유효하지 않은 종류는
  성공적인 경로 결과가 아니다.
- SpotNode 스냅샷 엔트리는 코어 스냅샷과 동일한 Spot 종류/현재 Spot 필드를
  노출한다.

Python은 resolve된 Actor ref를 인자로 받는 `SpotNode.send_to_actor(actor_ref)`와
`SpotNode.request_to_actor(actor_ref)`를 노출한다. send operation은 submit이
성공하면 하나 이상의 message part 소유권을 넘기고, Actor 소유자 mailbox가 인계를 받으면
완료된다. request operation은 submit이 성공하면 요청 part의 소유권을 넘기고,
Actor handler가 만든 reply part를 전달한다. Python은 제거된 Discovery route
table이나 resolver API를 compatibility helper로 되살리면 안 된다.
