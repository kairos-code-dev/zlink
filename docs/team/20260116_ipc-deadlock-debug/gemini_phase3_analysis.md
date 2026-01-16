# Phase 3 Strand 직렬화 결과 분석 (Gemini)

## 1. 질문 답변

**Q1. Phase 3 구현이 제안한 내용과 일치하는가?**
*   **아니요.** 핵심적인 부분은 구현되었으나, **Timer 핸들러(`add_timer`)와 Handshake 핸들러(`start_transport_handshake`)의 래핑이 누락**되었습니다.
*   `src/asio/asio_engine.cpp` 확인 결과, `async_read`/`async_write`는 `bind_executor`로 감싸져 있으나, `_timer->async_wait`와 `_transport->async_handshake`는 기본 핸들러를 그대로 사용하고 있습니다.

**Q2. 왜 성능이 악화되었는가? (70% → 60%)**
*   **Partial Strand (불완전한 직렬화)에 의한 Data Race 심화:**
    *   `on_read_complete`/`on_write_complete`는 Strand 위에서 실행되지만, `on_timer`는 Strand 밖(IO Context 스레드)에서 실행됩니다.
    *   `on_timer`가 호출하는 `restart_output` -> `speculative_write`는 `_write_pending`, `_outpos` 등의 공유 상태를 변경합니다.
    *   이때 Strand 위에서 돌고 있는 `on_write_complete`와 동시에 실행될 경우, **Lock 없는 Data Race**가 발생합니다.
    *   기존에는 "운 좋게" 순차 실행되던 것들이, Strand 도입으로 인해 스케줄링 타이밍이 변경되면서 충돌 빈도가 늘어났거나, Strand의 오버헤드(Atomic/Mutex)는 지불하면서 보호 효과는 얻지 못하는 "최악의 상태"가 되었습니다.

**Q3. Timer 핸들러와 Transport handshake 핸들러도 래핑해야 하는가?**
*   **필수(Critical)입니다.**
*   Strand 패턴의 대원칙은 **"공유 상태를 건드리는 모든 핸들러는 같은 Strand 위에 있어야 한다"**입니다. 하나라도 예외가 있으면 그곳이 Race Condition의 진원지가 됩니다.

**Q4. dispatch가 아닌 다른 방식(예: post)을 써야 하는가?**
*   `dispatch`는 현재 스레드가 Strand 내부에 있을 경우 즉시 실행(재귀 호출 위험), 아니면 post와 동일하게 동작합니다.
*   `session`이 `restart_input`을 호출하는 시점이 명확하지 않으므로, **일관성 있는 비동기 실행을 보장하기 위해 `post`를 사용하는 것이 더 안전**합니다. `restart_input`이 `true`를 반환하고 나갔을 때, 실제로 작업이 큐에 들어갔음을 보장하는 것이 로직 추적에 유리합니다.

**Q5. 아니면 Strand 접근 자체가 잘못되었는가?**
*   접근 방식은 맞습니다. ASIO에서 Thread-safety를 확보하는 표준적인 방법입니다. 다만 구현이 불완전했을 뿐입니다.

## 2. 심층 분석: 왜 60%인가?

Strand를 도입하면 안전해져야 하는데 왜 성공률이 떨어졌을까요?

1.  **경합(Contention) 증가:** `on_timer` (Heartbeat 등)가 꽤 자주 발생하는데, 이것이 Strand로 보호되지 않은 채 `speculative_write`를 시도합니다. Strand로 보호된 `async_write` 핸들러와 충돌하면 메모리 커럽션이나 로직 꼬임(둘 다 쓰기 시도 등)이 발생하여 테스트가 실패합니다.
2.  **거짓된 안도감:** `read`/`write`는 보호받지만 `timer`는 무방비 상태인 구조적 결함이 "Strand 오버헤드"와 결합되어, 처리량은 낮추고 안정성은 확보하지 못한 상태입니다.

## 3. 권장 사항

**추천 옵션: Option 1 (미구현 사항 보완) + Option 2 (post 변경) 통합 적용**

불완전한 Strand는 의미가 없습니다. 우선 **모든 비동기 핸들러를 Strand에 태우는 것**이 1순위입니다. 또한, `restart_input`의 동작 일관성을 위해 `post`로 변경하는 것을 강력히 권장합니다.

### 구체적인 Action Item

1.  **Timer 핸들러 래핑 (필수):**
    ```cpp
    // asio_engine.cpp: add_timer
    _timer->async_wait(boost::asio::bind_executor(*_strand,
        [this, id_](const boost::system::error_code &ec) {
            on_timer(id_, ec);
        }));
    ```

2.  **Handshake 핸들러 래핑 (필수):**
    ```cpp
    // asio_engine.cpp: start_transport_handshake
    _transport->async_handshake(type, boost::asio::bind_executor(*_strand,
        [this](const boost::system::error_code &ec, std::size_t) {
            on_transport_handshake(ec);
        }));
    ```

3.  **restart_input의 dispatch → post 변경 (권장):**
    *   호출자의 컨텍스트와 무관하게 항상 큐에 넣어서 실행 순서를 보장합니다.
    ```cpp
    // asio_engine.cpp: restart_input
    boost::asio::post(*_strand, [this]() { restart_input_internal(); });
    ```

이 수정을 적용한 후 **Phase 4** 테스트를 진행해야 합니다.
