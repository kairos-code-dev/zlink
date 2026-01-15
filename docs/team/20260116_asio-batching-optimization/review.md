# ASIO 배칭 최적화 리뷰

## 주요 이슈

### 중간 위험 (정확성/행동 변화)
- `src/asio/asio_poller.cpp:185-236`, `src/asio/asio_poller.cpp:246-292`
  - `has_pending_data()`/`has_pending_write_space()`는 `POLLIN`/`POLLOUT`만 체크합니다. `async_wait`가 `POLLERR`/`POLLHUP`로 깨어난 경우에도 첫 `in_event()`/`out_event()`는 실행되지만, 이후 배칭 루프는 즉시 종료됩니다. 이 동작이 문제는 아닐 수 있으나, 에러/행업 처리도 배칭에서 기대하는 경우(예: 에러 전파를 여러 번 해야 하는 경우)가 있다면 이벤트 처리 횟수가 감소할 수 있습니다.
  - 대응: 배칭 루프 조건에 `POLLERR`/`POLLHUP`를 포함할지, 혹은 “에러/행업은 1회 처리만”이라는 의도를 주석으로 명시하는 것이 안전합니다.

### 낮은 위험 (성능/공정성)
- `src/asio/asio_poller.cpp:185-236`, `src/asio/asio_poller.cpp:246-292`, `src/asio/asio_poller.cpp:441-485`
  - 배칭 루프가 최대 64회까지 연속 처리되므로, 단일 FD가 과도하게 hot한 경우 다른 FD의 이벤트 처리 지연(공정성 저하)이 발생할 수 있습니다. 특히 `in_event()`/`out_event()`가 내부에서 다량의 work를 수행하면 지연이 커질 수 있습니다.
  - 대응: `max_batch`를 런타임 튜너(환경변수/옵션)로 내리거나, 상한을 낮추고 실제 워크로드에서 A/B 확인하는 방안을 권장합니다.

## 배칭 로직 검토
- Unix 경로의 `start_wait_read()`/`start_wait_write()`는 첫 이벤트 처리 후 `poll()`로 추가 준비 상태를 확인해 배칭합니다. `pollin_enabled/pollout_enabled`, `retired_fd`, `_stopping` 체크가 반복 루프에 포함되어 안전장치가 있습니다.
- Windows 경로는 `WSAPoll()` 이후 `FIONREAD`/`select`로 추가 준비 상태를 확인하여 배칭합니다. 구조상 논리 일관성은 유지됩니다.

## has_pending_data() 구현 적절성
- Unix: `poll(fd, 1, 0)`은 non-blocking readiness 체크로 적절합니다. 다만 배칭 루프마다 syscall이 들어가므로 메시지 단위로 호출될 때 오버헤드가 커질 수 있습니다.
- Windows: `ioctlsocket(FIONREAD)`/`select()` 조합은 일반적인 readiness 체크 방식입니다. 다만 `select()`는 내부적으로 비용이 커질 수 있고, 작은 배치 단위일수록 오버헤드가 체감될 수 있습니다.

## 성능 개선 미진 원인 분석
- 추가 syscall 오버헤드: 배칭 루프마다 `poll()`/`ioctlsocket()`/`select()`가 호출됩니다. 실제 워크로드에서는 메시지 처리 비용보다 readiness 체크 오버헤드가 더 커져 전체 이득이 상쇄될 가능성이 큽니다.
- `in_event()`/`out_event()`의 내부 동작: 기존에도 내부에서 “가능한 만큼 읽기/쓰기”를 수행하고 있다면, 외부 배칭은 실효가 낮고 오히려 중복 체크가 됩니다. 이 경우 micro-benchmark(작은 고정 payload)에서는 개선되지만, 실제 workload에서는 효과가 줄어들 수 있습니다.
- 배칭 상한/워크로드 미스매치: 실제 벤치마크 패턴이 한 FD에 충분히 연속된 이벤트를 만들지 못하면 배칭이 거의 발생하지 않습니다. `max_batch=64`가 높아도 관측되는 배치 길이가 작다면 성능 향상이 작습니다.

## 결론
- 구현은 전반적으로 일관되며 큰 구조적 오류는 보이지 않습니다. 다만 readiness 체크 오버헤드와 공정성 측면에서 실제 성능 이득이 제한될 수 있습니다.
- 성능 미개선 원인을 확인하려면 배치 길이 분포(평균/최대)와 `has_pending_*` 호출 횟수 계측이 유효합니다.
