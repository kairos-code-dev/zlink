# Bindings Integration Test Plan (Common Scenarios)

목표
- 모든 바인딩(.NET/Java/Node/Python)에서 **동일한 시나리오**로 핵심 기능/소켓 타입 메시징을 검증한다.
- 최소 기능(메시지 송수신/옵션/에러처리) + 서비스 디스커버리/게이트웨이/스팟 사용 시나리오를 포함한다.

참고
- Net.Zmq.Tests/Integration 패턴 참고: https://github.com/ulala-x/net-zmq/tree/main/tests/Net.Zmq.Tests/Integration

## 공통 전제
- 테스트는 기본적으로 `tcp://`, `ws://`, `inproc://` **3가지 transport를 루프**로 돌린다
  - `tcp://127.0.0.1:PORT`
  - `ws://127.0.0.1:PORT`
  - `inproc://name`
- 타임아웃은 짧게, 재시도(최대 2~3회) 허용
- 동일한 메시지 payload/프레임 구조 사용
- 실패 시 상세 로그 출력 (서비스명/토픽/프레임 수)

## 공통 시나리오 목록

### S1. Context/Socket Lifecycle
- context 생성/종료
- socket 생성/종료
- double close 허용 여부 확인(예외/무시 정책 일관성 확인)

### S2. PAIR basic roundtrip
- PAIR A bind, PAIR B connect
- 단일 프레임 send/recv
- `DONTWAIT` 수신 시 EAGAIN 확인

### S3. PUB/SUB basic
- PUB bind, SUB connect
- SUB subscribe("topic")
- PUB send("topic", payload)
- SUB recv payload 확인

### S4. DEALER/ROUTER basic
- ROUTER bind, DEALER connect
- DEALER send(single frame)
- ROUTER recv: routing-id + data frame 확인
- ROUTER reply -> DEALER recv

### S5. XPUB/XSUB subscription propagation
- XPUB bind, XSUB connect
- XSUB subscribe send
- XPUB recv subscription frame 확인

### S6. Message multipart
- DEALER/ROUTER 또는 PAIR에서 multi-part send
- recv 시 프레임 수/순서 확인

### S7. Socket options
- HWM or SND/RCV TIMEOUT set/get
- getsockopt 반환값 확인

### S8. Registry/Discovery
- Registry: set endpoints, start
- Discovery: connect registry, subscribe("svc")
- Provider: register("svc", endpoint)
- Discovery: providerCount/serviceAvailable/getProviders

### S9. Gateway
- Provider 등록 후 Gateway send("svc", frames)
- Provider router socket에서 수신 확인
- Gateway recv로 응답 수신

### S10. Spot
- SpotNode bind/connect registry (필요 시)
- Spot topic create
- Spot publish (multi-part)
- Spot subscribe + recv 확인

## 언어별 매핑

### .NET (xUnit)
- 파일: `bindings/dotnet/tests/Zlink.Tests/Integration/*.cs`
- 공통 helper: TestPorts, Retry, AssertFrames

### Java (JUnit)
- 파일: `bindings/java/src/test/java/io/ulalax/zlink/integration/*Test.java`
- 공통 helper: TestUtil (port allocator, wait)

### Node (node:test)
- 파일: `bindings/node/tests/integration/*.test.js`
- 공통 helper: `tests/_util.js`

### Python (unittest)
- 파일: `bindings/python/tests/integration/test_*.py`
- 공통 helper: `bindings/python/tests/util.py`

## 실행 계획
- 각 언어별 기본 5~8개 시나리오부터 적용
- Registry/Discovery/Gateway/Spot는 통합 테스트로 묶되 플래그로 enable
- CI에서는 최소 시나리오만 실행하고 확장 시나리오를 nightly로 분리 고려

## 다음 단계
1) 시나리오 확정
2) 공통 helper 설계
3) 언어별 테스트 구현 (멀티에이전트 병렬)
4) CI 통합
