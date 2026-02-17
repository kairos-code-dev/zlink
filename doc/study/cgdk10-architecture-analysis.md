# CGDK10 (Cho sanghyun's Game Classes II) - Architecture & Performance Analysis

> **Author**: Cho SangHyun (sangduck@cgcii.co.kr)
> **Version**: 10.0 / Release 2019.12.11
> **Homepage**: http://www.CGCII.co.kr
> **Target**: Game Server / High-Performance Network Application

---

## Source Reference Convention

본 문서의 모든 소스 참조는 아래 base path를 기준으로 한다.

```
BASE = core/tests/scenario/stream/cgdk10/upstream/CGDK10.Cpp
```

| 약칭 | 실제 경로 |
|------|----------|
| `buffers/` | `{BASE}/include/cgdk/buffers/` |
| `sdk10/common/` | `{BASE}/include/cgdk/sdk10/common/` |
| `sdk10/containers/` | `{BASE}/include/cgdk/sdk10/containers/` |
| `sdk10/net.socket/` | `{BASE}/include/cgdk/sdk10/net.socket/socket/` |
| `examples/` | `{BASE}/examples/` |

---

## 1. Overview

CGDK10은 한국 개발자 조상현이 만든 **게임 서버 전용 C++ 네트워크 프레임워크**다.
핵심 설계 철학은 **Zero-Copy 버퍼 직렬화 + Lock-Free 자료구조 + OS 네이티브 비동기 I/O**를
하나의 통합 아키텍처로 묶어 극한의 throughput을 달성하는 것이다.

```
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                     │
│         (Game Logic / Message Handlers)                  │
├─────────────────────────────────────────────────────────┤
│              Messageable / Message Map                   │
│         (process_message dispatch system)                │
│         → sdk10/common/message_map.h                     │
├─────────────────────────────────────────────────────────┤
│           Packetable (Stream Packet Parser)              │
│     (TCP stream → message 경계 분리, length-prefix)      │
│         → sdk10/net.socket/net.io.packetable.stream.h    │
├─────────────────────────────────────────────────────────┤
│    Sendable / Receivable (Async I/O Completion)          │
│  ┌──────────────────┐  ┌────────────────────────────┐   │
│  │ IOCP / RIO (Win)  │  │  epoll (Linux)             │   │
│  └──────────────────┘  └────────────────────────────┘   │
│  → sdk10/net.socket/net.io.sendable.tcp_async*.h         │
│  → sdk10/net.socket/net.io.receivable.stream*.h          │
├─────────────────────────────────────────────────────────┤
│         Connective (Acceptor / Connector)                │
│    (Connection lifecycle management)                     │
│  → sdk10/net.socket/net.acceptor.h                       │
│  → sdk10/net.socket/net.connector.h                      │
├─────────────────────────────────────────────────────────┤
│              Buffer System (Core Engine)                  │
│  ┌────────────┐ ┌──────────┐ ┌───────────────────┐     │
│  │ buffer_view │ │  buffer   │ │  shared_buffer    │     │
│  │ (read-only) │ │ (r/w)    │ │ (ref-counted r/w) │     │
│  └────────────┘ └──────────┘ └───────────────────┘     │
│  → buffers/_buffer_view.h, _basic_buffer.h,              │
│    _shared_buffer.h, _buffer_common.h                    │
├─────────────────────────────────────────────────────────┤
│        Infrastructure (Lock-Free / Pool / Timer)         │
│  ┌────────────────┐ ┌──────────────┐ ┌────────────┐    │
│  │ lockfree_stack  │ │ allocator_pool│ │ object_ptr  │    │
│  │ (CAS + ABA fix) │ │ (freelist)   │ │ (ref count) │    │
│  └────────────────┘ └──────────────┘ └────────────┘    │
│  → sdk10/common/lockfree_self_stack.h                    │
│  → sdk10/containers/_allocator_pool.h                    │
└─────────────────────────────────────────────────────────┘
```

---

## 2. Directory Structure

```
CGDK10.Cpp/
├── include/cgdk/
│   ├── buffers/                    ← 핵심: 버퍼 직렬화 엔진
│   │   ├── _buffer_base.h          ← POD 구조체 (data_, size_)
│   │   ├── _buffer_view.h          ← 읽기 전용 뷰 + extract 연산
│   │   ├── _basic_buffer.h         ← 읽기/쓰기 + append/prepend 연산
│   │   ├── _shared_buffer.h        ← shared_ptr 기반 참조 카운트 래퍼
│   │   ├── _buffer_common.h        ← 타입 트레이트, 직렬화 디스패처, 매크로 (~2400행)
│   │   └── _Imemory.h              ← 메모리 인터페이스 (bound 관리)
│   │
│   ├── sdk10/
│   │   ├── common/                 ← 공통 기반
│   │   │   ├── definitions.h       ← 플랫폼 매크로, 컴파일러 설정
│   │   │   ├── lockfree_self_stack.h    ← Lock-Free 스택 (CAS + ABA 해결)
│   │   │   ├── lockfree_self_stack_64.h ← 64비트 포인터 버전
│   │   │   ├── lockable.h          ← RAII 기반 잠금 래퍼
│   │   │   ├── scoped_lock.h       ← Scoped lock 유틸리티
│   │   │   ├── message_map.h       ← 메시지 디스패치 맵
│   │   │   └── definition.lock.h   ← Lock 관련 정의
│   │   │
│   │   ├── containers/             ← 고성능 컨테이너
│   │   │   ├── _allocator_pool.h   ← Freelist 기반 메모리 풀 Allocator
│   │   │   ├── _fast_vector.h      ← 최적화된 벡터
│   │   │   ├── _circular_list.h    ← 링 버퍼 리스트
│   │   │   └── _placement_array.h  ← Placement new 배열
│   │   │
│   │   └── net.socket/socket/      ← 네트워크 소켓 계층
│   │       ├── net.io.sendable.tcp_async.h              ← 비동기 TCP 송신
│   │       ├── net.io.sendable.tcp_async_gather_buffered.h ← Scatter-Gather 송신
│   │       ├── net.io.receivable.stream.rio.h           ← RIO(Registered I/O) 수신
│   │       ├── net.io.packetable.stream.h               ← TCP 스트림 패킷 파서
│   │       ├── net.io.connectable.tcp.h                 ← TCP 연결 관리
│   │       ├── net.acceptor.h                           ← Accept 처리
│   │       ├── net.connector.h                          ← Connect 처리
│   │       └── net.io.connective.manager.h              ← 연결 관리자
│   │
│   └── sdk10/net.socket.h          ← 네트워크 모듈 통합 헤더
│
└── examples/
    ├── 1.1.tcp_echo/               ← TCP Echo 서버/클라이언트
    ├── 1.2.tcp_relay_echo/         ← TCP Relay Echo
    ├── 1.3.tcp_multicast_echo/     ← TCP Multicast Echo
    └── 2.1.chatting_simple/        ← 간단한 채팅 서버
```

---

## 3. Core Buffer System (고성능의 핵심)

### 3.1 Buffer Hierarchy

CGDK10의 성능 핵심은 **4-Layer Buffer 계층 구조**다.

```
  buffer_base<char>          ← 16 bytes (data_ + size_), POD
       │                        → buffers/_buffer_base.h:22-35
       │
  _buffer_view<char>         ← 읽기 전용, extract 연산 제공
       │                        → buffers/_buffer_view.h:19-629
       │
  _basic_buffer<char>        ← 읽기/쓰기, append/prepend + bound 검사
       │                        → buffers/_basic_buffer.h:50-1860
       │
  _shared_buffer<buffer>     ← shared_ptr<Imemory> 참조 카운트 래퍼
                                → buffers/_shared_buffer.h:19-207
```

#### buffer_base (최하층 - 16 bytes POD)

> **Source**: `buffers/_buffer_base.h:22-35`

```cpp
template <class ELEMENT_T = char>
class buffer_base
{
protected:
    using traits   = _buffer_traits_t<ELEMENT_T>;
    using element_t = ELEMENT_T;
    using size_type = size_t;

public:
    size_type    size_ = 0;       // 현재 데이터 크기 (bytes)
    element_t*   data_ = nullptr; // 데이터 시작 포인터
};
```

**핵심**: 전체 버퍼 시스템의 기반이 **딱 2개의 멤버**로 구성된 POD 구조체.
`sizeof(buffer_base) == 16` (64bit 환경), 캐시 라인 친화적이다.

표준 이름 정의 (`_buffer_base.h:42-45`):
```cpp
using const_buffer   = buffer_base<const char>;
using mutable_buffer = buffer_base<char>;
```

#### _buffer_view (읽기 전용 + Extract)

> **Source**: `buffers/_buffer_view.h:19-629`

`buffer_base`를 `protected` 상속하여 읽기 전용 인터페이스를 제공한다.

```cpp
// _buffer_view.h:19-20
template <class ELEM_T>
class _buffer_view : protected buffer_base<ELEM_T>
```

**extract 핵심 구현** (`_buffer_view.h:594-606`):
```cpp
template <class T>
void _extract_general(T& _dest)
{
    // bounds check
    _CGD_BUFFER_BOUND_CHECK(sizeof(T) <= this->size_);

    // reinterpret_cast로 직접 역직렬화 (Zero-Copy)
    _dest = *reinterpret_cast<_buffer_return_t<traits,T>*>(this->data_);

    // 포인터 전진
    this->data_ += sizeof(T);
    this->size_ -= sizeof(T);
}
```

참조 반환 버전 (`_buffer_view.h:607-622`):
```cpp
template <class T>
T& _extract_general()
{
    _CGD_BUFFER_BOUND_CHECK(sizeof(T) <= this->size_);
    T* p = reinterpret_cast<_buffer_return_t<traits,T>*>(this->data_);
    this->data_ += sizeof(T);
    this->size_ -= sizeof(T);
    return *p;  // 버퍼 내 메모리를 직접 참조 반환 (완전 Zero-Copy)
}
```

#### _basic_buffer (읽기/쓰기 + Append)

> **Source**: `buffers/_basic_buffer.h:50-1860`

**append 공개 인터페이스** (`_basic_buffer.h:180-183`):
```cpp
template <class T>
constexpr appd_tr<T> append(const T& _data) {
    return APPD_t<self_t,T>::_do_append(*this, _data);
    // → serializer_append가 타입별로 _append_general / _append_string /
    //   _append_array 등으로 디스패치
}
```

**operator<< 체이닝** (`_basic_buffer.h:331-332`):
```cpp
template <class T>
self_t& operator<<(const T& _rhs) {
    APPD_t<self_t, T>::_do_append(*this, _rhs);
    return *this;
}
```

**_append_general 핵심 구현** (`_basic_buffer.h:947-967`):
```cpp
template <class T>
constexpr T& _append_general(const T& _data)
{
    // 1) 버퍼 끝 위치에 포인터 설정
    auto p = reinterpret_cast<T*>(this->data_ + this->size_);

    // 2) bounds check
    _CGD_BUFFER_BOUND_CHECK((p + 1) <= this->get_upper_bound());

    // 3) 직접 대입 (reinterpret_cast + assignment = Zero-Copy 직렬화)
    *p = _data;

    // 4) 크기 갱신
    this->size_ += sizeof(T);
    return *p;
}
```

**_append_bytes (원시 바이트 복사)** (`_basic_buffer.h:1418-1443`):
```cpp
constexpr base_t _append_bytes(std::size_t _size, const void* _buffer)
{
    CGDK_ASSERT(_buffer != nullptr || (_buffer == nullptr && _size == 0), ...);
    _CGD_BUFFER_BOUND_CHECK((this->data_ + this->size_ + _size) <= this->get_upper_bound());

    const auto buf_dest = this->data_ + this->size_;
    if (_size == 0) return base_t(buf_dest, 0);

    if (_buffer != nullptr && _buffer != buf_dest)
    {
        memcpy(buf_dest, _buffer, _size);   // 대량 데이터: memcpy 한 번
    }

    this->size_ += _size;
    return base_t(buf_dest, _size);
}
```

**_append_string (문자열 직렬화)** (`_basic_buffer.h:1025-1062`):
```cpp
template <class T>
constexpr std::enable_if_t<is_string_type<T>::value, base_t>
_append_string(std::basic_string_view<T> _string)
{
    const auto buf_dest = this->data_ + this->size_;
    const auto length_string = _string.size();
    const auto bytes_copy = length_string * sizeof(T);
    const auto added_length = sizeof(COUNT_T) + sizeof(T) + bytes_copy;
    // 총 크기 = 길이 필드 + NULL 종료 + 문자열 데이터

    _CGD_BUFFER_BOUND_CHECK((buf_dest + added_length) <= this->get_upper_bound());

    auto buf_now = buf_dest;

    // ① 문자열 길이 기록 (NULL 포함)
    *reinterpret_cast<COUNT_T*>(buf_now) = static_cast<COUNT_T>(length_string + 1);
    buf_now += sizeof(COUNT_T);

    // ② 문자열 데이터 복사
    if (bytes_copy != 0) {
        memcpy(buf_now, _string.data(), bytes_copy);
        buf_now += bytes_copy;
    }

    // ③ NULL 종료자 추가
    *reinterpret_cast<T*>(buf_now) = 0;

    this->size_ += added_length;
    return base_t(buf_dest, added_length);
}
```

**_append_array (POD 배열 - memcpy 최적화)** (`_basic_buffer.h:993-1005`):
```cpp
template <class T>
constexpr std::enable_if_t<is_memcopy_able<T>::value, base_t>
_append_array(const T* _data, std::size_t _count)
{
    // ① 원소 수 기록
    this->_append_general<COUNT_T>(static_cast<COUNT_T>(_count));

    // ② POD 배열 전체를 memcpy 한 번으로 복사
    return this->_append_bytes(_count * sizeof(T), _data);
}
```

비-POD 배열은 원소별 순회 (`_basic_buffer.h:968-992`):
```cpp
template <class T>
constexpr std::enable_if_t<!is_memcopy_able<T>::value, base_t>
_append_array(const T* _data, std::size_t _count)
{
    this->_append_general<COUNT_T>(static_cast<COUNT_T>(_count));
    for (std::size_t i = 0; i < _count; ++i) {
        this->_append<T>(_data[i]);  // 각 원소를 재귀적으로 직렬화
    }
    ...
}
```

### 3.2 Zero-Copy Serialization (핵심 성능 원리)

CGDK10은 **별도의 직렬화 포맷 없이 C++ 메모리 레이아웃을 그대로 사용**한다.

```
┌─ 전통적 직렬화 (protobuf, JSON, etc.) ──────────────────┐
│                                                          │
│  Object → Serialize → [Copy] → Buffer → [Copy] → Send   │
│  Recv → Buffer → [Copy] → Deserialize → [Copy] → Object │
│                                                          │
│  총 4회 복사, 인코딩/디코딩 CPU 오버헤드                    │
└──────────────────────────────────────────────────────────┘

┌─ CGDK10 Zero-Copy ─────────────────────────────────────┐
│                                                         │
│  Object ──→ reinterpret_cast로 직접 Buffer에 기록       │
│           (memcpy 또는 직접 대입, 1회 복사)               │
│                                                         │
│  Buffer  ──→ reinterpret_cast로 직접 Object로 읽기      │
│           (포인터 캐스팅, 0회 복사)                       │
│                                                         │
│  총 0~1회 복사, 인코딩/디코딩 없음                        │
└─────────────────────────────────────────────────────────┘
```

#### POD 타입 직렬화 (Zero-Copy)

```cpp
// _basic_buffer.h:954,960 에서 실제 동작:
*reinterpret_cast<int32_t*>(data_ + size_) = 42;
size_ += 4;

// 바이트 레이아웃 (Little-Endian):
// Offset 0-3: 0x2A 0x00 0x00 0x00
```

#### String 직렬화

> **Source**: `buffers/_basic_buffer.h:1025-1062`

```
"hello" 직렬화 바이트 레이아웃:

Offset 0-3:  0x06 0x00 0x00 0x00   ← COUNT_T = 6 (5글자 + NULL)
Offset 4-8:  0x68 0x65 0x6C 0x6C 0x6F  ← "hello"
Offset 9:    0x00                  ← NULL 종료자
Total: 10 bytes
```

#### 배열/컨테이너 직렬화

> **Source**: `buffers/_basic_buffer.h:993-1005` (POD), `:968-992` (non-POD)

```
vector<int32_t>{1, 2, 3} 직렬화 바이트 레이아웃:

Offset 0-3:   0x03 0x00 0x00 0x00  ← COUNT_T = 3 (원소 수)
Offset 4-7:   0x01 0x00 0x00 0x00  ← int32 = 1
Offset 8-11:  0x02 0x00 0x00 0x00  ← int32 = 2
Offset 12-15: 0x03 0x00 0x00 0x00  ← int32 = 3
Total: 16 bytes (POD이므로 memcpy 1회로 처리)
```

### 3.3 Compile-Time Type Dispatch (Template Metaprogramming)

CGDK10은 **컴파일 타임에 타입별 최적 직렬화 경로를 결정**한다.

> **Source**: `buffers/_buffer_common.h:787-822`

```cpp
// 기본 serializer_append (L787-791)
template<class B, class T, class FLAG = void>
class serializer_append {
    using TX = std::remove_const_t<T>;
public:
    using type = TX&;
    template<class S>
    constexpr static type _do_append(S& _s, const TX& _data) {
        return _s.template _append_general<TX>(_data);  // 기본: 직접 대입
    }
};

// 타입 별칭 (L815, L821)
template<class B, class T> using APPD_t = serializer_append<B, remove_ref_const<T>>;
template<class B, class T> using EXTR_t = serializer_extract<B, remove_ref_const<T>>;
```

타입별 특수화가 `_buffer_common.h`에 대량으로 정의되어 있다:

| 특수화 조건 | 소스 위치 | 디스패치 대상 |
|------------|----------|-------------|
| `is_string_type<T>` | `_buffer_common.h:1660-1694` | `_append_string` / `_extract_string_view` |
| `is_linear_container<T>` | `_buffer_common.h:1767-1772` | 컨테이너 순회 직렬화 |
| `is_set_container<T>` | `_buffer_common.h:2083-2088` | set 컨테이너 직렬화 |
| `is_associative_container<T>` | `_buffer_common.h:2217-2222` | map 컨테이너 직렬화 |
| `std::tuple<T...>` | `_buffer_common.h:1154-1160` | 튜플 원소별 직렬화 |
| `Ibuffer_serializable` 상속 | `_buffer_common.h:1186-1192` | 사용자 정의 직렬화 |
| `google::protobuf::Message` | `_buffer_common.h:1339-1359` | Protobuf 연동 |
| `std::array<T,N>` | `_buffer_common.h:1011-1016` | 고정 크기 배열 |
| `std::span<T,E>` | `_buffer_common.h:2036-2054` | span 직렬화 |

#### is_memcopy_able (핵심 타입 트레이트)

> **Source**: `buffers/_buffer_common.h:496-506`

```cpp
template <class T> struct is_memcopy_able
{
    static const bool value = std::is_trivially_copyable<T>::value
                           && !std::is_pointer<T>::value
                           && !is_std_string_view<T>::value
                           && !is_std_string<T>::value
                           && !is_reserved_class<T>::value
                           && !is_struct_serializable<T>::value;
};
template <class T> constexpr bool is_memcopy_able_v = is_memcopy_able<T>::value;
```

**의미**: `trivially_copyable`이면서 포인터/string/커스텀 직렬화 타입이 아닌 경우에만
`memcpy`로 블록 복사를 허용한다. 이 트레이트가 배열 직렬화에서 **원소별 순회 vs memcpy 한 방**을
결정하는 핵심 분기점이다.

### 3.4 Shared Buffer & Reference Counting

> **Source**: `buffers/_shared_buffer.h:19-207`

```cpp
// _shared_buffer.h:19-35
template <class BASE_T = _buffer_view<char>>
class _shared_buffer : public BASE_T {
    ...
    #if defined(_CGDK)
        using object_ptr_t = object_ptr<memory_t>;    // CGDK 내부 ref-count
    #else
        using object_ptr_t = std::shared_ptr<memory_t>; // standalone: std::shared_ptr
    #endif
    ...
private:
    object_ptr_t psource;   // ← 메모리 소유권 (L200)
};
```

```
┌─────────────────────────────────────────┐
│            shared_buffer A              │
│  data_ ──────────┐    psource ──┐      │
│  size_ = 100     │    (refcnt=3)│      │
└──────────────────│──────────────│──────┘
                   │              │
┌──────────────────│──────────────│──────┐
│  shared_buffer B │              │      │
│  data_ ──────────┤    psource ──┤      │
│  size_ = 50      │    (shared)  │      │
└──────────────────│──────────────│──────┘
                   ▼              ▼
         ┌─────────────────────────────┐
         │      Imemory (실제 메모리)    │
         │  [====== 256 bytes ======]  │
         │  ^data_A  ^data_B           │
         │  ◄─100──► ◄─50─►           │
         └─────────────────────────────┘
```

**Imemory 인터페이스** (`buffers/_Imemory.h:20-35`):
```cpp
class Imemory : protected _buffer_view<char> {
    auto data() const noexcept { return this->data_; }
    auto size() const noexcept { return this->size_; }
    auto get_lower_bound() const noexcept { return data_; }
    auto get_upper_bound() const noexcept { return data_ + size_; }
    auto get_bound() const noexcept {
        return buffer_bound{ get_lower_bound(), get_upper_bound() };
    }
};
```

**buffer_bound 구조체** (`buffers/_buffer_common.h:117-122`):
```cpp
struct buffer_bound {
    const void* lower = nullptr;   // 할당 메모리 시작
    const void* upper = nullptr;   // 할당 메모리 끝
    constexpr void reset() noexcept { lower = upper = nullptr; }
};
```

**핵심 이점**:
- 동일 메모리 블록을 여러 버퍼가 **서로 다른 offset/size로 참조** 가능
- 복사 없이 **슬라이싱** (message 파싱 시 sub-buffer 생성)
- `shared_ptr`의 reference counting으로 **자동 메모리 해제**
- `split_head` / `split_tail` (`_shared_buffer.h:258-276`)로 버퍼 분할

### 3.5 Bounds Checking (Configurable)

> **Source**: `buffers/_buffer_common.h:49-55`

```cpp
#if defined(CGDK_NO_BOUND_CHECK)
    // Release 최적화: bounds check 완전 제거
    #define _CGD_BUFFER_BOUND_CHECK(condition)

#elif defined(CGDK_DISABLE_ASSERT)
    // Release: assert 없이 throw만
    #define _CGD_BUFFER_BOUND_CHECK(condition) \
        if((condition) == false) { \
            throw std::overflow_error("CGDK::shared_buffer out of memory bounding"); \
        }
#else
    // Debug: assert + throw
    #define _CGD_BUFFER_BOUND_CHECK(condition) \
        if((condition) == false) { \
            CGDK_ASSERT_ON_BOUND; \
            throw std::overflow_error("CGDK::shared_buffer out of memory bounding"); \
        }
#endif
```

참고: `NDEBUG` 또는 `!_DEBUG` 매크로가 정의되면 `CGDK_DISABLE_ASSERT`가 자동 설정된다
(`_buffer_common.h:45-47`).

**Release 빌드**: `CGDK_NO_BOUND_CHECK` 정의 시 모든 bounds check가 **컴파일에서 완전히 제거**된다.
런타임 오버헤드 = 0.

---

## 4. Network I/O Architecture

### 4.1 Platform-Native Async I/O

CGDK10은 **각 OS의 최고 성능 I/O 모델을 직접 사용**한다.

| Platform | I/O Model | 소스 | 특징 |
|----------|-----------|------|------|
| Windows | **IOCP** | `net.io.sendable.tcp_async.h:74-76` | 커널 레벨 스레드 풀, 완료 큐 |
| Windows | **RIO** | `net.io.receivable.stream.rio.h:45-115` | IOCP보다 더 낮은 레이턴시 |
| Linux | **epoll** | `net.io.sendable.tcp_async.h:77-85` | 이벤트 기반 I/O 다중화 |

```cpp
// net.io.sendable.tcp_async.h:74-86 - 플랫폼별 분기
#if defined(_WINSOCK2API_)     // Windows: IOCP
    int    m_count_pended = 0;
    size_t m_bytes_pended = 0;
#elif defined(_SYS_EPOLL_H)    // Linux: epoll
    QUEUE_SEND  m_queue_pending;
    QUEUE_SEND  m_queue_sending;
    bool        m_flag_sending = false;
#endif
    lockable<>  m_lockable_sending;
```

### 4.2 Proactor Pattern (비동기 완료 통보 모델)

> **Source**: `sdk10/net.socket/net.io.sendable.tcp_async.h:63-87`

```
┌──────────────────────────────────────────────────┐
│                 Application Thread                 │
│                                                    │
│  ① send(shared_buffer) 호출                        │
│     └→ process_sendable()에서 비동기 전송 요청      │
│        (L68: virtual bool process_sendable(...))   │
│                                                    │
│  ② I/O 완료 시 OS가 Completion Queue에 통보         │
│     └→ IOCP/epoll Worker Thread가 꺼내감           │
│                                                    │
│  ③ process_complete_sendable() 콜백               │
│     (L69: virtual void process_complete_sendable)  │
│     └→ 전송 통계 업데이트, 다음 전송 처리            │
└──────────────────────────────────────────────────┘
```

### 4.3 Scatter-Gather I/O (Gather Buffered Send)

여러 작은 메시지를 **하나의 시스템 콜로 한꺼번에 전송**한다.

> **Source**: `sdk10/net.socket/net.io.sendable.tcp_async_gather_buffered.h:34-85`

```cpp
class Ntcp_async_gather_buffered {
    lockable<>     m_lockable_sending;    // L77: 송신 보호용 락
    QUEUE_SEND     m_queue_pending;       // L78: 대기 큐 - 새 메시지 축적
    QUEUE_SEND     m_queue_sending;       // L79: 전송 큐 - OS에 전달된 메시지
    bool           m_flag_sending{false}; // L80: 현재 전송 중 여부
};
```

**QUEUE_SEND 노드 구조** (Linux epoll 버전, `L44-61`):
```cpp
struct QUEUE_SEND {
    struct NODE {
        buffer_view     buf_send;          // 전송할 버퍼 뷰
        object_ptr<Ireferenceable> powner; // 소유권 (ref-count)
        std::size_t     bytes_data = 0;
        std::size_t     count_message = 0;
        std::size_t     bytes_remained = 0;
    };
    container_t<NODE> array_node;
    std::size_t bytes_data = 0;
};
```

```
Thread A: send(msg1) ─┐
Thread B: send(msg2) ─┤──→ m_queue_pending에 축적 (lock으로 보호)
Thread C: send(msg3) ─┘
                              │
                    ┌─────────▼──────────┐
                    │ Pending → Sending   │  (큐 swap)
                    │ 축적된 메시지들을    │
                    │ Scatter-Gather 전송  │
                    └─────────────────────┘
                              │
                    OS Kernel (단일 시스템 콜)
```

**핵심 이점**: 시스템 콜 횟수 최소화 → 커널/유저 전환 오버헤드 감소

### 4.4 TCP Stream Packet Parser

> **Source**: `sdk10/net.socket/net.io.packetable.stream.h:38-151`

```cpp
template <class TMSG_HEAD = uint32_t>
class net::io::packetable::Nstream {
    virtual std::size_t process_packet(shared_buffer& _buffer, ...) override
    {
        // L68: shared_buffer에서 크기 접두사만큼 잘라서 message 뷰 생성
        shared_buffer message = _buffer ^ sizeof(TMESSAGE_HEAD);

        // L71: 남은 바이트 수 추적
        std::size_t remained_size = _buffer.size();

        // L75: message 경계 분리 루프
        while (remained_size >= sizeof(TMESSAGE_HEAD))
        {
            // L78: 메시지 헤더에서 전체 크기 읽기
            auto message_size = definition_message_header<TMESSAGE_HEAD>
                                    ::_get_message_size(message);

            // L81: 뷰 크기 설정
            ((buffer_view&)message).set_size(message_size);

            // L85: 너무 짧은 메시지 → 에러
            THROW_IF(message_size < sizeof(TMESSAGE_HEAD), ...);

            // L88: 불완전한 메시지 → 다음 recv 대기
            BREAK_IF(message_size > remained_size);

            // L100-110: 완전한 메시지 → 핸들러 디스패치
            msg.buf_message = message;
            this->process_pre_message(msg);
            this->process_message(msg);

            // L116-117: 포인터 전진 (Zero-Copy: 같은 버퍼 내에서 이동)
            ((buffer_view&)message).add_data(message_size);
            remained_size -= message_size;
        }
        ...
    }
};
```

**메시지 프레임 포맷**:
```
┌─────────────┬──────────────────────────────┐
│ TMSG_HEAD   │         Payload              │
│ (4 bytes)   │    (message_size - 4 bytes)  │
│ = total len │                              │
└─────────────┴──────────────────────────────┘
```

### 4.5 RIO (Registered I/O) 수신

> **Source**: `sdk10/net.socket/net.io.receivable.stream.rio.h:45-115`

Windows RIO는 IOCP보다 더 낮은 레이턴시를 제공하는 고급 I/O 모델이다.

```cpp
class net::io::receivable::Nstream_rio {
    // L94: 최소/최대 메시지 버퍼 크기 설정
    std::size_t m_minimum_mesage_buffer_size;
    std::size_t m_maximum_message_buffer_size;

    // L106-107: 수신 버퍼 (WSABUF로 OS에 등록)
    shared_buffer  m_buffer_received;    // 수신 데이터 축적 버퍼
    WSABUF         m_wsabuf_receiving;   // OS에 전달할 버퍼 디스크립터
    DWORD          m_wsa_bytes_received; // L108: 수신 바이트 수
    DWORD          m_wsa_flag;           // L109: WSA 플래그

    // L98-104: IOCP 실행 객체 (poolable - 오브젝트 풀에서 재사용)
    class executable_receiving : virtual public Iexecutable,
                                public Npoolable<executable_receiving> {
        Nstream_rio* m_preceivable_stream = nullptr;
        virtual intptr_t process_execute(intptr_t _result, std::size_t _param);
    };
};
```

---

## 5. Lock-Free Data Structures

### 5.1 lockfree_self_stack (CAS + ABA 해결)

> **Source**: `sdk10/common/lockfree_self_stack.h:52-272`

**"self" stack의 의미**: 별도 노드를 할당하지 않고, 저장할 객체 자체에 `Next` 포인터가 있어야 한다.
즉, intrusive linked list 방식이다 (추가 메모리 할당 없음).

**CGSLIST_HEAD 유니온** (`lockfree_self_stack.h:119-132`):
```cpp
union CGSLIST_HEAD {
    int64_t alligned;        // 64비트 단위 원자적 교환용
    struct {
        TDATA          phead;     // 스택 top 포인터 (32bit)
        unsigned short depth;     // 현재 원소 수
        unsigned short sequence;  // ABA 방지 시퀀스 번호
    } partial;
};

volatile CGSLIST_HEAD m_head;    // L131
```

**Push 구현** (`lockfree_self_stack.h:148-176`):
```cpp
void push(TDATA _pdata) noexcept {
    CGSLIST_HEAD temp_head, temp_head_new;
    do {
        temp_head.alligned = m_head.alligned;          // ① 현재 head 스냅샷
        _pdata->Next = m_head.partial.phead;           // ② 새 노드의 Next = 현재 top
        temp_head_new.partial.phead = _pdata;          // ③ 새 head 구성
        temp_head_new.partial.depth = temp_head.partial.depth + 1;
        temp_head_new.partial.sequence = temp_head.partial.sequence + 1;
    } while (_InterlockedCompareExchange64(             // ④ CAS로 원자적 교체
                &m_head.alligned,
                temp_head_new.alligned,
                temp_head.alligned) != temp_head.alligned);
}
```

**Pop 구현** (`lockfree_self_stack.h:210-243`):
```cpp
TDATA pop() noexcept {
    CGSLIST_HEAD temp_head, temp_head_new;
    do {
        temp_head.alligned = m_head.alligned;
        if (temp_head.partial.phead == 0) return 0;    // 빈 스택
        temp_head_new.partial.phead = temp_head.partial.phead->Next;
        temp_head_new.partial.depth = temp_head.partial.depth - 1;
        temp_head_new.partial.sequence = temp_head.partial.sequence + 1;
    } while (_InterlockedCompareExchange64(
                &m_head.alligned,
                temp_head_new.alligned,
                temp_head.alligned) != temp_head.alligned);
    return temp_head.partial.phead;
}
```

**Batch push (여러 노드 한 번에)** (`lockfree_self_stack.h:179-207`):
```cpp
void push(TDATA _pfirst, TDATA _plast, int _count) noexcept {
    // linked list 체인 (_pfirst → ... → _plast)을 한 번의 CAS로 push
    ...
    temp_head_new.partial.depth = temp_head.partial.depth + _count;
    ...
}
```

**ABA 문제 해결 원리**:
```
head를 64비트로 구성: [pointer(32) | depth(16) | sequence(16)]

Thread A: head={ptr=X, seq=1} → pop 시작, 문맥 전환
Thread B: pop X, pop Y, push X → head={ptr=X, seq=4}
Thread A: CAS 비교 시 sequence가 1≠4 → 재시도!

→ 포인터만 비교하면 같은 X인데, sequence가 달라서 ABA를 감지
```

### 5.2 allocator_pool (Freelist Memory Pool)

> **Source**: `sdk10/containers/_allocator_pool.h:46-189`

STL 호환 Allocator 인터페이스를 가진 freelist 기반 메모리 풀.

**allocate** (`_allocator_pool.h:153-170`):
```cpp
pointer allocate(size_type) {
    if (m_pHead == 0)
        return (TYPE*)::operator new(sizeof(TYPE));  // 풀 비었으면 새 할당

    TYPE* pHead = m_pHead;
    m_pHead = *reinterpret_cast<TYPE**>(m_pHead);    // freelist에서 꺼내기
    return pHead;
}
```

**deallocate** (`_allocator_pool.h:173-177`):
```cpp
void deallocate(const pointer _Ptr, size_type) noexcept {
    *reinterpret_cast<TYPE**>(_Ptr) = m_pHead;       // 해제 메모리를 freelist에 반환
    m_pHead = (TYPE*)_Ptr;                           // 메모리 블록 자체를 next 포인터로!
}
```

**소멸자 - 풀 정리** (`_allocator_pool.h:140-149`):
```cpp
~allocator_pool() noexcept {
    while (m_pHead != 0) {
        TYPE* pHead = m_pHead;
        m_pHead = *reinterpret_cast<TYPE**>(m_pHead);
        ::operator delete(pHead);   // 풀 소멸 시에만 실제 해제
    }
}
```

**핵심 기법**: 해제된 메모리 블록의 **처음 sizeof(TYPE*)바이트를 next 포인터로 재활용**.
추가 노드 구조체 없이 intrusive freelist를 구성한다.

```
Freelist (해제된 메모리 재사용):

m_pHead → [Block A: next=B | unused...] → [Block B: next=C | unused...] → [Block C: next=0]

allocate():  m_pHead에서 꺼냄 (O(1), 시스템 콜 없음)
deallocate(): m_pHead 앞에 삽입 (O(1), 시스템 콜 없음)
```

---

## 6. Performance Optimization Summary

### 6.1 고성능을 만드는 핵심 기법 정리

| 기법 | 소스 위치 | 효과 |
|------|----------|------|
| **Zero-Copy 직렬화** | `_basic_buffer.h:948-967` | 메모리 복사 0~1회, 인코딩/디코딩 CPU 비용 0 |
| **Compile-Time Dispatch** | `_buffer_common.h:787-822` | 런타임 분기 제거, 인라인 최적화 |
| **Bounds Check 제거** | `_buffer_common.h:49-55` | Release 빌드에서 if 분기 비용 0 |
| **Lock-Free Stack** | `lockfree_self_stack.h:119-243` | 스레드 경합 시 blocking 없음 |
| **Freelist Pool** | `_allocator_pool.h:153-177` | `malloc/free` 호출 최소화, O(1) 할당 |
| **shared_ptr 참조 카운트** | `_shared_buffer.h:19-200` | 버퍼 슬라이싱 시 복사 없이 동일 메모리 참조 |
| **Scatter-Gather I/O** | `net.io.sendable.tcp_async_gather_buffered.h:34-85` | 커널-유저 전환 최소화 |
| **OS 네이티브 I/O** | `net.io.sendable.tcp_async.h`, `net.io.receivable.stream.rio.h` | OS 최고 성능 I/O 모델 직접 사용 |
| **Proactor Pattern** | `net.io.sendable.tcp_async.h:63-87` | I/O 대기 시 스레드 blocking 없음 |
| **Length-Prefix Protocol** | `net.io.packetable.stream.h:38-151` | 빠른 메시지 경계 파싱 O(1) |
| **POD memcpy 최적화** | `_basic_buffer.h:993-1005` | POD 배열은 memcpy 1회로 처리 |
| **is_memcopy_able 트레이트** | `_buffer_common.h:496-506` | 컴파일 타임에 최적 복사 경로 결정 |

### 6.2 성능 흐름 (메시지 1개 라이프사이클)

```
[송신 측]
  ① buffer 할당
     → allocator_pool에서 O(1)로 꺼냄 (_allocator_pool.h:153)
  ② append<T>(data)
     → _append_general에서 reinterpret_cast로 직접 기록 (_basic_buffer.h:960)
  ③ send(shared_buffer)
     → m_queue_pending에 축적 (net.io.sendable.tcp_async_gather_buffered.h:78)
     → scatter-gather로 한 번에 전송 (시스템 콜 1회)
  ④ 전송 완료
     → shared_ptr refcount 감소 → 0이면 pool로 반환

[수신 측]
  ① OS가 epoll/IOCP로 데이터 도착 통보
  ② recv → shared_buffer에 수신
     → Nstream_rio::process_complete_receivable (net.io.receivable.stream.rio.h:90)
  ③ packetable이 length-prefix 파싱 → message 경계 분리
     → Nstream::process_packet (net.io.packetable.stream.h:53)
     → 같은 버퍼 내에서 포인터만 이동 (Zero-Copy)
  ④ process_message() 호출
     → extract<T>() → _extract_general에서 reinterpret_cast로 직접 읽기
       (_buffer_view.h:601)
  ⑤ 처리 완료
     → shared_ptr refcount 감소 → 0이면 pool로 반환
```

### 6.3 다른 프레임워크와의 비교

```
┌──────────────────┬──────────┬──────────┬──────────┬──────────────┐
│                  │ CGDK10   │ Boost.   │ gRPC     │ 일반 TCP     │
│                  │          │ Asio     │          │ (교과서적)   │
├──────────────────┼──────────┼──────────┼──────────┼──────────────┤
│ 직렬화           │ Zero-Copy│ 없음     │ Protobuf │ JSON/Custom  │
│ (복사 횟수)      │ (0~1회)  │ (수동)   │ (2~4회)  │ (2~4회)      │
├──────────────────┼──────────┼──────────┼──────────┼──────────────┤
│ 메모리 관리      │ Pool +   │ custom   │ Arena    │ new/delete   │
│                  │ LockFree │ alloc    │ Alloc    │              │
├──────────────────┼──────────┼──────────┼──────────┼──────────────┤
│ I/O 모델         │ IOCP/RIO │ IOCP/    │ epoll/   │ select/      │
│                  │ /epoll   │ epoll    │ IOCP     │ poll         │
├──────────────────┼──────────┼──────────┼──────────┼──────────────┤
│ 메시지 Gather    │ O        │ 수동     │ X (HTTP2)│ X            │
├──────────────────┼──────────┼──────────┼──────────┼──────────────┤
│ Bounds Check     │ Release  │ N/A      │ 항상     │ 항상         │
│ 제거 가능        │ 에서 제거│          │          │              │
├──────────────────┼──────────┼──────────┼──────────┼──────────────┤
│ 타입 Dispatch    │ 컴파일   │ N/A      │ 런타임   │ 런타임       │
│                  │ 타임     │          │ (vtable) │              │
└──────────────────┴──────────┴──────────┴──────────┴──────────────┘
```

---

## 7. Key API Usage Patterns

### 7.1 Buffer Append (직렬화)

> **Source**: `buffers/_basic_buffer.h:180-183` (append), `:331-332` (operator<<)

```cpp
// operator<< 체이닝으로 직관적 직렬화
CGDK::shared_buffer buf = alloc_shared_buffer(256);
buf << int32_t(42)
    << std::string("hello")
    << std::vector<int>{1, 2, 3};

// 또는 명시적 append
buf.append<int32_t>(42);
buf.append(std::string("hello"));
```

### 7.2 Buffer Extract (역직렬화)

> **Source**: `buffers/_buffer_view.h:118-123` (extract), `buffers/_shared_buffer.h:88-102` (shared extract)

```cpp
// extract로 순차 역직렬화
auto value  = buf.extract<int32_t>();       // 42
auto str    = buf.extract<std::string>();   // "hello"
auto vec    = buf.extract<std::vector<int>>(); // {1, 2, 3}

// operator>> 체이닝도 가능 (_basic_buffer.h:328-329)
int32_t v; std::string s;
buf >> v >> s;
```

### 7.3 Socket Template Composition

> **Source**: `sdk10/net.socket/net.definition.socket_templates.h:25-27`

```cpp
// 소켓을 기능 조합으로 구성 (Mixin 패턴)
using tcp_server_socket = net::socket::tcp<
    net::io::sendable::Ntcp_async_gather_buffered,  // 송신: Scatter-Gather
    net::io::receivable::Nstream_rio,                // 수신: RIO
    net::io::packetable::Nstream<uint32_t>,          // 패킷 파서
    net::io::messageable::Nbase                      // 메시지 핸들러
>;
```

매크로 단축:
```cpp
#define SOCKET_TCP_SERVER  net::socket::tcp_server   // L25
#define SOCKET_TCP_CLIENT  net::socket::tcp_client   // L26
```

---

## 8. Design Principles

### 8.1 핵심 설계 철학

1. **"복사를 줄여라"**: 모든 계층에서 Zero-Copy를 추구한다.
   - 직렬화: `reinterpret_cast` (`_basic_buffer.h:954,960`)
   - 역직렬화: 포인터 캐스팅 + 참조 반환 (`_buffer_view.h:608-621`)
   - 메시지 파싱: 포인터 이동만 (슬라이싱) (`net.io.packetable.stream.h:116-117`)
   - 전송: Scatter-Gather (`net.io.sendable.tcp_async_gather_buffered.h`)

2. **"런타임 비용은 컴파일 타임으로 옮겨라"**: `serializer_append/extract` 특수화 + SFINAE로
   타입별 최적 경로를 컴파일 타임에 결정한다 (`_buffer_common.h:787-822`, `:1660-2312`).

3. **"OS 기능을 직접 써라"**: 추상 레이어를 최소화하고, IOCP/RIO/epoll을 직접 사용한다.
   Boost.Asio 같은 추상화 레이어 없이 OS API를 바로 호출.

4. **"잠금을 피하라"**: Lock-Free CAS 기반 자료구조로 스레드 경합을 제거한다
   (`lockfree_self_stack.h`). 필요 시에만 `lockable<>`을 사용.

5. **"할당을 줄여라"**: Freelist Pool로 `malloc/free` 빈도를 줄인다
   (`_allocator_pool.h`). `executable_receiving`도 `Npoolable`로 풀링 (`net.io.receivable.stream.rio.h:98`).

### 8.2 사용된 외부 라이브러리

CGDK10은 **외부 라이브러리 의존성이 거의 없다**.

| 라이브러리 | 용도 | 필수 여부 |
|-----------|------|----------|
| C++ Standard Library | `std::shared_ptr`, `std::vector`, `std::list`, `std::string`, type_traits | 필수 |
| Windows API | `winsock2.h`, `mswsock.h` (IOCP/RIO) | Windows에서 필수 |
| Linux API | `sys/epoll.h` | Linux에서 필수 |
| Boost.Asio | `examples/` 내 Asio 연동 예제용 | 선택적 (핵심 기능은 불필요) |
| Google Protobuf | `_buffer_common.h:1339-1359` 특수화 | 선택적 (ifdef 보호) |

**자체 구현 모듈** (외부 의존 없음):
- Buffer 직렬화 엔진 (`buffers/` 전체)
- Lock-Free Stack (`sdk10/common/lockfree_self_stack.h`)
- Memory Pool Allocator (`sdk10/containers/_allocator_pool.h`)
- Scatter-Gather Send Queue (`sdk10/net.socket/net.io.sendable.tcp_async_gather_buffered.h`)
- TCP Packet Parser (`sdk10/net.socket/net.io.packetable.stream.h`)
- Fast Vector (`sdk10/containers/_fast_vector.h`)
- Circular List (`sdk10/containers/_circular_list.h`)

---

## 9. Conclusion

CGDK10의 고성능은 단일 기법이 아닌 **모든 레이어에 걸친 최적화의 총합**에서 나온다.

```
성능 = Zero-Copy 직렬화       (_basic_buffer.h)
     + Compile-Time Dispatch  (_buffer_common.h)
     + Lock-Free 동시성       (lockfree_self_stack.h)
     + Memory Pool 재사용     (_allocator_pool.h)
     + OS 네이티브 Async I/O  (net.io.*.h)
     + Scatter-Gather 전송    (net.io.sendable.tcp_async_gather_buffered.h)
     + Configurable Bounds    (_buffer_common.h:49-55)
```

게임 서버처럼 **수만 개의 동시 연결에서 초당 수백만 개의 작은 메시지를 처리**해야 하는 환경에
특화된 설계다. 전통적인 직렬화 프레임워크(protobuf, JSON)와 범용 네트워크 라이브러리(Boost.Asio)를
쓸 때 발생하는 오버헤드를 시스템 수준에서 제거한 것이 핵심이다.

---

## Appendix: Quick Source Reference

| 컴포넌트 | 파일 | 핵심 라인 | 설명 |
|---------|------|----------|------|
| Buffer 기본 구조 | `_buffer_base.h` | L22-35 | `size_` + `data_` POD |
| Buffer 읽기 뷰 | `_buffer_view.h` | L19-20, L594-622 | extract 연산 |
| Buffer 읽기/쓰기 | `_basic_buffer.h` | L180-183, L331-332, L947-967 | append / operator<< |
| 문자열 직렬화 | `_basic_buffer.h` | L1025-1062 | LENGTH + DATA + NULL |
| 바이트 복사 | `_basic_buffer.h` | L1418-1443 | memcpy 래퍼 |
| POD 배열 최적화 | `_basic_buffer.h` | L993-1005 | memcpy 1회 |
| 비-POD 배열 | `_basic_buffer.h` | L968-992 | 원소별 순회 |
| 참조 카운트 버퍼 | `_shared_buffer.h` | L19-35, L200 | psource (shared_ptr) |
| 메모리 인터페이스 | `_Imemory.h` | L20-35 | bound 관리 |
| Bounds Check | `_buffer_common.h` | L49-55 | 3단계 설정 |
| 타입 트레이트 | `_buffer_common.h` | L496-506 | is_memcopy_able |
| Serializer Dispatch | `_buffer_common.h` | L787-822 | APPD_t / EXTR_t |
| 타입별 특수화 | `_buffer_common.h` | L869-2312 | 50+ 타입 특수화 |
| Lock-Free Stack | `lockfree_self_stack.h` | L119-132, L148-243 | CAS + ABA |
| Memory Pool | `_allocator_pool.h` | L133, L153-177 | freelist allocator |
| Async TCP 송신 | `net.io.sendable.tcp_async.h` | L39-87 | IOCP/epoll 분기 |
| Gather 송신 | `net.io.sendable.tcp_async_gather_buffered.h` | L34-85 | scatter-gather 큐 |
| RIO 수신 | `net.io.receivable.stream.rio.h` | L45-115 | Registered I/O |
| 패킷 파서 | `net.io.packetable.stream.h` | L38-151 | length-prefix 파싱 |
| buffer_bound | `_buffer_common.h` | L117-122 | lower/upper 포인터 |
