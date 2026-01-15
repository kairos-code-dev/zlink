# ASIO poller handler 재사용 패턴 리뷰

## 주요 이슈

### 중간 위험 (빌드/호환성)
- `src/asio/asio_poller.cpp:44-45` 및 `src/asio/asio_poller.hpp:93-95`
  - `read_handler {poller_, this}` / `write_handler {poller_, this}`는 C++11 brace-init에 의존합니다. 본 프로젝트는 C++03 모드 호환을 유지하려는 경향이 있어, 일부 빌드 설정에서 컴파일 실패 위험이 있습니다.
  - 대응: C++03 방식으로 멤버를 설정하거나, `read_handler_t`/`write_handler_t`에 명시적 생성자를 추가하고 `read_handler (poller_, this)` 형태로 초기화하는 편이 안전합니다.

### 낮은 위험 (향후 유지보수)
- `src/asio/asio_poller.cpp:188-234`
  - `operation_aborted` 경로가 즉시 `return`되면서 재등록을 하지 않습니다. 현재는 `cancel_ops()`가 `rm_fd()`에서만 사용된다는 전제라 문제 없지만, 향후 다른 경로(예: 일시적 비활성화)에서 `cancel_ops()`가 호출되면 `pollin_enabled/pollout_enabled`가 `true`인 상태로 이벤트 재등록이 끊길 수 있습니다.
  - 대응: `operation_aborted` 처리에서 `entry->fd != retired_fd && entry->pollin_enabled`(또는 pollout)인 경우 재등록 여부를 명확히 하는 주석/가드가 있으면 안전합니다.

## 정확성/메모리 안전성
- 재사용 handler가 `poll_entry_t`에 소유되는 방식은 `in_event_pending/out_event_pending` 플래그와 `cancel_ops()` 조합으로 dangling 호출을 피하도록 설계되어 있어 전반적으로 안전해 보입니다.
- 다만 위의 `operation_aborted` 처리 가정이 깨질 경우 이벤트가 영구적으로 멈출 수 있어, 가정에 대한 주석 보강이 바람직합니다.

## ASIO Best Practices
- `async_wait`에 재사용 handler를 전달하는 방식은 할당을 줄이는 데 적합합니다.
- `io_context`는 단일 스레드에서 실행된다는 전제라면 현재 패턴은 무리가 없습니다. 만약 다른 컴포넌트가 동일 `io_context`를 별도 스레드에서 돌릴 가능성이 있다면, 향후 `strand` 적용 여부를 검토하는 것이 좋습니다.

## 성능 개선 효과
- lambda 생성 제거로 인해 핫패스에서의 할당/복사 비용을 줄인 것은 타당합니다. 특히 빈번한 재등록이 발생하는 read/write loop에서 효과가 기대됩니다.
- `_io_context.poll()` 배치 최적화와 시너지 가능성이 높아 보입니다.

## 에러 처리 완전성
- `operation_aborted`를 명시적으로 처리하는 것은 개선입니다.
- 다른 에러코드에 대한 로깅 또는 통계가 없으므로, 디버그 모드에서만 가시성이 확보됩니다(현 상태 유지 가능).

## 가독성/유지보수성
- handler 구조체 분리가 명확하고 반복 코드가 줄었습니다.
- 다만 위 C++03 호환성 문제는 유지보수 리스크로 남습니다.

## 결론
- 기능적 변경은 전반적으로 안정적이고 성능 측면의 이점이 기대됩니다.
- 빌드 표준 호환성(C++03)과 `operation_aborted` 경로의 전제 조건을 보완하면 더 안전합니다.
