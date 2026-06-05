# Framework option builder naming

이 문서는 framework option builder 이름을 정할 때 적용하는 공통 원칙이다.
언어마다 표기법은 다를 수 있지만, 이름이 드러내는 의미는 같아야 한다.

## 기본 원칙

framework option builder 는 설정 객체를 조회하는 표면이 아니라, runtime 등록 모델을
만드는 표면이다. 따라서 상태를 추가하거나 capability 를 켜는 메서드는 그 동작이
이름에 드러나야 한다.

- `add*` 는 새 등록 항목을 추가할 때 사용한다.
  예: `addClientServerChannel`, `addStreamNode`, `add_spot_mesh`
- `enable*` 는 이미 선택한 등록 항목 안에서 capability 를 켤 때 사용한다.
  예: `enableServer`, `enableClient`, `enable_publisher`
- `use*` 는 정책, discovery, filter 처럼 등록 항목에 적용되는 선택 기능을 사용할 때
  사용한다.
  예: `useDiscovery`, `use_handler_group`
- `configure*` 는 하위 option 객체를 넘겨 세부 값을 바꿀 때 사용한다.
  예: `configureDispatch`, `configure_metadata`
- `bind` 는 local endpoint 를 열 때 사용하고, `connect` 는 remote endpoint 로 연결할 때
  사용한다.

## 피해야 하는 이름

`server()`, `client()`, `publisher()`, `subscriber()`, `router()`, `dealer()` 처럼 명사만
있는 이름은 피한다. 이런 이름은 단순 조회처럼 보이지만, framework option builder 에서는
대부분 capability 를 활성화한다. 실제 동작이 활성화라면 `enableServer()` 또는
`enable_server()` 처럼 쓴다.

## 언어별 표기법

각 언어는 그 언어의 일반 표기법을 따른다.

| 언어 | 등록 예 | capability 예 |
|------|---------|---------------|
| .NET | `AddClientServerChannel` | `EnableServer` |
| Java | `addClientServerChannel` | `enableServer` |
| Node | `addClientServerChannel` | `enableServer` |
| C++ | `add_client_server_channel` | `enable_server` |

Kotlin DSL 은 Java builder 를 감싼 얇은 표면이므로 Java 이름의 의미를 바꾸지 않는다.

## 설계 이유

이 규칙은 호출자가 builder 의 내부 상태 모델을 몰라도 코드를 읽을 수 있게 하기 위한
것이다. `add*` 와 `enable*` 를 구분하면 등록과 활성화가 분리되어 보이고, `bind` 와
`connect` 를 구분하면 endpoint 방향이 드러난다. 이 방식은 option builder 를 더 깊은
모듈로 만들고, 호출자에게 숨은 전제 지식을 덜 요구한다.
