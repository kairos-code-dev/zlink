# 문서 리뷰: `docs/team/20260116_full-asio-integration/plan.md`

이 계획 문서는 ZMQ의 핵심 시그널링 및 폴링 메커니즘을 ASIO 네이티브 패턴으로 전환하는 매우 중요한 아키텍처 변경을 담고 있습니다. 전반적으로 방향성은 현대적인 C++ 비동기 모델에 부합하며, 성능과 유지보수성 측면에서 긍정적입니다.

다음은 요청하신 6가지 관점에 대한 상세 리뷰입니다.

## 1. Mailbox io_context 전환 방식
**평가: 주의 필요 (Concurrency Race)**

*   **Atomic Flag 로직의 위험성**: 계획에 언급된 "process_mailbox() 끝에서 `scheduled.store(false)` 후, 큐가 남아있으면 다시 post" 로직은 **Lost Wakeup(깨우기 누락)** 버그 발생 가능성이 있습니다.
    *   *시나리오*: `process_mailbox`가 큐를 비움 -> `scheduled`를 false로 변경 -> (이 순간 다른 스레드가 push하고 `scheduled`가 false이므로 post 시도하려 함? 혹은 push가 `scheduled` 확인 전) -> 큐 검사.
    *   *권장*: Double-Check 패턴이 필요합니다.
        ```cpp
        // Push 측
        if (!scheduled.exchange(true)) io_context.post(process_mailbox);

        // Process 측
        do {
            drain_queue();
            scheduled.store(false);
            // 메모리 장벽(Memory Barrier) 필요
            if (queue.empty()) break; // 비어있으면 종료
            // 비어있지 않다면 다시 점유 시도
            if (scheduled.exchange(true)) break; // 이미 다른 누군가가 점유했으면 종료
            // 점유 성공 시 루프 계속
        } while (true);
        ```
*   **Shutdown 시 Drain**: `io_context`가 멈추거나 파괴될 때, 큐에 남아있는 Command(`zmq::msg_t` 포함 가능)가 소멸되지 않으면 메모리 누수가 발생합니다. `mailbox_t` 소멸자에서 반드시 남은 큐를 비우는 로직이 구현되어야 합니다.

## 2. io_thread/reaper 변경
**평가: 긍정적이나 구현 순서 주의**

*   **Poller 등록 제거**: 올바른 방향입니다. `io_thread`가 순수하게 `io_context::run()` 래퍼가 되면서 구조가 단순해집니다.
*   **Shutdown 순서**: 기존에는 `signaler`를 통해 `stop` 명령을 보내고 폴러 루프를 깨웠습니다. 변경 후에는 `mailbox.send(stop_cmd)` -> `post` -> `process_stop` 흐름이 됩니다. 이 과정이 `io_context`가 파괴되기 **전**에 확실히 완료되어야 합니다. `work_guard` 사용 여부를 명확히 해야 합니다.

## 3. Poller 인터페이스 및 IPC 지원 문제 (Critical)
**평가: 심각한 누락 가능성 있음**

*   **IPC (Unix Domain Socket) 지원 불투명**: 계획에서 "stream_descriptor 제거", "TCP 소켓만 처리"라고 명시되어 있습니다.
    *   ZMQ의 `ipc://` 트랜스포트는 Unix에서 `stream_descriptor`(AF_UNIX)를 사용합니다.
    *   이를 제거하고 `tcp::socket`만 남길 경우, **IPC 지원이 중단되는지** 혹은 `asio::local::stream_protocol::socket`으로 대체하는지 명시되어 있지 않습니다.
    *   *권장*: IPC 지원이 필요하다면 `asio_poller`가 `tcp::socket` 외에 `local::stream_protocol::socket`도 처리할 수 있도록 템플릿화하거나 다형성을 유지해야 합니다.
*   **Timer 처리**: `poller`에서 타이머 로직이 제거되면, ZMQ 내부의 타이머(`zmq::timers_t`)를 어떻게 구동할지 계획이 필요합니다. `io_context::steady_timer`로 대체하는 것이 가장 이상적입니다.

## 4. 성능 영향
**평가: 긍정적**

*   **시스템 콜 감소**: `eventfd`/`socketpair`의 `write`/`read` 시스템 콜이 제거되고, 유저 모드 락(mutex/futex)으로 대체되므로 레이턴시가 개선될 것입니다.
*   **Batching 효과**: `drain_queue()` 루프를 돌면서 한 번의 `post` 실행으로 여러 커맨드를 처리할 수 있어 처리량(Throughput)이 향상될 것으로 기대됩니다.

## 5. 구현 순서
**평가: 의존성 관리 필요**

*   **단계 1(Mailbox)과 단계 2(io_thread)의 결합**: 단계 1에서 `mailbox`가 `post` 방식으로 바뀌면, 기존 `io_thread`는 `get_fd()`를 호출할 수 없어 컴파일이 깨지거나 런타임 에러가 발생합니다.
    *   *제안*: 단계 1, 2를 하나의 PR로 진행하거나, 과도기적으로 `mailbox`가 더미 FD를 반환하도록 하여 호환성을 유지해야 합니다.

## 6. 누락된 부분 및 추가 고려사항

*   **에러 처리**: `io_context::post`는 일반적으로 예외를 던지지 않으나, OOM 등의 상황이나 `io_context`가 중단된 상태에서의 동작을 정의해야 합니다.
*   **Windows IOCP 호환성**: `WSAPoll` 제거는 환영할 일이나, Windows에서 `tcp::socket` 사용 시 IOCP 백엔드가 자동으로 사용됩니다. 이때 `cancel()` 동작이나 비동기 완료 시점이 WSAPoll(Select 기반)과 다를 수 있으므로 꼼꼼한 테스트가 필요합니다.

---

## 결론 및 수정 제안

이 계획은 승인 가능하나, **IPC 지원 여부**와 **Mailbox 스케줄링의 Race Condition**에 대한 보완이 반드시 필요합니다.

**수정 제안 사항:**
1.  **IPC 명확화**: `ipc://` 지원 유지 여부를 명시하고, 유지 시 `asio::local::stream_protocol` 통합 계획 추가.
2.  **스케줄링 로직 구체화**: Atomic flag 처리 로직의 Pseudo-code를 문서에 포함하여 Race Condition 방지.
3.  **단계 통합**: 단계 1, 2를 "Core Signaling Refactoring"으로 묶어서 진행.
