# Python Socket Surface 분리 실행 가이드

> 상태: 완료
> 기준 범위: `bindings/python/`
> 작업 범위: `bindings/python/src/zlink/`, `bindings/python/tests/`, `bindings/python/examples/`, `bindings/python/plan/socket/`
> 목적: Python socket public surface를 POSD 기준으로 concrete facade 중심으로 분리하고, compat `Socket`을 유지한 채 구현/검증/문서 정렬까지 끝낸다.
> 최종 종료 판정: `미적용 사항이 없습니다.`

## 1. 문서 목적

이 문서는 Python socket surface 분리 작업의 유일한 실행 authority다.

이미 작성된
[`2026-03-27-python-socket-surface-detailed-design.ko.md`](./2026-03-27-python-socket-surface-detailed-design.ko.md)
는 설계 입력 문서로만 취급한다. 실제 실행 중 우선순위와 완료 판정은 이 guide가
결정한다.

실행 중 설계/순서/호환성 판단이 바뀌면:

1. 먼저 이 guide를 갱신한다.
2. 그 다음 코드와 테스트를 수정한다.
3. 설계 입력 문서가 필요하면 마지막에 동기화한다.

즉, 분리 실행 중에는 이 guide 한 파일만 계속 갱신한다.

## 2. 고정 목표

이번 루프의 최종 목표는 아래 다섯 가지다.

- `Socket`의 과도한 책임을 공통 심부 모듈과 concrete facade로 분리한다.
- 새 public surface를 `PairSocket`, `DealerSocket`, `RouterSocket`,
  `StreamSocket`, `PubSocket`, `SubSocket`, `XPubSocket`, `XSubSocket`
  중심으로 정착시킨다.
- compat `Socket(ctx, SocketType.X)`는 유지하되 concrete facade 반환 구조로
  축소한다.
- 예제와 테스트를 새 surface 기준으로 정렬한다.
- 구현 완료 기준을 pytest와 examples smoke로 닫는다.

## 3. 적용 원칙

- POSD 우선:
  - 공통 로직은 깊은 내부 모듈로 모으고, public surface는 concrete 타입 중심으로
    줄인다.
- compat 우선:
  - `Socket(ctx, SocketType.X)`는 유지한다.
  - `isinstance(sock, zlink.Socket)` 호환도 유지한다.
- surface 제한 우선:
  - topic 계층과 raw message 계층은 public API에서 분리한다.
  - unsupported 동작은 runtime 설명보다 API 자체에서 숨기는 방향을 우선한다.
- import cycle 금지:
  - `_core.py` helper는 당장 유지하고, 새 socket 계층이 이를 재사용한다.
  - `_monitor.py`, `_poller.py`, `_spot.py`, `_discovery.py`는 새 socket 모듈을
    직접 import하지 않는다.
- fail-fast 검증:
  - 테스트는 sleep/retry로 문제를 가리지 않는다.
  - 실패 시 원인 코드 수정이 우선이다.
- 단일 authority:
  - 실행 중 새 main/master/gap/residual 문서를 만들지 않는다.
  - 이 guide만 갱신한다.

## 4. 금지 규칙

- `bindings/python/` 바깥으로 범위를 확장하지 않는다.
- `core/` 수정으로 우회하지 않는다.
- ad-hoc repro 스크립트나 `/tmp` 실험물로 증명하지 않는다.
- generic `Socket`의 full surface를 영구 유지하는 방향으로 타협하지 않는다.
- 테스트 통과만 보고 surface 분리가 끝났다고 판단하지 않는다.

## 5. iteration 작업 순서

각 iteration은 아래 순서를 따른다.

1. 이 guide의 첫 미완료 slice를 고른다.
2. 관련 현재 코드를 읽고 import/호환성 제약을 다시 확인한다.
3. 코드 수정 전에 필요한 경우 이 guide 상태/메모를 갱신한다.
4. 코드 수정, 테스트/예제 정렬, export 정리를 한 묶음으로 수행한다.
5. 해당 slice 검증 명령을 실행한다.
6. 이 guide 상태를 갱신한다.
7. 다음 slice가 남아 있으면 바로 이어서 진행한다.

한 iteration 안에서 계속 진행 가능하면 멈추지 않는다.

## 6. 기본 검증 명령

최소 smoke:

```bash
./bindings/python/plan/socket/run_python_socket_surface_loop.sh --max-iterations 0
```

단계별 주 검증:

```bash
cd bindings/python && python -m pytest -q tests/test_version.py tests/test_enums.py tests/test_core_api_alignment.py
cd bindings/python && python -m pytest -q tests/integration
cd bindings/python && python -m pytest -q
```

필요 시 보강 검증:

```bash
cd bindings/python && python -m pytest -q tests/test_bench_fastpath.py
```

example smoke는 concrete facade 전환이 시작된 뒤 아래를 최소 기준으로 본다.

```bash
cd bindings/python && python examples/pair_recv.py
cd bindings/python && python examples/pubsub_recv.py
cd bindings/python && python examples/dealer_router_recv.py
```

## 7. 작업 레지스터

상태 값은 아래만 사용한다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

### Slice 1. 공통 socket 계층 추출

상태: `완료`

대상:

- `src/zlink/_core.py`
- `src/zlink/_socket_base.py`

작업:

- `_SocketHandle`, `_BaseSocket`, `Socket`, `MessageSocket`,
  `PublisherSocket`, `SubscriberSocket`를 새 모듈로 분리
- `_core.py`에는 helper, `Context`, `Message`, `Received*`를 유지
- `Socket` compat base를 새 계층 위로 옮기되 Phase 종료 전까지는 기존 동작을
  내부 forwarding으로 유지 가능

완료 기준:

- 새 `_socket_base.py`가 생긴다
- 현재 테스트가 깨지지 않는다
- import cycle이 생기지 않는다

검증:

```bash
cd bindings/python && python -m pytest -q tests/test_version.py tests/test_enums.py tests/test_core_api_alignment.py
```

### Slice 2. concrete socket 타입 도입

상태: `완료`

대상:

- `src/zlink/_socket_types.py`
- `src/zlink/__init__.py`

작업:

- concrete socket 8종 추가
- type별 option 메서드 public 노출
- `XPubSocket.subscription_event()` 소속 고정
- `Socket(ctx, SocketType.X)`가 concrete facade를 반환하도록 준비

완료 기준:

- concrete class 직접 생성이 가능하다
- `isinstance(zlink.Socket(ctx, zlink.SocketType.PAIR), zlink.Socket)`가 유지된다
- 새 클래스가 `__init__.py`에서 export된다

검증:

```bash
cd bindings/python && python -m pytest -q tests/test_enums.py tests/test_core_api_alignment.py tests/integration
```

### Slice 3. compat surface 축소

상태: `완료`

대상:

- `src/zlink/_socket_base.py`
- `src/zlink/_core.py`
- `src/zlink/__init__.py`

작업:

- `Socket.__new__` dispatch 구현
- `recv(size)`를 compat API로만 남김
- `set_*_handler`는 deprecated alias로 유지하고 `on_*` canonical alias 추가
- generic `Socket`이 의미별 raw/topic API를 다시 모아두지 않도록 정리

완료 기준:

- `Socket(ctx, SocketType.X)`가 concrete instance를 돌려준다
- concrete facade 사용 시 raw/topic surface가 분리된다
- compat API warning 정책이 코드에 반영된다

검증:

```bash
cd bindings/python && python -m pytest -q tests/test_core_api_alignment.py tests/integration
```

### Slice 4. tests / examples 정렬

상태: `완료`

대상:

- `tests/test_core_api_alignment.py`
- `tests/test_enums.py`
- `tests/integration/`
- `examples/`

작업:

- concrete facade 생성 smoke 추가
- surface restriction test 추가
- callback alias test 추가
- examples를 concrete facade 기준으로 전환

완료 기준:

- 새 public surface 기준 contract test가 있다
- examples가 generic `Socket` 대신 concrete facade를 기본 사용한다
- `XPubSocket.subscription_event()` 회귀 검증이 있다

검증:

```bash
cd bindings/python && python -m pytest -q
cd bindings/python && python -m pytest -q tests/integration
```

### Slice 5. 문서/실행 자산 정리

상태: `완료`

대상:

- `plan/socket/`
- 필요 시 `src/zlink/__init__.py` export 설명 반영 지점

작업:

- 이 guide 상태 갱신
- 상세 설계 문서와 실제 구현 차이 동기화
- loop wrapper에서 legacy `--master-plan` 표면을 제거하고 execution guide 단일
  authority 규칙과 맞춘다.
- wrapper smoke 재확인

완료 기준:

- 이 guide에 미완료 slice가 없다
- detailed design과 실제 구현의 주요 결정이 어긋나지 않는다
- loop wrapper가 guide 한 파일만 authority로 취급한다
- loop wrapper smoke 성공

메모:

- detailed design을 실제 코드 구조(`_socket_base.py`, `_socket_types.py`,
  concrete facade export, canonical `on_*` callback alias) 기준으로 동기화했다.
- loop wrapper는 외부 option/help/output에서 legacy `--master-plan` 표면을
  제거하고 execution guide 단일 authority만 노출하도록 정리했다.
- wrapper smoke:
  `./bindings/python/plan/socket/run_python_socket_surface_loop.sh --max-iterations 0`
  성공
- 검증:
  `cd bindings/python && python -m pytest -q`
  -> `49 passed, 4 skipped`
- example smoke:
  `cd bindings/python && python examples/pair_recv.py`
  -> `hello`
  `cd bindings/python && python examples/pubsub_recv.py`
  -> `prices [b'101.25']`
  `cd bindings/python && python examples/dealer_router_recv.py`
  -> `b'CLIENT' b'ping'` / `b'pong'`

검증:

```bash
./bindings/python/plan/socket/run_python_socket_surface_loop.sh --max-iterations 0
```

### Slice 6. POSD 잔여 리팩토링 루프

상태: `완료`

대상:

- `src/zlink/`
- `tests/`
- `examples/`
- 필요 시 `plan/socket/`

작업:

- 구현 완료 후 socket 계층 전체를 POSD 기준으로 다시 리뷰
- shallow wrapper, 중복 분기, 의미 없는 forwarding, public 설명 비용 증가 지점 제거
- concrete facade에 잘못 남은 정책 로직을 공통 심부 모듈로 다시 흡수
- change amplification을 만드는 import/option/callback 분산 지점 정리
- 문서와 코드 설명이 어긋나는 부분이 있으면 함께 수정
- 위 작업을 반복하고, 더 이상 정당한 POSD 리팩토링 대상이 없을 때만 종료

완료 기준:

- socket 계층에서 남은 shallow wrapper성 구조가 없다
- 새 수정 없이도 각 public concrete facade의 책임을 몇 문장으로 설명할 수 있다
- 같은 정책 변경이 여러 파일/클래스에 반복 전파되는 지점이 남아 있지 않다
- 추가 POSD 리팩토링 후보가 더 이상 식별되지 않는다

검증:

```bash
cd bindings/python && python -m pytest -q
cd bindings/python && python -m pytest -q tests/integration
```

메모:

- lifecycle/option/callback/legacy compat는 `_BaseSocket`과 의미별 facade로
  흡수했고, concrete 타입은 허용된 surface와 type별 option만 노출하도록
  유지했다.
- 추가 shallow wrapper나 중복 분기를 만들지 않고 `Socket.__new__` dispatch 한
  곳에 compat 생성을 고정했다.

## 8. 완료 판정

아래를 모두 만족해야 완료다.

- `src/zlink/`에 socket 공통 계층과 concrete facade 계층이 분리되어 있다.
- `Socket`은 compat base/entry로 남지만 concrete facade 중심 surface가 정착됐다.
- 테스트와 examples가 새 surface를 증명한다.
- 이 guide의 모든 slice 상태가 `완료`다.
- 마지막 POSD 리팩토링 루프까지 끝나서 더 이상 정당한 리팩토링 대상이 없다.
- `python -m pytest -q`가 통과한다.

## 9. 로그 / commit / push 규칙

- logs는 `plan/socket/logs/` 아래에 둔다.
- commit / push는 사용자 요청이 있을 때만 한다.
- unrelated 변경은 절대 섞지 않는다.
- 실행 중 생긴 중요한 판단은 이 guide의 slice 메모나 완료 기준에 반영한다.

## 10. 사용자 입력이 필요한 경우

아래가 아니면 사용자에게 묻지 않는다.

- 저장소 외부 API 호환 정책을 깨야만 하는 경우
- 사용자 변경과 직접 충돌하는 워크트리 변경이 있는 경우
- `bindings/python/` 범위로 해결할 수 없는 blocker

## 11. supervisor 종료 문구 규약

진짜 완료일 때만 정확히 아래 한 줄만 출력한다.

```text
미적용 사항이 없습니다.
```

사용자 결정이 필요할 때만 정확히 아래 형식 한 줄만 출력한다.

```text
사용자 입력 필요: <한 줄 이유>
```

그 외에는 정확히 아래 한 줄만 출력한다.

```text
계속 진행 필요
```
