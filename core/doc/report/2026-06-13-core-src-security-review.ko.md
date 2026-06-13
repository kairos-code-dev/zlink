# core/src/runtime C++ 코어 보안·버그 검토 리포트

- **작성일**: 2026-06-13
- **수정일**: 2026-06-14
- **대상 범위**: `core/src/runtime/**` (libzmq 파생 메시징 런타임 "zlink")
- **검토 방식**: 5개 공격면 영역 병렬 정밀 리뷰 → 상위 발견 항목 소스 직접 대조 검증
- **상태**: 리포트 전용. **코드 수정 없음.** 2026-06-14에 최종 판정과 수정 권고를 정리함.

> 본 문서는 외부 입력이 닿는 신뢰 경계와 동시성 코어를 우선순위로 검토한 결과다.
> zlink는 libzmq를 포크해 재구조화한 코드베이스이며, **asio 기반 I/O 엔진·WebSocket·`services/` 계층은
> repo 고유**(upstream libzmq는 asio를 사용하지 않음)라 상대적으로 검증이 덜 되어 위험도가 높다.

---

## 0. 요약

최종 유효 항목은 9건이다. 초판의 #8 메일박스 동시성 의심은 실제 `_sync` 사용과 단일 워커 모델을
대조한 결과 반박되었으므로 수정 대상에서 제외한다. 원격 트리거 가능성과 영향이 가장 큰 항목은
**#1(mtrie 재귀 순회/소멸 → 스택 오버플로 DoS)** 이다.

| # | 심각도 | 분류 | 위치 | 상태 | 출처 |
|---|--------|------|------|------|------|
| 1 | **High** | DoS / 스택 오버플로 | `core/src/runtime/utils/generic_mtrie_impl.hpp`, 보조: `trie.cpp`, `radix_tree.cpp` | **수정 완료(2026-06-14)** | upstream 파생 |
| 2 | Medium | DoS / 큰 메시지 버퍼 이중 보관 | `core/src/runtime/transports/ws/ws_transport.cpp`, `core/src/runtime/transports/tls/wss_transport.cpp` | 확인 | repo 고유 |
| 3 | Medium | 입력검증 / 잘못된 포트 | `core/src/runtime/utils/ip_resolver.cpp:216,257` | **수정 완료(2026-06-14)** | upstream 파생 |
| 4 | Medium | 파일시스템 / TOCTOU | `core/src/runtime/transports/ipc/asio_ipc_listener.cpp:118` | 확인 | repo 고유 순서 |
| 5 | Medium | 정수 오버플로 | `core/src/runtime/protocol/decoder_allocators.cpp:88` | 확인, 현실 도달성 낮음 | upstream 파생 |
| 6 | Medium | API 경계 가드 누락 | `core/src/api/message/message_api.cpp`, `core/src/runtime/core/send_internal.cpp` | **수정 완료(2026-06-14)** | repo 고유 |
| 7 | Medium | DoS / 큰 단일 할당 | `core/src/runtime/protocol/zmp_decoder.cpp:139` | 확인, 기본값 정책 사안 | upstream 동작 |
| 8 | - | 동시성 / 미스 웨이크업 | `core/src/runtime/core/mailbox.cpp:121-143` | **반박됨, 수정 제외** | repo 고유 |
| 9 | Low | 방어적 길이 clamp 누락 | `core/src/runtime/core/msg.cpp:623` | ZMP 원격 도달 불가, 방어 수정 권장 | upstream 파생 |
| 10 | Low | IPC 주소 길이 방어 누락 | `core/src/runtime/transports/ipc/ipc_address.cpp:13,73` | 호출자 제공 sockaddr 경로 한정 | upstream 파생 |

### 질문에 대한 결론

수정해도 기능·성능 문제가 크지 않은 항목은 **#1, #3, #4, #6, #9, #10**이다. 이 항목들은
정상 동작의 의미를 바꾸지 않고, 잘못된 입력이나 비정상 상태를 더 일찍 실패시키는 수정이다.

주의가 필요한 항목은 **#2와 #7**이다. #2는 `pending_message` 전체 사본을 제거하는 방향이면 기능
호환성을 유지하면서 메모리와 복사 비용을 줄일 수 있다. 다만 `read_message_max` 기본값을 낮추는
변경은 큰 WS/WSS 메시지를 쓰는 사용자에게 영향을 줄 수 있으므로 별도 호환성 검토가 필요하다.
#7도 `maxmsgsize` 기본값을 바로 바꾸면 큰 메시지 사용자를 깨뜨릴 수 있다. 먼저 신뢰할 수 없는
listener에서 상한을 설정하도록 문서와 샘플을 정리하는 쪽이 안전하다.

**#8은 수정하면 안 된다.** 실제 코드 대조 결과 미스 웨이크업이나 UAF로 볼 근거가 없으므로,
수정하면 오히려 검증된 동시성 경로를 불필요하게 흔들 수 있다.

권고 처리 순서:
1. **#1 mtrie 비재귀화** — 원격 구독 경로가 닿는 mtrie 소멸자와 `visit_values`를 먼저 고친다.
2. **#6 send/message API 가드** + **#3 포트 파싱 검증** — 정상 호출 영향이 작고 오류 방어 효과가 크다.
3. **#4 IPC unlink 순서** + **#2 WS/WSS 전체 사본 제거** — 보안 영향과 메모리 방어 효과가 크다.
4. **#5/#9/#10 방어 하드닝** — 정상 입력 영향이 거의 없는 안전한 가드다.
5. **#7 `maxmsgsize` 정책** — 기본값 변경은 나중에 별도 마이그레이션 계획으로 다룬다.

### 기능·성능 영향 검토

| 항목 | 권장 수정 | 기능 영향 | 성능 영향 |
|------|-----------|-----------|-----------|
| #1 mtrie 비재귀화 | 명시적 힙 스택으로 소멸자와 `visit_values`를 반복 처리 | 구독 의미는 유지된다. 방문 순서에 의존하는 호출자가 없는지 테스트 필요 | 종료·모니터링 순회에서 힙 스택 비용이 생기지만 스택 오버플로 위험 제거가 우선이다 |
| #2 WS/WSS 버퍼 | `pending_message` 전체 사본 제거를 우선 처리. `read_message_max` 조정은 별도 검토 | 기본 상한을 낮추면 큰 WS/WSS 메시지 사용자에게 호환성 영향이 있다 | 사본 제거는 성능·메모리 모두 개선 가능. 기본 상한 변경은 메모리 방어 효과가 큼 |
| #3 포트 파싱 | `strtol`/`strtoul` + 전체 소비·범위 검사 | 잘못된 포트를 조용히 받아들이던 동작이 `EINVAL`로 바뀐다. 정상 입력 영향 없음 | 엔드포인트 파싱 시 1회 비용이라 무시 가능 |
| #4 IPC unlink 순서 | `resolve` 후 unlink, 파일 타입·소유 확인 추가 | 잘못된 IPC 경로를 먼저 삭제하던 동작이 사라진다 | bind 시 1회 비용이라 무시 가능 |
| #5 할당 산술 가드 | 곱셈·덧셈 오버플로 검사 | 정상 크기 영향 없음 | 디코더 버퍼 재할당 시 1회 분기라 영향 낮음 |
| #6 메시지 가드 | public message API와 send 내부에서 NULL/`check()` 검사 | 닫힌 메시지를 넘기던 잘못된 호출이 `EFAULT`로 실패한다 | send hot path에 분기 1개가 추가된다. branch 예측상 정상 경로 영향은 작다 |
| #7 `maxmsgsize` | 기본값 변경보다 문서·샘플·listener 설정 우선 | 기본값 변경은 큰 메시지 사용자에게 깨지는 변경이 될 수 있다 | 낮은 상한은 메모리 DoS를 줄이나 큰 메시지 워크로드를 제한한다 |
| #9/#10 clamp | 길이 계산 방어 가드 | 정상 입력 영향 없음 | 영향 없음에 가까움 |

---

## 0.5 교차검증 결과 (2026-06-13)

초판 작성 후 문서와 코드 위치를 다시 대조하면서 각 발견을 반박하는 방향으로 검토했다. 결과:
후속 교차검증도 이 문서만 읽고 판단하지 않는다. 각 항목의 현재 코드를 직접 열어 보고, 라인 번호와
호출 경로가 지금 체크아웃과 맞는지 확인한 뒤 판정해야 한다. 문서에 적힌 결론과 코드가 다르면
코드를 기준으로 문서를 고친다.

| # | 초판 | 교차검증 판정 | 정정 요지 |
|---|------|-----------|-----------|
| 1 | High 확인 | **NEEDS-NUANCE** | 재귀는 사실이나 **trie/radix는 원격 도달 미입증** — XSUB 구독은 로컬 `xsub.cpp:252` `xsend`가 공급. **원격 SUB 바이트는 `xpub.cpp:119` → mtrie.add()** 로 가며, **mtrie `rm()`은 비재귀지만 소멸자(`generic_mtrie_impl.hpp:25`)·`visit_values`는 여전히 재귀** → 진짜 원격 DoS 경로는 여기. 수정 대상을 mtrie 소멸자/visit로 재조준. |
| 2 | Med 확인 | **CONFIRMED** | `ws_transport.cpp:39-44` 64MiB 기본, `:190-204` 전체 사본 확인 |
| 3 | Med 확인 | **CONFIRMED** | `ip_resolver.cpp:216,257`(`:257`=zone_id atoi; Codex의 `:254`는 isalpha 분기 오기) atoi 래핑 + `ws_address.cpp:87` strtol 불일치 확인 |
| 4 | Med 확인 | **CONFIRMED** | `asio_ipc_listener.cpp:118` unlink 선행 확인 |
| 5 | Med 확인 | **CONFIRMED** | `decoder_allocators.cpp:86-91` 곱셈 미검사 확인(64비트 원격 도달 미입증) |
| 6 | Med 확인 | **NEEDS-NUANCE** | **recv 측 반박** — `socket_base_msg.cpp:309-312`가 `!msg_ \|\| !msg_->check()` 수행. **send 측만 유효**(`send_internal.cpp:20` → `message_api.cpp:77` `size()`가 check 없이 호출) |
| 7 | Med 확인 | **CONFIRMED** | `options.cpp:114` maxmsgsize(-1) → `zmp_decoder.cpp:11` 4GiB 확인 |
| 8 | High 의심 | **반박됨** | `mailbox.cpp:39-56`의 write/flush와 `:131-136`의 check_read가 **모두 `_sync` 안**. `flush()`는 `_c` sleep(NULL) 시에만 false 반환→`schedule_if_needed` 트리거(`ypipe.hpp:76-91`). poller는 단일 워커(`asio_poller.cpp:421`, `poller_base.cpp:102`) → lost-wakeup/이중소비/UAF 없음 |
| 9 | Med 의심 | **NEEDS-NUANCE** | `zmp_decoder.cpp:82-92`가 subscribe/cancel + 다른 플래그 조합을 거부 → control 플래그 동반 불가 → **원격 도달 불가**, 방어적 clamp만 권장 |
| 10 | Med 의심 | **NEEDS-NUANCE** | 기본 ctor는 `sun_family==0`이라 `:55-58`에서 조기 반환. 언더플로는 **호출자가 `sa_len_`을 제공하는 ctor(`ipc_address.cpp:18-24`)** 경로로만 도달 가능 |

**추가 확인(초판 누락)**: `core/src/api/message/message_api.cpp:11-80` — `init/init_size`(`:11`)가 `msg_` NULL 검사 없이 역참조, `close`(`:36`)·`data()/size()`(`:72`)도 NULL/`check()` 가드 부재. **Low~Medium API 하드닝**, #6의 send 측 우려를 보강한다.

**교차검증 총평**: 구체적 메모리/할당/입력검증 발견(#2~#5,#7)은 정확. 다만 **#1의 trie/radix 원격 도달성 과장**, **#6 recv 과장**, **#8은 실제 `_sync` 사용·단일 워커 모델 앞에서 반박**. #9는 실제 코드 스멜이나 인용된 ZMP 경로로는 원격 도달 미입증.

### 교차검증 당시 정리한 우선순위

1. **#1(재조준)** mtrie 소멸자·`visit_values` 비재귀화 — 원격 SUB 바이트가 닿는 유일한 재귀 경로. trie/radix 비재귀화는 방어적(로컬 트리거).
2. **#3 strtol** + **#4 IPC unlink 순서** + **#2 WS 64MiB** — 확정된 실질 영향.
3. **#6(send 한정) + 추가 확인** `message_api`/`send_internal` 경계 `check()`·NULL 가드.
4. #5/#7 하드닝(오버플로 가드, maxmsgsize 기본/문서화).
5. #9/#10 방어적 clamp. **#8은 종결(no-op).**

현재 작업 순서는 위 기록을 바탕으로 §0의 "질문에 대한 결론"에 다시 정리했다.

---

## 1. [High · DoS] 신뢰할 수 없는 구독 프리픽스로 mtrie 재귀 소멸/순회 → 스택 오버플로

- **분류**: DoS / 스택 오버플로 (메모리 안전)
- **위치**:
  - `core/src/runtime/utils/generic_mtrie_impl.hpp:25-38` (`~generic_mtrie_t`)
  - `core/src/runtime/utils/generic_mtrie_impl.hpp:526-547` (`visit_values`)
  - `core/src/runtime/utils/trie.cpp:17-28` (`~trie_t`)
  - `core/src/runtime/utils/trie.cpp:249-284` (`apply_helper`)
  - `core/src/runtime/utils/radix_tree.cpp:209` (`free_nodes`)
  - `core/src/runtime/utils/radix_tree.cpp:549-567` (`visit_keys`)
- **출처**: upstream(libzmq) 파생 — libzmq에도 동일 결함 존재
- **상태**: ✅ 2026-06-14 수정 완료 / ✅ 원격 구독 경로는 mtrie로 재조준됨

trie/radix는 재귀 코드가 남아 있지만, 원격 SUB 바이트가 직접 닿는 경로로 확인되지는 않았다.
XSUB 구독은 로컬 `xsub.cpp:252`의 `xsend`가 공급한다. 반면 원격 SUB 바이트는
`xpub.cpp:119`에서 구독 명령으로 해석되어 `mtrie.add()`로 들어간다. `mtrie::rm`은 이미
반복 처리로 바뀌었지만, mtrie 소멸자와 `visit_values`는 여전히 재귀를 사용한다.
따라서 1순위 수정 대상은 **mtrie 소멸자와 `visit_values`**다. trie/radix 비재귀화는
로컬 트리거까지 줄이는 방어적 후속 작업으로 분리한다.

### 코드

```cpp
// utils/generic_mtrie_impl.hpp:25
template <typename T> generic_mtrie_t<T>::~generic_mtrie_t ()
{
    LIBZLINK_DELETE (_pipes);

    if (_count == 1) {
        zlink_assert (_next.node);
        LIBZLINK_DELETE (_next.node);          // 프리픽스 깊이만큼 재귀
    } else if (_count > 1) {
        for (unsigned short i = 0; i != _count; ++i) {
            LIBZLINK_DELETE (_next.table[i]);  // 재귀
        }
        free (_next.table);
    }
}
```

```cpp
// utils/generic_mtrie_impl.hpp:526
void generic_mtrie_t<T>::visit_values (void (*func_) (value_t *value_, Arg arg_), Arg arg_) const
{
    if (_pipes) {
        for (typename pipes_t::const_iterator it = _pipes->begin (), end = _pipes->end ();
             it != end; ++it)
            func_ (*it, arg_);
    }

    if (_count == 1) {
        if (_next.node)
            _next.node->visit_values (func_, arg_);  // 프리픽스 깊이만큼 재귀
        return;
    }

    for (unsigned short i = 0; i != _count; ++i) {
        if (_next.table[i])
            _next.table[i]->visit_values (func_, arg_);  // 재귀
    }
}
```

trie/radix에도 같은 형태의 재귀가 남아 있다. 이 둘은 현재 확인된 원격 1순위 경로가 아니므로
같은 패치에 섞기보다 별도 하드닝으로 처리하는 편이 검증 범위를 줄인다.

```cpp
// utils/trie.cpp:17
zlink::trie_t::~trie_t ()
{
    if (_count == 1) {
        zlink_assert (_next.node);
        LIBZLINK_DELETE (_next.node);          // 프리픽스 깊이만큼 재귀
    } else if (_count > 1) {
        for (unsigned short i = 0; i != _count; ++i)
            LIBZLINK_DELETE (_next.table[i]);  // 재귀
        free (_next.table);
    }
}
```

```cpp
// utils/radix_tree.cpp:209
static void free_nodes (node_t node_)
{
    for (size_t i = 0, count = node_.edgecount (); i < count; ++i)
        free_nodes (node_.node_at (i));        // ← 키 길이만큼 재귀
    free (node_._data);
}
```

### 트리거

악의적 SUB 피어가 중첩 프리픽스 구독을 다수 등록한다:

```
"a", "ab", "abc", "abcd", … (또는 "a"×N 형태의 깊은 단일 체인)
```

단일-엣지 노드 체인이 N 깊이로 쌓인다. 이후 다음 시점에 N 프레임 재귀가 발생한다:

- XPUB 소켓/컨텍스트 종료 시 `~generic_mtrie_t`
- XPUB delivery-ready 상태 갱신 시 `visit_values`
- 로컬 트리거까지 포함하면 `~trie_t`, `radix_tree_t::free_nodes`, `apply_helper`, `visit_keys`

N을 수만 단위로 키우면 콜 스택을 초과해 **프로세스 크래시**. 구독 길이·개수 상한이 ingest 레이어에 없다.

### 핵심 근거

같은 코드베이스의 `generic_mtrie_t::rm`은 이 위험 때문에 명시적으로 반복 처리로 바뀌어 있다.
구현부 주석도 원격 클라이언트가 재귀 깊이를 조절할 수 있다는 취지를 설명한다. 즉 위험은 이미
인지된 사안인데, 소멸자와 `visit_values`에는 같은 처리가 빠져 있다. `match`도 반복 처리이므로
핵심 수신 경로가 아니라 종료·순회 경로에 남은 문제다.

### 수정 방향

- `mtrie::rm`과 동일하게 **명시적 힙 스택**으로 mtrie 소멸자와 `visit_values`를 먼저 비재귀화한다.
- 같은 패치에 구독 의미를 바꾸는 길이 상한을 넣지 않는다. 상한은 공개 동작과 호환성에 영향을 주므로
  별도 설계와 테스트가 필요하다.
- trie/radix 비재귀화는 후속 하드닝으로 분리한다.

### 처리 기록 (2026-06-14)

`generic_mtrie_t` 소멸자와 `visit_values`를 명시적 힙 스택 기반 반복 처리로 바꾸었다. 이 변경은
구독 prefix의 의미와 방문 순서를 유지하면서, 깊은 단일 체인에서도 C++ 호출 스택을 prefix 깊이만큼
사용하지 않게 만든다. trie/radix 재귀는 원격 구독 경로로 확인되지 않았으므로 이 패치에 섞지 않고
후속 방어 하드닝으로 남겼다.

검증:
- `cmake --build core/build --target unittest_mtrie -j2`: 통과.
- `ctest --test-dir core/build -R '^unittest_mtrie$' --output-on-failure`: 통과.
- `cmake --build core/build -j2`: 통과.
- `bindings/dev_sync_local_core_libs.sh`: core runtime을 바인딩 workspace로 동기화했다.
- `bindings/cpp/tests/run_tests.sh`: 통과.
- `bindings/c/tests/run_tests.sh`: 6개 중 5개 통과, `test_c_contract_surface` 실패. 실패 위치는
  `bindings/c/tests/test_c_contract_surface.c`의 `ZLINK_VERSION_PATCH == 3` 기대값이며,
  이미 별도 C 바인딩 리포트의 `C-BINDING-001`에 기록된 버전 매크로 불일치와 같은 문제다. mtrie
  변경 경로와는 무관하지만, C 바인딩 검증은 해당 항목을 처리할 때 다시 통과시켜야 한다.

Claude 리뷰:
- 2026-06-14: 실제 `generic_mtrie_impl.hpp`와 회귀 테스트를 대조한 결과, 소멸자에서 자식 노드를
  지우기 전에 `_count`와 child 포인터를 비워 재귀 소멸이 다시 발생하지 않음을 확인했다.
- `visit_values`는 역순 push와 LIFO pop으로 기존 pre-order 방문 순서를 유지하며, mtrie 경로에
  깊은 prefix 재귀 패턴이 남아 있지 않다고 판정했다.
- C binding 실패는 `ZLINK_VERSION_PATCH` 기대값 불일치로 mtrie 변경과 무관하다고 확인했다.
- 결론: "추가 이슈 없음."

---

## 2. [Medium · DoS] WS/WSS `read_message_max` 기본 64MiB + `pending_message` 이중 버퍼링

- **분류**: DoS / 큰 메시지 버퍼 이중 보관
- **위치**:
  - `core/src/runtime/transports/ws/ws_transport.cpp:43` (`ZLINK_WS_READ_MESSAGE_MAX`), `:199-205`
  - `core/src/runtime/transports/tls/wss_transport.cpp:45` (`ZLINK_WS_READ_MESSAGE_MAX`)
- **출처**: repo 고유
- **상태**: ✅ 확인

### 참고 (오해 방지)

WS 프레임 파싱(opcode/FIN/continuation/7·16·64비트 length/mask/control-frame 규칙)은 **직접 구현이 아니라 Boost.Beast에 위임**되어 있다(`ws_transport_t`와 `wss_transport_t`가 Beast WebSocket stream을 래핑). 따라서 전형적인 손수 짠 프레임 파서 취약점은 이 repo에 **존재하지 않는다**. 남는 repo 고유 위험은 버퍼를 ZMP 디코더로 넘기는 브리징 코드와 수명 관리에 한정된다.

### 코드

```cpp
// ws_transport.cpp:43, wss_transport.cpp:45  기본 64 MiB
ZLINK_WS_READ_MESSAGE_MAX ... 64 * 1024 * 1024

// ws_transport.cpp:199
if (available > deliver) {
    read_state->pending_message.resize (available);   // 최대 64 MiB 2차 사본
    boost::asio::buffer_copy (...);
}
```

### 트리거

Beast가 WS/WSS 메시지당 최대 64MiB를 `message_buffer`에 버퍼링한 뒤, `async_read_some`이
`pending_message.resize(available)`로 **두 번째 64MiB 사본**을 만든다. 엔진은 이를
`read_buffer_size` 단위로 빼가므로 `available > deliver`가 큰 메시지에서 쉽게 성립한다.
결과적으로 연결 하나가 큰 메시지를 처리하는 동안 최대 약 128MiB를 붙잡을 수 있다.
ZMP `maxmsgsize` 검사는 이 할당의 **하류**라 WS/WSS 전송 버퍼를 보호하지 못한다.
다수 연결로 메모리를 고갈시킬 수 있다.

### 수정 방향

- 우선 `message_buffer`를 증분 소비해 `pending_message` 전체 사본을 제거한다. 이 수정은 기능 의미를
  바꾸지 않고 메모리와 복사 비용을 함께 줄일 수 있다.
- 기본 `read_message_max`를 `_options.maxmsgsize` 또는 작은 고정값에 맞추는 변경은 큰 메시지 사용자에게
  호환성 영향을 줄 수 있다. 기본값 변경은 별도 릴리스 노트와 테스트를 붙여 처리한다.

---

## 3. [Medium · 입력검증] 포트 파싱 `atoi` 범위 초과 시 조용한 절단/래핑

- **분류**: 입력검증 / 잘못된 엔드포인트 해석
- **위치**: `core/src/runtime/utils/ip_resolver.cpp:216` (포트), `:257` (zone_id)
- **출처**: upstream 파생
- **상태**: ✅ 2026-06-14 수정 완료

### 코드

```cpp
// ip_resolver.cpp:216
port = static_cast<uint16_t> (atoi (port_str.c_str ()));
if (port == 0) { errno = EINVAL; return -1; }
...
// ip_resolver.cpp:257
zone_id = static_cast<uint32_t> (atoi (if_str.c_str ()));
```

### 트리거

`atoi`는 범위 검사·후행 garbage 검사를 하지 않고, 결과가 `uint16_t`로 절단된다:

| 입력 | atoi 결과 | 최종 포트 | 결과 |
|------|-----------|-----------|------|
| `host:65537` | 65537 | **1** | 포트 1에 연결/바인드, 무오류 |
| `host:-1` | -1 | **65535** | 수락됨 |
| `host:8080abc` | 8080 | 8080 | 후행 garbage 무시 |

예상과 다른 포트로 연결/바인드되는 보안+정확성 문제. `%`-zone_id(`:257`)도 같은 `atoi` 결함이라 `%-1` 등이 거대한 scope id로 래핑된다.

### 핵심 근거

**같은 repo의 `transports/ws/ws_address.cpp:87-92`는 `strtol` + `port < 0 || port > 65535` 범위 검사로 올바르게 처리**한다 → 코드베이스 내부 불일치. 올바른 패턴이 이미 존재한다.

### 수정 방향

`atoi` → `strtol`로 교체하고 `errno`·전체 소비(`*endptr == '\0'`)·`0 <= v <= 65535` 검사를 추가(`ws_address_t::parse_url`를 모델로). zone_id는 `strtoul` + 범위 검사.

### 처리 기록 (2026-06-14)

`ip_resolver_t::resolve`의 포트와 숫자 zone id 파싱을 digit-only 검사 후 `strtoul`로 변환하도록 바꾸었다.
포트는 `1..65535`만 숫자 파서 경로에서 허용하고, 기존 명시적 `"0"`·`"*"` 처리만 포트 0을 허용한다.
zone id는 0, 음수, 후행 문자가 있는 값, `uint32_t` 범위를 넘는 값을 거부한다.
Claude 리뷰에서 같은 `atoi` 포트 파싱 패턴이 PGM transport와 SPOT control endpoint helper에도
남아 있음을 확인해, 두 경로도 같은 방식으로 숫자 전체 소비와 범위 검사를 적용했다.

검증:
- `cmake --build core/build --target unittest_ip_resolver test_socket_null -j2`: 통과.
- `ctest --test-dir core/build -R '^(unittest_ip_resolver|test_socket_null)$' --output-on-failure`: 통과.
- `cmake --build core/build --target unittest_ip_resolver test_socket_null unittest_spot_data_plane_protocol -j2`: 통과.
- `ctest --test-dir core/build -R '^(unittest_ip_resolver|test_socket_null|unittest_spot_data_plane_protocol)$' --output-on-failure`: 통과.
- `cmake --build core/build -j2`: 통과.
- `bindings/dev_sync_local_core_libs.sh`: core runtime을 바인딩 workspace로 동기화했다.
- `bindings/cpp/tests/run_tests.sh`: 통과.
- `bindings/c/tests/run_tests.sh`: 6개 중 5개 통과, `test_c_contract_surface` 실패. 실패 원인은 기존
  `C-BINDING-001` 버전 매크로 기대값 불일치와 동일하다.

Claude 리뷰:
- 2026-06-14 1차 리뷰에서 #3/#6 자체는 닫혔으나, 같은 패턴으로 PGM port 파싱, SPOT control
  endpoint port 파싱, `zlink_msg_refcnt`, 처리 기록 문구를 추가 확인하라고 지적했다.
- 2026-06-14 2차 리뷰에서 PGM/SPOT port parser의 `atoi` 제거, `zlink_msg_refcnt`의 `check()` 가드,
  처리 기록 문구 수정을 실제 코드와 테스트로 확인했고, "추가 이슈 없음"이라고 판정했다.

---

## 4. [Medium · 파일시스템/TOCTOU] IPC 바인드가 검증 前에 임의 경로 `unlink`

- **분류**: 파일시스템 / 권한 / TOCTOU
- **위치**: `core/src/runtime/transports/ipc/asio_ipc_listener.cpp:118`
- **출처**: pre-resolve unlink 순서는 repo 고유
- **상태**: ✅ 확인

### 코드

```cpp
// asio_ipc_listener.cpp:118 (set_local_address)
::unlink (addr.c_str ());          // ← 검증 이전에 무조건 삭제
_filename.clear ();
ipc_address_t address;
int rc = address.resolve (addr.c_str ());   // 길이/abstract 검증은 그 다음
```

### 트리거

`set_local_address`가 `ipc_address_t::resolve`(길이·abstract `@` 검증) **이전에** 사용자 지정 경로를 무조건 `::unlink()` 한다. 결과:

- bind가 이후 검증에서 실패하더라도, 디렉터리 쓰기 권한이 있는 **임의 경로를 삭제**. 소유/타입 확인 없음.
- `unlink`와 asio `bind()`(`:144`) 사이에 **TOCTOU 갭** — 다른 프로세스가 그 경로를 재생성할 수 있고, 소켓 노드에 대한 `O_EXCL` 류 배타 생성 보장이 없다.

(abstract 소켓 `@name`은 `unlink`가 no-op이라 무해. 파일시스템 경로에서만 실제 삭제 발생.)

### 수정 방향

- `resolve` 검증을 **먼저** 수행, 대상이 프로세스가 소유해야 할 소켓임을 확인한 **후에만** `unlink`.
- 와일드카드 경로처럼 mkdtemp(0700) 디렉터리에 바인드하는 방식 고려.

---

## 5. [Medium · 정수 오버플로] 디코더 할당 사이즈 곱셈 미검사

- **분류**: 정수 오버플로 → under-allocation / 힙 오버플로
- **위치**: `core/src/runtime/protocol/decoder_allocators.cpp:88-89`
- **출처**: upstream 파생(libzmq `shared_message_memory_allocator`), 생성자는 repo 수정
- **상태**: ✅ 확인(도달성은 설정 의존)

### 코드

```cpp
// decoder_allocators.cpp:88
std::size_t const allocationsize =
      target_size
    + sizeof (zlink::atomic_counter_t)
    + _max_counters * sizeof (zlink::msg_t::content_t);   // ← 곱·합 오버플로 미검사
_buf = static_cast<unsigned char *> (std::malloc (allocationsize));
alloc_assert (_buf);
```

### 트리거

`_max_counters * sizeof(content_t)`와 최종 합 모두 오버플로 검사가 없다. wrap이 발생하면 `malloc`이 작은 버퍼를 반환하고, 이후 디코더가 `content_t` 레코드를 그 너머에 기록 → **힙 오버플로**.

64비트 + 현실적 버퍼 크기에서는 wrap이 불가능하므로 **잠복 결함**이다. 다만 `_max_size`/`bufsize_`가 attacker-영향(예: STREAM `resize_buffer`/`set_allocation_size` 성장 경로, `raw_decoder.hpp:27`)을 받으면 도달 가능해진다.

### 수정 방향

`__builtin_mul_overflow`/`__builtin_add_overflow`(또는 `a > SIZE_MAX - b` 가드)로 `allocationsize`를 계산, 오버플로 시 `alloc_assert`/ENOMEM.

---

## 6. [Medium · API 경계] send/public message API에서 msg `check()` 누락

- **분류**: UB 방지 / 잘못된 C API 핸들 방어
- **위치**:
  - `core/src/api/message/message_api.cpp:11-80`
  - `core/src/runtime/core/send_internal.cpp:20,39`
- **출처**: repo 고유 plumbing
- **상태**: ✅ 2026-06-14 수정 완료. recv 측은 반박됨.

recv 경로는 `recv_internal.cpp:56`이 `socket_->recv()`를 호출하고, 그 내부
`socket_base_msg.cpp:309-312`가 `!msg_ || !msg_->check()`를 검사하므로 이 항목에서 제외한다.
유효한 문제는 send 내부와 public message API다. `send_internal.cpp`는 전송 전에
`zlink_msg_size()`를 호출하고, `zlink_msg_size()`는 NULL 또는 닫힌 메시지를 확인하지 않고
`msg_t::size()`를 호출한다.

### 코드

```cpp
// core/src/api/message/message_api.cpp:77
size_t zlink_msg_size (const zlink_msg_t *msg_)
{
    return ((zlink::msg_t *) msg_)->size ();  // NULL/check() 가드 없음
}

// core/src/api/message/message_api.cpp:72
void *zlink_msg_data (zlink_msg_t *msg_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->data ();  // NULL/check() 가드 없음
}

// send_internal.cpp:20
zlink_msg_size (msg_);   // 호출자 제공 msg_의 check() 선행 없이 사용
```

### 트리거

public message API 중 `init`, `init_size`, `init_buffer`, `init_data`, `close`, `data`, `size`는
대체로 NULL 또는 `check()` 검사를 하지 않는다. C 호출자가:

- `zlink_msg_init`을 하지 않았거나
- 이미 `zlink_msg_close`한(`close()`가 `_u.base.type`을 0으로 클리어, `msg.cpp:441`) msg

를 넘기면 `data()/size()`의 `switch(type)`가 release 빌드에서 union garbage를 읽을 수 있다.
send 경로에서는 이 검사가 소켓 내부 전송보다 앞서 실행되므로, 소켓의 `send` 검사가 방어하기 전에
잘못된 메시지를 읽는다.

### 핵심 근거

recv 쪽 소켓 내부에는 이미 `check()` 패턴이 있고, `zlink_msg_adopt()`도 `src->check()`를 확인한다.
검증 패턴이 존재하므로 send 내부와 public message API에 같은 정책을 적용하면 된다.

### 수정 방향

- `send_msg_internal`과 `send_msg_routed_internal`에서 `msg_` NULL 및 `check()`를 먼저 확인하고,
  실패 시 `EFAULT`를 반환한다. 그 뒤에 `zlink_msg_size()`를 호출한다.
- public message API는 함수 성격에 맞춰 NULL 가드를 추가한다. `data()`/`size()`는 오류 반환값이
  제한적이므로 `errno = EFAULT`와 `NULL`/`0` 반환 정책을 명확히 해야 한다.
- 이 수정은 잘못된 호출을 실패로 바꾸는 하드닝이다. 정상 호출의 기능은 바뀌지 않는다.

### 처리 기록 (2026-06-14)

`send_msg_internal`과 `send_msg_routed_internal`이 socket 검사 뒤, `msg_t::size()`를 호출하기 전에
`msg_ == NULL`과 `msg_t::check()`를 확인하도록 바꾸었다. 유효하지 않은 메시지는 `EFAULT`와 `-1`로
실패한다. public message API도 NULL 또는 닫힌 메시지에 대해 `ZLINK_CONFIG_INVALID_HANDLE`,
`NULL`, `0`, 또는 `-1`을 반환하고 `errno = EFAULT`를 설정한다. Claude 리뷰에서 같은 public message
API 경계인 `zlink_msg_refcnt`도 닫힌 메시지의 union을 읽을 수 있다고 확인해, 같은 `check()` 가드를
적용했다. `init*` 함수는 메시지를 새로 만드는 함수이므로 NULL만 확인하고 `check()`를 요구하지 않는다.

검증:
- `test_socket_null`에 NULL message handle, NULL move/copy source·destination, 닫힌 message의
  `close`/`data`/`size`/`refcnt` 회귀 테스트를 추가했다.
- `cmake --build core/build --target unittest_ip_resolver test_socket_null -j2`: 통과.
- `ctest --test-dir core/build -R '^(unittest_ip_resolver|test_socket_null)$' --output-on-failure`: 통과.
- `cmake --build core/build --target unittest_ip_resolver test_socket_null unittest_spot_data_plane_protocol -j2`: 통과.
- `ctest --test-dir core/build -R '^(unittest_ip_resolver|test_socket_null|unittest_spot_data_plane_protocol)$' --output-on-failure`: 통과.
- `cmake --build core/build -j2`: 통과.
- `bindings/dev_sync_local_core_libs.sh`: core runtime을 바인딩 workspace로 동기화했다.
- `bindings/cpp/tests/run_tests.sh`: 통과.
- `bindings/c/tests/run_tests.sh`: 6개 중 5개 통과, `test_c_contract_surface` 실패. 실패 원인은 기존
  `C-BINDING-001` 버전 매크로 기대값 불일치와 동일하다.

Claude 리뷰:
- 2026-06-14 1차 리뷰에서 #3/#6 자체는 닫혔으나, 같은 패턴으로 PGM port 파싱, SPOT control
  endpoint port 파싱, `zlink_msg_refcnt`, 처리 기록 문구를 추가 확인하라고 지적했다.
- 2026-06-14 2차 리뷰에서 PGM/SPOT port parser의 `atoi` 제거, `zlink_msg_refcnt`의 `check()` 가드,
  처리 기록 문구 수정을 실제 코드와 테스트로 확인했고, "추가 이슈 없음"이라고 판정했다.

---

## 7. [Medium · DoS, 정책] 기본 `maxmsgsize = -1`에서 프레임당 최대 약 4GiB 단일 할당

- **분류**: 큰 단일 할당 / DoS
- **위치**: `core/src/runtime/protocol/zmp_decoder.cpp:114-118, 139-140`
- **출처**: upstream 동작
- **상태**: ✅ 확인(문서화/기본값 사안)

### 코드

```cpp
// zmp_decoder.cpp:114
if (unlikely (msg_size_ > _max_msg_size_effective)) { ... EMSGSIZE; return -1; }
...
// zmp_decoder.cpp:139
rc = _in_progress.init_size (static_cast<size_t> (msg_size_));  // malloc(content_t + msg_size_)
```

### 트리거

피어가 헤더 flags=0 + 4바이트 길이 `0xFFFFFFFF`를 보내고 `maxmsgsize`가 미설정(`-1` → 유효 상한 `0xffffffff`)이면 단일 `malloc(약 4GiB + sizeof(content_t))`이 발생한다. `init_size`가 덧셈 오버플로는 가드하고 실패 시 ENOMEM을 정상 반환하므로 **메모리 손상이 아니라 메모리 압박 DoS**다. 신뢰할 수 없는 listener에서는 `ZLINK_MAXMSGSIZE` 설정으로만 완화된다.

### 수정 방향

- 기본값을 바로 바꾸면 기존 큰 메시지 사용자에게 호환성 문제가 생길 수 있다.
- 우선 guide와 샘플에서 신뢰할 수 없는 listener는 `ZLINK_MAXMSGSIZE`를 명시하도록 안내한다.
- 기본값 변경이 필요하면 별도 마이그레이션 계획, 릴리스 노트, 큰 메시지 회귀 테스트를 붙여 처리한다.

---

## 8. [반박됨] asio 메일박스 `_scheduled` ↔ ypipe sleep-flag

- **분류**: 동시성 / 미스 웨이크업 의심
- **위치**: `core/src/runtime/core/mailbox.cpp:116-143`
- **출처**: repo 고유
- **상태**: ❌ 반박됨. 수정 대상 아님.

초판은 `_scheduled`와 ypipe sleep flag가 서로 다른 동기화 변수라 미스 웨이크업 또는 이중 소비가
가능하다고 의심했다. 실제 코드를 다시 대조한 결과, `mailbox.cpp:39-56`의 write/flush와
`:131-136`의 `check_read`가 모두 `_sync` 뮤텍스 안에서 실행된다. 초판이 가정한
"flush와 check_read 사이의 보호되지 않은 창"은 성립하지 않는다.

`flush()`는 `_c`가 sleep 상태일 때만 false를 반환해 `schedule_if_needed`를 트리거한다.
또한 현재 poller는 단일 워커 스레드 모델로 동작하므로 같은 mailbox를 두 핸들러가 동시에 drain하는
경로도 확인되지 않았다.

### 결론

lost wakeup, 이중 소비, UAF로 볼 근거가 없다. 이 항목은 오탐으로 종결하고 코드 수정은 하지 않는다.

---

## 9. [Low · 방어적 하드닝] `command_body_size` 언더플로 방어 clamp

- **분류**: 정수 언더플로 방어 / 원격 도달 미입증
- **위치**: `core/src/runtime/core/msg.cpp:623-651`
- **출처**: upstream 파생
- **상태**: ZMP 원격 경로로는 도달 불가. 코드 자체의 방어 clamp는 권장.

### 코드

```cpp
// msg.cpp:623
size_t zlink::msg_t::command_body_size () const
{
    if (this->is_ping () || this->is_pong ())
        return this->size () - ping_cmd_name_size;     // size < name 이면 언더플로
    ...
    else if (this->is_subscribe ())
        return this->size () - sub_cmd_name_size;       // 동일
    else if (this->is_cancel ())
        return this->size () - cancel_cmd_name_size;    // 동일
    return 0;
}
```

### 트리거

호출자가 ping/sub/cancel 플래그를 켠 채 `size() < name_size`인 메시지를 직접 만들면
`size_t` 언더플로가 거대한 값으로 바뀐다. `command_body()`는 `data() + name_size`를 반환하므로,
그 값을 신뢰한 소비자가 `command_body_size()` 바이트를 읽으면 OOB read가 될 수 있다.

### 도달성 판단

ZMP 디코더는 subscribe/cancel 플래그가 다른 플래그와 함께 오는 조합을 거부한다
(`zmp_decoder.cpp:82-92`). 따라서 초판에서 가정한 "원격 피어가 control 플래그와 subscribe/cancel을
함께 보내 command-body 언더플로를 유발한다"는 경로는 성립하지 않는다. 이 항목은 원격 취약점이
아니라 내부 방어 하드닝으로 낮춘다.

### 수정 방향

`command_body_size`에 방어적 clamp를 넣는다. 정상 메시지는 결과가 바뀌지 않고, 비정상 내부 메시지만
0 길이로 제한된다.

---

## 10. [Low · 방어적 하드닝] `ipc_address_t::to_string` 길이 언더플로 + `_addrlen` 미초기화

- **분류**: 정수 언더플로 방어 / 호출자 제공 sockaddr 경로 한정
- **위치**: `core/src/runtime/transports/ipc/ipc_address.cpp:73-75` (언더플로), `:13-16` (기본 ctor 미초기화)
- **출처**: upstream 파생
- **상태**: 기본 생성자 경로는 `sun_family == 0`으로 조기 반환. 호출자 제공 sockaddr 경로만 방어 필요.

### 코드

```cpp
// ipc_address.cpp:13  — _addrlen 미초기화
zlink::ipc_address_t::ipc_address_t () { memset (&_address, 0, sizeof _address); }

// ipc_address.cpp:73
const size_t src_len = strnlen (src_pos,
    _addrlen - offsetof (sockaddr_un, sun_path) - (src_pos - _address.sun_path));
memcpy (pos, src_pos, src_len);   // 스택 buf 오버런 가능
```

### 트리거

기본 생성자는 `_address`를 0으로 채우므로 `to_string()`이 호출되어도 `sun_family != AF_UNIX` 조건에서
조기 반환한다. 따라서 기본 생성자만으로 즉시 OOB가 되지는 않는다. 남는 위험은
`ipc_address_t(const sockaddr *, socklen_t)`에 호출자가 짧은 `sa_len_`을 넘긴 뒤 `to_string()`을
호출하는 경로다. 이 경우 `_addrlen - offsetof(sockaddr_un, sun_path)` 계산이 언더플로할 수 있다.

### 수정 방향

- 기본 ctor에 `_addrlen (0)`.
- `if (_addrlen <= offsetof(sockaddr_un, sun_path)) { prefix만 사용하고 반환; }` 가드.
- `strnlen` bound를 `sizeof(_address.sun_path)`로 clamp.

---

## 부록 A. 추가 추적 후보 (Low ~ Medium)

| 항목 | 위치 | 출처 | 요지 |
|------|------|------|------|
| `init_view` OOM 롤백 | `core/src/runtime/core/msg.cpp:216-255` | repo 고유 | 비-shared 경로 실패 시 `src_`의 `shared` 플래그가 영구 세팅으로 잔존(행동상 결함, refcnt는 상쇄 → UAF 아님) |
| slice content thread-local 풀 교차스레드 | `core/src/runtime/core/msg.cpp:31-63` | repo 고유 | 스레드 A에서 만든 view를 B에서 close → B 풀 적재. 풀 churn. 메시지 수명 모델을 별도 추적한다 |
| signaler eventfd write EINTR 미루프 | `core/src/runtime/core/signaler.cpp:151` | repo 고유 | socketpair 분기는 EINTR 루프인데 eventfd 분기는 없음 → 드물게 `errno_assert` abort |
| `check_term_acks` 비-acquire load | `core/src/runtime/core/own.cpp:167` | upstream 파생 | `_sent_seqnum.get()`이 비-acquire volatile read. 메일박스 뮤텍스 배리어 덕에 현재 안전하나 주석/assert 권장 |
| `resolve_nic_name` strncpy 미종단 | `core/src/runtime/utils/ip_resolver.cpp:477` | upstream 파생 | AIX/HPUX 경로 한정. `ifr_name` NUL 미종단 가능 → 후속 `strcmp` over-read |
| `blob_t::operator<` NULL memcmp | `core/src/runtime/utils/blob.hpp:87` | upstream 파생 | 빈 blob에 `memcmp(NULL, x, 0)` — 표준상 UB, 실 libc에선 무해(UBSan only) |

---

## 부록 B. 검토 후 "이상 없음"으로 확인한 항목 (오탐 방지용)

이 항목들은 고전적 버그 지점에서 **명시적으로 확인**한 결과 문제가 없었다.

- **WS 프레임 파싱(opcode/length/mask/control 규칙)**: Boost.Beast 위임 → 직접 구현 취약점 부재.
- **multipart txn 롤백**(`core/multipart_send_txn.cpp`): 부분 실패 시 누수·이중 close 없음. `consume_frames_from`이 미소비 파트만 정리, 소비된 파트는 소유권 이전됨. 정확.
- **msg `close()` 멱등성**(`core/src/runtime/core/msg.cpp:441`): 두 번째 `close()`는 `check()` 실패 → `-1/EFAULT`, 이중 free 아님.
- **`copy()`/`move()` refcnt**: copy는 ref 증가(또는 shared 승격 refcnt=2), move는 이전 후 src re-init. 정확.
- **`init_size` 오버플로 가드**(`core/src/runtime/core/msg.cpp:130`): `sizeof(content_t)+size_ > size_` 검사 존재. 정확.
- **`ipc_address_t::resolve` 경로 길이**(`ipc_address.cpp:33-37`): over-long 경로를 `ENAMETOOLONG`으로 **거부**(절단 아님). 양호.
- **TCP accept 루프 fd 처리**(`asio_tcp_listener.cpp`, `asio_ipc_listener.cpp`): 필터-거부/튠 실패/종료 시 fd 명시적 close. fd 누수 없음.
- **`getaddrinfo`/`freeaddrinfo`**(`ip_resolver.cpp`): 모든 경로에서 정확히 해제, 누수 없음. `ai_addrlen` memcpy는 assert로 bound.
- **IPv6 브래킷 파싱, CIDR 마스크 `strtol`**: 경계 검사 존재, OOB 없음.
- **yqueue spare-chunk ABA**: `xchg`만 사용(CAS 아님)이라 ABA 불가. 정확.
- **ypipe flush/check_read CAS(C++11 경로)**: `acq_rel`/acquire 페어링 정확(단 비-C++11 intrinsic fallback의 success=RELEASE는 의심이나 이 빌드 미사용).
- **`atomic_counter::sub`**: ACQ_REL로 "0 도달" 정확 반환, 이중-0 없음.
- **reaper 이중 `send_done`**: `_done_sent` 가드로 방지.
- **mailbox 소멸자 lock/unlock 배리어**: 동시 `send` 대기. 양호.
- **zmp hello/heartbeat/metadata 길이 검사**: 경계 검사 순서 정확, `identity_len`(≤255)·`ctx_len`(≤16) 등 cap 존재. OOB 없음.
- **asio_engine 부분 읽기 prefix 산술**: `read_size > _insize` 가드로 언더플로/OOB 포인터 방지.
- **asio_engine pending-buffer DoS bound**: `total_pending_bytes + _insize + bytes > max_pending_buffer_size → error()`. bounded.

---

## 부록 C. 동시성 코어 추가 메모

- **C5 (메일박스 단일 소비자 가정)**: 초판은 `_scheduled` 경쟁으로 핸들러가 이중 post될 수 있다고
  의심했지만, `write/flush`와 `check_read`가 모두 `_sync` 안에서 실행되고 poller가 단일 워커로
  동작하는 점을 확인해 #8은 반박했다. 이 메모는 오탐 방지 기록으로 남긴다.
- **S1 (`process_pipe_term` 중복 처리)**: `pipe.cpp:614` 부근의 early-return은 **repo 추가**(upstream은 해당 상태에서 assert). 중복 `pipe_term`을 무시하는데, 그 중복이 진짜 중복이 아니라 별개 요청이라면 ack가 매칭되지 않아 피어의 `check_term_acks`가 영원히 대기 → own_t 누수 / 컨텍스트 미종료. 모든 `send_pipe_term` 호출자가 "파이프당 1회"임을 추적 검증 필요.

---

## 부록 D. 방법론·범위

- **검토 영역 분담**:
  1. 와이어 프로토콜 디코더(`protocol/**`) — 신뢰 불가 바이트 파싱
  2. asio/WS 엔진(`engine/asio/**`, `transports/ws/**`)
  3. 주소·전송(`utils/ip_resolver`, `transports/{tcp,ipc,...}`)
  4. 동시성 코어(`core/{mailbox,pipe,ypipe,ctx,reaper,signaler,own,...}`)
  5. msg·C-API·trie/radix(`core/{msg,*_internal}`, `utils/{trie,radix_tree,generic_mtrie,blob}`)
- **검증**: 위 #1·#3·#6·#9의 핵심 코드는 리포트 작성 시 소스 직접 대조로 재확인.
- **후속 리뷰 원칙**: 문서만 읽고 결론을 내리지 않는다. `core/src/**`의 실제 코드, 호출 경로,
  옵션 기본값, 소켓별 도달 경로를 함께 확인한다. 특히 기능·성능 영향 평가는 구현 위치가 hot path인지,
  정상 입력의 공개 동작을 바꾸는지, 기본값 변경이 기존 사용자에게 영향을 주는지를 코드 기준으로
  다시 확인한다.
- **한계**: 정적 리뷰 기준. 동적(ASAN/TSan/fuzzing) 검증은 미수행. #1은 재현 테스트와 회귀 테스트를
  붙여 수정하는 것이 좋다. #8은 현재 수정 대상이 아니다.
- **upstream vs repo 고유**: libzmq 파생 코드는 upstream에도 동일 결함이 있을 수 있으나, repo 고유(asio/WS/services)가 검증이 덜 되어 위험도가 높다는 전제로 가중.
