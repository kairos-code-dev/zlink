# Spot Shutdown / Teardown 이슈 검토 답변서

기준 커밋: `17e69e79` (`refactor: checkpoint direct callback lifecycle work`)

---

## 1. 현재 증상의 Root Cause 가설 (3개)

### 가설 A: `wait_drained()` 타임아웃 시 소켓 추적 유실 (Critical)

`service_runtime_base_t::wait_drained()`가 타임아웃되면
내부적으로 `_closing_sockets`를 swap한 로컬 변수가 스코프를 벗어나면서
소켓 포인터 추적이 완전히 유실된다.
이후 abortive fallback 경로가 실행되더라도, 추적을 잃은 소켓에 대해
`force_wait_remaining()`이 아무런 대기를 수행하지 않는다.

결과적으로 `spot_node_destroy()`는 반환하지만,
`ctx_t::_sockets`에 아직 reaper가 처리 중인 소켓이 남아 있고,
`ctx_term()`이 `_term_mailbox.recv(&cmd, -1)`에서 무기한 블로킹된다.

### 가설 B: TLS 소켓 reaper 처리 지연 (High)

`linger=0`이더라도 TLS 소켓의 세션 종료(close_notify 교환)는
I/O 스레드의 이벤트 루프에서 비동기적으로 처리된다.
양쪽 피어가 동시에 종료될 때(테스트 teardown),
close_notify 교환이 지연되거나 상대 피어의 소켓이 이미 닫혀
응답이 오지 않는 상황이 발생할 수 있다.

이 지연이 `wait_for_socket_removal()`의 직렬 폴링 시간 예산을 소진시키고,
결국 `wait_drained()`가 타임아웃되어 가설 A의 버그를 트리거한다.

### 가설 C: 순차 테스트 간 OS 자원 잔류 (Medium)

각 테스트가 별도의 `ctx`를 생성하지만, OS 수준에서는:
- TCP TIME_WAIT 상태의 이전 연결이 60-120초간 잔류
- TLS 라이브러리의 글로벌 세션 캐시
- 이전 테스트의 `ctx_term()`이 느리게 완료되면서 포트/fd가 아직 해제 안 됨

이것이 `create_started_registry_with_port_seed()`의 NULL 반환이나
특정 테스트 순서에서만 나타나는 비결정적 실패를 설명한다.

---

## 2. 각 가설의 근거

### 가설 A 근거: 코드 추적

`service_runtime_base_t::wait_drained()` (service_runtime_base.hpp:82-138):

```
while (true) {
    std::map<int, const socket_base_t *> sockets;
    {
        scoped_lock_t lock (_sync);
        sockets.swap (_closing_sockets);   // ← _closing_sockets 비움
    }
    // ...
    for (...) {
        if (_ctx->wait_for_socket_removal (it->second, socket_timeout_ms) != 0)
            return -1;  // ← sockets 로컬 변수가 drop됨. 추적 유실!
    }
}
```

이후 `spot_node_t::destroy()`의 abortive 경로:

```
// spot_node.cpp:1782-1798
if (_runtime && (first_error != 0 || ...)) {
    _runtime->abortive_stop ();                      // (1) runtime 멤버 포인터 전부 NULL
    _lifecycle.force_wait_remaining (5000);           // (2) _closing_sockets 이미 비어있음!
    wait_owned_socket_removals (5000);                // (3) 역시 비어있음
    first_error = 0;                                 // (4) 에러 리셋
}
```

`abortive_stop()`이 읽는 runtime 멤버 포인터들:
- `data_ctrl_front` → `close_control_sockets()`에서 이미 NULL
- `data_ctrl_back`, `mesh_pub`, ... → 데이터 플레인 스레드 cleanup에서 이미 NULL
- `attachments` → `destroy_handles()`에서 이미 clear

결과: **abortive 경로가 사실상 no-op**이다.

### 가설 A 추가 근거: 관찰된 증상과 정확히 일치

1. `shutdown=abortive reason=110` 로그가 찍힘 → `wait_drained()`가 ETIMEDOUT 반환
2. 로그 이후에도 프로세스가 종료되지 않음 → `ctx_term()` 무기한 블로킹
3. `live_slots=0 attachments=0` → abortive_stop()이 할 일이 없음

### 가설 B 근거: 실패 테스트 패턴

- `test_spot_tls_settings_lock_after_bind_connect_and_register`에서만 60초 타임아웃
- 이 테스트는 TLS bind/connect를 사용하는 유일한 introspection 테스트
- 단독 실행에서는 통과 → TLS 세션이 단독으로는 빠르게 정리되지만,
  앞선 테스트들이 reaper를 바쁘게 만들면 지연됨

테스트의 teardown 순서:

```
zlink_spot_node_destroy(&reg_node);     // TCP only - 빠름
zlink_spot_node_destroy(&client_node);  // TLS client mesh_xsub 닫힘
                                        //   → close_notify 전송
                                        //   → server는 아직 살아있으므로 응답 가능
zlink_spot_node_destroy(&server_node);  // TLS server mesh_pub 닫힘
                                        //   → client 이미 닫혔으므로 피어 없음
```

문제 시나리오: client_node destroy에서 `wait_drained(10000)`이
TLS 세션 종료 지연으로 타임아웃 → 가설 A의 소켓 추적 유실 트리거.

### 가설 C 근거: 순차 실행 전용 실패

- `test_spot_topology_summary_lifecycle`에서
  `create_started_registry_with_port_seed()`가 NULL 반환
- 이 테스트의 `registry_seed=22670`은 다른 테스트와 겹치지 않지만,
  32번 시도 모두 EADDRINUSE로 실패할 수 있음
- 앞선 테스트의 TCP 연결이 TIME_WAIT에 있으면 포트 재사용 불가

---

## 3. 가장 가능성 높은 1순위 원인

**가설 A: `wait_drained()` 타임아웃 시 소켓 추적 유실**

이것이 1순위인 이유:

1. **재현 조건과 완벽히 일치**: TLS 소켓이 `wait_drained()` 타임아웃을 유발하면,
   abortive 경로가 no-op이 되어 `ctx_term()`이 영구 블로킹

2. **단독 실행 통과 설명**: 단독 실행에서는 시스템 부하가 낮아
   `wait_drained()` 10초 내에 완료됨. 순차 실행에서는
   reaper가 이전 테스트의 소켓을 아직 처리 중이라 지연

3. **코드에서 명확히 확인 가능**: `wait_drained()` return -1 시점에
   `_closing_sockets`가 이미 swap되어 비어있고,
   로컬 `sockets` 변수가 스코프를 벗어나면서 추적 유실

4. **abortive_stop()이 독립적으로 무력함**: runtime 멤버 포인터가
   이미 NULL이므로 `_lifecycle` 추적과 무관하게 할 일이 없음

가설 B(TLS 지연)는 가설 A를 트리거하는 **원인** 역할이고,
가설 C(포트 잔류)는 별개의 isolation 문제다.
하지만 핵심 blocker는 가설 A의 추적 유실이다.

---

## 4. 구조적으로 맞는 해결책

### 4.1 `wait_drained()` 소켓 추적 보존

`wait_drained()`가 타임아웃 시 미처리 소켓을 `_closing_sockets`에 되돌려야 한다.
이렇게 하면 이후 `force_wait_remaining()`이 해당 소켓을 찾아 대기할 수 있다.

### 4.2 `ctx_t::terminate()`에 bounded fallback 도입

현재 `ctx_t::terminate()`는 `_term_mailbox.recv(&cmd, -1)`로 무기한 대기한다.
service 레벨에서 아무리 잘 정리해도, 최종 방어선인 ctx가 무기한이면
하나의 느린 소켓이 전체 프로세스를 멈출 수 있다.

옵션:
- `_term_mailbox.recv(&cmd, bounded_timeout)` + 재시도 + 최종 강제 종료
- 또는 `ctx_shutdown()` 이후 bounded wait 정책

### 4.3 `abortive_stop()` 경로에서 `_lifecycle` 추적 활용

현재 `abortive_stop()`는 runtime 멤버 포인터만 읽는데,
이미 NULL이므로 무력하다.
대신 `_lifecycle._closing_sockets`에 남아있는 소켓을 읽어서
force-wait하도록 변경해야 한다.

---

## 5. 지금 코드에 바로 적용 가능한 최소 수정안

### 수정 1: `wait_drained()` 타임아웃 시 소켓 복원 (Critical Fix)

파일: `core/src/services/common/service_runtime_base.hpp`

```cpp
int wait_drained (int timeout_ms_)
{
    if (!_ctx)
        return 0;

    const uint64_t deadline_ms =
      timeout_ms_ >= 0 ? zlink::clock_t ().now_ms () + timeout_ms_ : 0;

    while (true) {
        std::map<int, const socket_base_t *> sockets;
        size_t owned_count = 0;
        {
            scoped_lock_t lock (_sync);
            owned_count = _owned_sockets.size ();
            sockets.swap (_closing_sockets);
        }

        if (owned_count == 0 && sockets.empty ())
            return 0;

        int remaining_ms = -1;
        if (timeout_ms_ >= 0) {
            const uint64_t now_ms = zlink::clock_t ().now_ms ();
            if (now_ms >= deadline_ms) {
                // ★ FIX: 타임아웃 시 미처리 소켓을 되돌림
                restore_closing_sockets (sockets);
                errno = ETIMEDOUT;
                return -1;
            }
            remaining_ms = static_cast<int> (deadline_ms - now_ms);
        }

        if (sockets.empty ()) {
            usleep (1000);
            continue;
        }

        for (std::map<int, const socket_base_t *>::const_iterator it =
               sockets.begin ();
             it != sockets.end (); ++it) {
            int socket_timeout_ms = remaining_ms;
            if (_ctx->wait_for_socket_removal (it->second, socket_timeout_ms)
                != 0) {
                // ★ FIX: 현재 소켓 이후의 나머지를 되돌림
                std::map<int, const socket_base_t *> remaining;
                for (++it; it != sockets.end (); ++it)
                    remaining[it->first] = it->second;
                restore_closing_sockets (remaining);
                return -1;
            }
            if (timeout_ms_ >= 0) {
                const uint64_t now_ms = zlink::clock_t ().now_ms ();
                if (now_ms >= deadline_ms) {
                    // ★ FIX: 데드라인 도달 시 나머지를 되돌림
                    std::map<int, const socket_base_t *> remaining;
                    for (++it; it != sockets.end (); ++it)
                        remaining[it->first] = it->second;
                    restore_closing_sockets (remaining);
                    errno = ETIMEDOUT;
                    return -1;
                }
                remaining_ms = static_cast<int> (deadline_ms - now_ms);
            }
        }
    }
}

private:
void restore_closing_sockets (
  const std::map<int, const socket_base_t *> &sockets_)
{
    if (sockets_.empty ())
        return;
    scoped_lock_t lock (_sync);
    for (std::map<int, const socket_base_t *>::const_iterator it =
           sockets_.begin ();
         it != sockets_.end (); ++it) {
        _closing_sockets[it->first] = it->second;
    }
}
```

이 수정만으로도 abortive 경로의 `force_wait_remaining()`이
타임아웃된 소켓을 다시 찾아서 대기할 수 있다.

### 수정 2: `force_wait_remaining()`도 동일하게 보존 (Safety)

`force_wait_remaining()`도 같은 패턴의 swap을 사용하므로,
타임아웃 시 소켓이 유실된다. 동일한 복원 로직을 적용해야 한다.

파일: `core/src/services/common/service_runtime_base.hpp`

`force_wait_remaining()` 내부의 각 `return -1` 경로에서도
미처리 소켓을 `_owned_sockets` 또는 `_closing_sockets`에 되돌려야 한다.

### 수정 3: `destroy_test_ctx()`의 settle 시간 증가 (Workaround)

파일: `core/tests/spot/test_spot_service_introspection.cpp`

```cpp
static void destroy_test_ctx (void *ctx_)
{
    TEST_ASSERT_NOT_NULL (ctx_);
    msleep (200);   // 50 → 200: TLS 종료에 더 많은 시간
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx_));
    msleep (50);    // 10 → 50: shutdown 후 reaper 처리 시간
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx_));
}
```

이것은 workaround이지 근본 해결이 아니다.
수정 1이 적용되면 필요 없을 수 있지만,
TLS 테스트의 안정성을 위해 적용 권장.

---

## 6. 장기적으로 더 나은 구조 개선안

### 6.1 `ctx_t::terminate()`에 bounded fallback 도입

```cpp
int zlink::ctx_t::terminate ()
{
    // ... existing shutdown logic ...

    // Wait with bounded timeout instead of infinite
    command_t cmd;
    const int bounded_timeout_ms = 30000;  // 30초
    int rc = _term_mailbox.recv (&cmd, bounded_timeout_ms);
    if (rc == -1 && errno == ETIMEDOUT) {
        // Force-stop remaining sockets in reaper
        _slot_sync.lock ();
        for (sockets_t::size_type i = 0; i < _sockets.size (); ++i) {
            if (_sockets[i])
                _sockets[i]->stop ();
        }
        _slot_sync.unlock ();
        // Retry with shorter timeout
        rc = _term_mailbox.recv (&cmd, 5000);
        if (rc == -1) {
            // Last resort: log warning, proceed with cleanup
            fprintf (stderr,
                     "[ctx] terminate: %zu sockets still pending after "
                     "bounded wait\n",
                     static_cast<size_t> (_sockets.size ()));
        }
    }
    // ...
}
```

invariant 요구사항:
- bounded fallback 이후에도 소켓 정리가 완료되지 않으면
  ctx 소멸자의 `zlink_assert (_sockets.empty ())` assertion이 실패함
- 이를 피하려면 assertion을 warning으로 변경하거나,
  강제 삭제 경로를 추가해야 함
- 프로덕션에서는 assertion 실패보다 로깅 후 진행이 더 안전

### 6.2 `service_runtime_base_t`에 소켓 상태 모델 강화

현재 모델:

```
tracked → owned → close_socket() → closing → reaped(removed from ctx)
```

개선된 모델:

```
tracked → owned → close_socket() → closing → wait_confirmed → reaped
                                     │
                                     └── timeout → force_closing (with ctx fallback)
```

구체적으로:
- `closing` 상태의 소켓은 swap으로 빼지 말고,
  상태 플래그(`waited`, `confirmed`)를 추가
- `wait_drained()`가 타임아웃되어도 소켓이 남아있는 한 추적 유지
- `force_wait_remaining()`은 `ctx->wait_for_socket_count_at_most()`처럼
  전체 카운트 기반 대기도 가능하게

### 6.3 소켓 소유권 명시적 이전 모델

현재 `abortive_stop()`은 runtime 멤버 포인터를 직접 읽는데,
이 포인터들은 데이터 플레인 스레드와 공유되어 cleanup 후 NULL이다.

개선:
- 소켓 소유권을 항상 `_lifecycle`을 통해 관리
- `abortive_stop()`도 `_lifecycle`에서 소켓 목록을 가져옴
- runtime 멤버 포인터는 "현재 활성 참조"일 뿐, 소유권 아님

```cpp
int spot_runtime_t::abortive_stop ()
{
    abortive_shutdown = true;
    stop.set (1);

    // _lifecycle에서 모든 추적 중인 소켓을 가져와 강제 종료
    // runtime 멤버 포인터 대신 _lifecycle.force_close_all() 사용
    owner->_lifecycle.force_close_all ();
    // ...
}
```

### 6.4 테스트 포트 격리 강화

```cpp
// 현재: 정적 시드 기반
int registry_seed = 22670;

// 개선: 프로세스 전역 카운터 + 랜덤 오프셋
static std::atomic<int> g_port_cursor{0};
int registry_seed = 20000 + (g_port_cursor.fetch_add(100) % 40000);
```

또는 `tcp://127.0.0.1:0`으로 바인드 후 `getsockname`으로 실제 포트를 가져오는
방식으로 전환 (가능한 경우).

---

## 7. 테스트 관점에서 꼭 보강해야 할 회귀

### 7.1 소켓 추적 유실 검증 테스트

`wait_drained()` 타임아웃 후에도 `force_wait_remaining()`이
남은 소켓을 올바르게 대기하는지 검증하는 단위 테스트:

```cpp
void test_wait_drained_timeout_preserves_tracking ()
{
    // 1. ctx 생성
    // 2. 여러 소켓 생성, lifecycle에 등록
    // 3. 소켓을 close_socket()으로 닫음 (closing 상태)
    // 4. 짧은 타임아웃으로 wait_drained() 호출 (소켓이 아직 reaper에 있음)
    // 5. wait_drained() -1 반환 확인
    // 6. force_wait_remaining() 호출 → 남은 소켓 대기
    // 7. 최종적으로 모든 소켓이 정리됨 확인
}
```

### 7.2 TLS 순차 종료 안정성 테스트

```cpp
void test_spot_tls_sequential_teardown_convergence ()
{
    // 1. TLS server node + TLS client node 생성
    // 2. 연결 확립 (데이터 교환)
    // 3. client destroy → server destroy 순서로 종료
    // 4. ctx_term()이 5초 이내 반환 확인
    // 5. 역순(server first)으로도 동일 확인
}
```

### 7.3 Abortive 경로 실효성 테스트

```cpp
void test_spot_abortive_fallback_actually_closes_sockets ()
{
    // 1. 의도적으로 느린 소켓 시뮬레이션
    //    (예: 원격 피어가 없는 TLS 소켓)
    // 2. spot_node_destroy() 호출
    // 3. abortive 로그가 찍히는지 확인
    // 4. destroy() 반환 후 ctx.socket_count()가
    //    bounded time 내에 0이 되는지 확인
}
```

### 7.4 순차 테스트 격리 검증

```cpp
void test_spot_introspection_full_sequence_twice ()
{
    // 전체 introspection 테스트 시퀀스를 2회 반복 실행
    // 1회차에서 남긴 자원이 2회차를 방해하지 않는지 확인
}
```

### 7.5 `ctx_term()` bounded convergence assertion

기존 `destroy_test_ctx()`에 타이밍 assertion 추가:

```cpp
static void destroy_test_ctx (void *ctx_)
{
    TEST_ASSERT_NOT_NULL (ctx_);
    const auto start = std::chrono::steady_clock::now ();
    msleep (200);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx_));
    msleep (50);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx_));
    const auto elapsed = std::chrono::steady_clock::now () - start;
    // ctx_term이 10초 이상 걸리면 리그레션
    TEST_ASSERT_TRUE (elapsed < std::chrono::seconds (10));
}
```

---

## 부록: 질문별 직접 답변

### Q1: `spot_node_destroy()`의 종료 계약이 올바른가?

**아니다.** 현재 구조의 핵심 결함:

1. `wait_drained()` 타임아웃 시 소켓 추적 유실
2. `abortive_stop()`이 runtime 멤버 포인터(이미 NULL)에 의존
3. `first_error = 0` 리셋으로 에러 정보 소실

`destroy()` 반환 전에 enough drain을 **보장하지 않는다.**
`ctx_term()`에 최종 강제 수렴 책임을 넘겨야 하지만,
현재 `ctx_term()`도 무기한이므로 이것도 해결이 필요하다.

### Q2: `service_runtime_base_t`의 모델이 맞는가?

방향은 맞지만 구현에 결함이 있다:

- **tracked socket 모델 자체는 충분하다** (owned → closing → removed)
- **문제: swap 기반 추적이 타임아웃 시 유실됨**
- `force_wait_remaining()` 역시 같은 문제
- closing된 소켓의 "대기 완료 여부" 상태가 없음

### Q3: Attachment/runtime ownership 모델이 race를 줄이는 방향인가?

**맞는 방향이다.** 하지만:

- attachment → runtime → lifecycle 체인은 올바름
- public handle은 attachment_id로만 소켓을 조회하므로 직접 소유 없음 ✓
- **TLS/WS peer transport teardown이 직렬화되지 않는 경로 있음:**
  - 데이터 플레인 스레드가 TLS mesh_pub/mesh_xsub를 닫을 때
    close_notify가 I/O 스레드에서 비동기 처리됨
  - `close_socket()`은 `close()` 호출 후 즉시 반환
  - TLS 세션 종료는 reaper/I/O 스레드에서 나중에 완료

### Q4: `ctx_term()`에 bounded abortive fallback이 필요한가?

**필요하다.** 이유:

1. service-level teardown이 아무리 완벽해도,
   비동기 reaper 처리가 느리면 `ctx_term()`이 블로킹
2. 현재 `ctx_term()` 무기한 대기는 테스트 환경에서 60초 ctest timeout을 유발
3. 프로덕션에서도 프로세스 종료 지연 위험

ctx가 최종 강제 종료를 맡는다면 필요한 invariant:
- bounded wait 후에도 소켓이 남으면 `_reaper->stop()` 강제 호출
- 이후 `_sockets` assertion을 warning으로 완화
- 또는 남은 소켓에 대해 `process_destroy()` 강제 실행 (위험하지만 확실)

### Q5: Test isolation 문제인지, runtime bug인지?

**둘 다이며, 경계는 다음과 같다:**

| 증상 | 분류 | 비율 |
|------|------|------|
| `wait_drained()` 소켓 추적 유실 | **runtime bug** | 주 원인 |
| TLS 세션 종료 지연 | runtime limitation | 트리거 |
| 포트 충돌로 registry NULL | **test isolation** | 별개 |
| `ctx_term()` 무기한 대기 | **runtime bug** | 최종 blocker |
| 순차 실행에서만 재현 | 트리거 조건 | N/A |

runtime bug 2개(소켓 추적 유실 + ctx 무기한 대기)를 고치면,
test isolation 문제가 남아있어도 테스트가 timeout으로 실패하지는 않는다.
isolation 문제는 별도로 포트 관리를 개선하면 된다.

---

## 요약: 권장 수정 우선순위

| 순위 | 수정 | 파일 | 영향 |
|------|------|------|------|
| 1 | `wait_drained()` 타임아웃 시 소켓 복원 | service_runtime_base.hpp | 핵심 버그 수정 |
| 2 | `force_wait_remaining()` 동일 보존 | service_runtime_base.hpp | 안전망 |
| 3 | `destroy_test_ctx()` settle 시간 증가 | test_spot_service_introspection.cpp | 즉시 workaround |
| 4 | `ctx_t::terminate()` bounded fallback | ctx.cpp | 장기 안전망 |
| 5 | 테스트 포트 격리 개선 | 테스트 파일들 | isolation |
