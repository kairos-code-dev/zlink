# ASIO 개선 제안 (우선순위 기반)

## 목표
- ASIO 전환 이후 남아있는 비용(복사/할당/불필요한 wake-up)을 줄여 throughput과 tail latency를 개선한다.
- 레거시 구조(메일박스/커맨드 경로)를 보존하면서 리스크를 최소화한다.

## 비목표
- `mailbox`/`signaler` 제거 같은 대규모 아키텍처 교체는 포함하지 않는다.
- ZMTP 프로토콜 변경이나 ABI 변경은 하지 않는다.

## P0: 저위험/즉효 개선

### P0-1. ASIO 디버그 로그 기본 OFF
**문제**: `src/asio/asio_engine.cpp`의 `ASIO_ENGINE_DEBUG=1`은 I/O 핫패스에서 fprintf를 유발.  
**변경**: `ZMQ_ASIO_DEBUG` 같은 빌드 옵션으로 분리하고 기본값 OFF.  
**효과**: 불필요한 syscall/포맷 비용 제거, 벤치마크 안정화.  
**검증**: 디버그 빌드에서만 로그 확인 가능하도록 컴파일 플래그 체크.

### P0-2. write 버퍼 재사용 최적화
**문제**: `_write_buffer.assign`은 재할당/복사를 유발.  
**변경**: `_write_buffer.reserve(out_batch_size)` 이후 `resize + memcpy`로 채움.  
**효과**: 재할당/캐시 미스 감소.  
**리스크**: 낮음(기존 의미 동일).  
**검증**: 동일 메시지 패턴에서 throughput 비교.

## P1: 중간 리스크/중간 난이도

### P1-1. Zero-copy 또는 Scatter/Gather write
**문제**: `process_output()`에서 인코더 버퍼를 `_write_buffer`로 복사.  
**변경(안)**:
1) 인코더가 반환한 `_outpos/_outsize`를 직접 `boost::asio::buffer`로 전송.  
2) 또는 `std::vector<boost::asio::const_buffer>`로 프레임 목록을 모아 `async_write` 사용.  
**핵심 제약**: write 완료 전까지 `_outpos` 수명 보장 필요.  
**효과**: memcpy 제거, CPU 절감.  
**리스크**: 수명 관리 오류 시 UAF 가능.  
**검증**: 고부하 송수신/프레임 분할 테스트, ASAN 빌드.

### P1-2. ASIO poller loop의 wake-up 최소화
**문제**: `asio_poller_t::loop()`가 `run_for(100ms)`로 주기적 wake-up.  
**변경(안)**:
- `io_context::run()` + 단일 `steady_timer`를 사용해 다음 타이머 데드라인에 맞춰 깨어나도록 구성.  
**효과**: idle 시 CPU 사용량/지터 감소.  
**리스크**: 타이머 실행 순서/정확성 검증 필요.  
**검증**: 타이머 기반 기능(heartbeat/handshake) 회귀 테스트.

## P2: 선택적/고위험

### P2-1. command 처리 fast-path (동일 스레드)
**문제**: 동일 스레드 내 command도 mailbox 경유.  
**변경(안)**: `object_t::send_command()`에서 `tid`가 동일하면 직접 `process_command()` 호출.  
**리스크**: 재진입/순서 보장 문제. TLS guard 필요.  
**효과**: command 지연 감소(제어 경로).  
**검증**: terminate/seqnum/inproc 시나리오 집중 테스트.

### P2-2. io_uring 활성화 (Linux)
**문제**: 커널 5.10+에서 Asio io_uring 백엔드 미활성 가능성.  
**변경**: 빌드 플래그/Boost 설정 확인 후 `BOOST_ASIO_HAS_IO_URING` 활성화.  
**리스크**: 환경 의존성, CI/배포 복잡도 증가.  
**효과**: syscall 감소 가능(환경 종속).

## 권장 진행 순서
1) P0-1, P0-2 (즉효 + 리스크 낮음)
2) P1-1 (Zero-copy write) 먼저 PoC, 안정성 확인 후 적용
3) P1-2 (poller loop 개선)
4) P2 항목은 별도 RFC로 검토
