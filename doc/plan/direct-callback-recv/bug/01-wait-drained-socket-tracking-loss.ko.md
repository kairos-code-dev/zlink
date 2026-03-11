# BUG-01: wait_drained() 타임아웃 시 소켓 추적 유실

상태: **수정 완료** (`73dfc80f`)

---

## 증상

- `ctest --stop-on-failure -R '^test_spot_'` split 실행에서 비결정적 60초 timeout
- 로그: `shutdown=abortive reason=110 live_slots=0 attachments=0`
- 그런데도 프로세스가 끝나지 않음 → `ctx_term()` 무기한 블로킹
- 단독 실행은 통과

---

## 원인

`service_runtime_base_t::wait_drained()`이 `_closing_sockets`를
`swap()`으로 로컬 변수로 빼간 뒤 타임아웃하면,
로컬 변수가 스코프를 벗어나면서 **소켓 포인터 추적이 유실**된다.

### 문제 코드 (수정 전)

```cpp
// service_runtime_base.hpp — wait_drained()
while (true) {
    std::map<int, const socket_base_t *> sockets;
    {
        scoped_lock_t lock (_sync);
        sockets.swap (_closing_sockets);   // ← _closing_sockets 비움
    }
    // ...
    for (...) {
        if (_ctx->wait_for_socket_removal (it->second, timeout) != 0)
            return -1;  // ← sockets 로컬 변수 drop → 추적 유실!
    }
}
```

### 유실 이후의 연쇄 실패

1. `wait_drained()` 타임아웃 → `_closing_sockets` 이미 swap되어 비어있음
2. `spot_node_t::destroy()` abortive 경로 진입
3. `abortive_stop()` 실행 → runtime 멤버 포인터 전부 NULL (이미 cleanup됨) → **no-op**
4. `force_wait_remaining()` 실행 → `_closing_sockets` 비어있음 → **no-op**
5. `destroy()` 반환 → 소켓은 여전히 `ctx_t::_sockets`에 남아있음
6. `ctx_term()` → `_term_mailbox.recv(&cmd, -1)` → **무기한 블로킹**

### 로그와의 일치

- `live_slots=0 attachments=0`: runtime 포인터/attachment만 0이라는 뜻
- context 수준의 실제 소켓 제거 완료가 아님
- abortive 로그가 찍힌 뒤에도 프로세스가 종료되지 않는 이유

---

## 수정 내용

### service_runtime_base.hpp

- `wait_drained()`: swap 대신 copy 사용, 제거 확인된 소켓만 개별 erase
- `close_socket_and_wait()`: timeout 시에도 tracking 유지
- `force_wait_remaining()`: 동일 원칙 적용
- `erase_closing_socket()` private helper 추가

### spot_node.cpp

- abortive 경로에서 `tracked=` 추가 로그
- 미수렴 시 에러 정보 유지 (기존에는 `first_error = 0`으로 리셋)

### 검증

- `unittest_service_runtime_base`: timeout 후 tracking 보존 회귀 테스트 추가
- `ctest -R '^test_spot_'` 1회 통과 확인
- stress 반복 중 `tracked=3` 로그 확인 → 추적 보존 동작 확인

---

## 관련 파일

| 파일 | 변경 |
|------|------|
| `core/src/services/common/service_runtime_base.hpp` | swap → copy + 개별 erase |
| `core/src/services/spot/spot_node.cpp` | abortive 로그 개선 |
| `core/tests/unittest_service_runtime_base.cpp` | 회귀 테스트 추가 |
