# jvmzlink Message Path Optimization Study

이 문서는 `jvmzlink` 성능 저하 원인을 추적하고, 실제로 어떤 가설이
맞았는지 정리한 작업 기록이다.

대상은 `with_stream` 비교에서 드러난 Java binding 성능 문제다.
특히 작은 메시지 요청-응답 패턴에서 왜 `.NET`보다 크게 느려지는지,
그리고 어떤 수정이 실제로 효과가 있었는지를 남긴다.

## recv 경로 추가 분석

처음에는 `with_stream`의 `packet callback` 경로만 보고 있었지만,
실제 애플리케이션에서는 `recv()`를 직접 쓰는 경우도 많다.
그래서 `stream recv` 버전과 `multi router-router`를 같이 보면서
generic `recv` 경로도 따로 최적화했다.

핵심 질문은 두 가지였다.

1. `recv()` 전체를 줄이면 다른 소켓에도 이득이 있는가
2. `stream recv`가 특히 느린 이유는 binding의 generic `recv` 때문인가,
   아니면 Java 쪽 프레이밍 재조립 때문인가

### 1. generic `recv`의 single-part 경로는 실제 병목이었다

기존 Java binding의 generic `recv()`는 `Native.recvMultipart(...)` 뒤에
항상 아래 단계를 탔다.

- `RoutingId` 생성
- `Message.fromOwnedMsgVector(...)`
- `Message[]` 생성
- `Received` 생성
- `parts()`용 immutable `List` 생성

하지만 `router-router`나 raw stream chunk처럼 실제로는 single-part가 많은
패턴에서는 이 비용이 과했다.

그래서 아래 최적화를 넣었다.

- `Message.fromOwnedMsgSingle(...)` 추가
- `MessagePlane.recv()/recvNoWait()`에서 `partCount == 1`이면
  `Message[]` 대신 단일 `Message`로 바로 materialize
- `Received`에 single-part 전용 표현 추가
  - `singlePart`
  - `routingIdOrNull()`
  - `routingIdOrThrow()`
- `Received.parts()`는 필요할 때만 `List`를 생성하도록 변경

이건 stream 전용 꼼수가 아니라, Java binding의 generic `recv` 모델 자체를
줄인 것이다.

### 2. `stream recv` 서버에서 `byte[]`를 매번 만드는 것도 크게 손해였다

`jvmzlink-recv` 서버 초기 버전은 매 chunk마다:

- `part.data()`
- 새 `byte[] chunk`
- 완전한 frame이면 다시 `Message.copyOf(byte[])`

를 했다.

이건 binding이 아니라 raw recv 사용자 코드에서 흔히 나오는 낭비다.

그래서 `Message`에 아래 helper를 추가했다.

- `Message.copyOf(Message source)`
- `Message.copyTo(byte[] destination, int sourceOffset, int destinationOffset, int length)`
- `Message.readShortBe(int)`
- `Message.readIntBe(int)`

그리고 `jvmzlink-recv` 서버를 바꿨다.

- 완전한 frame이면 `Message.copyOf(part)`로 바로 echo
- chunk buffering도 `part.data()` 대신 `copyTo(...)`로 바로 누적
- 1-byte connect/disconnect event는 `readByte(0)`로 판별

### 3. 결과

#### `with_stream` 64B

처음 `jvmzlink-recv`는 대략 `12~18 Kops/s` 수준이었다.

generic `recv` single-part fast path와 raw recv 서버 최적화를 넣은 뒤,
다시 측정하면 아래 수준까지 올라왔다.

- `jvmzlink-recv`: `149.41 Kops/s`
- `jvmzlink`: `103.57 Kops/s`

결과 파일:

- [20260418_232447 comparison](/home/hep7/project/kairos/zlink/core/bench/with_stream/results/20260418_232447/comparison.md)

즉 raw `recv` 기반 stream echo도 이제는 packet callback 버전보다 느리기만 한
경로가 아니다.

#### Java multi `router-router` 64B

generic `recv` 최적화가 실제로 다른 소켓에도 의미가 있는지 보기 위해
`MULTI_ROUTER_ROUTER`도 다시 측정했다.

- `jvmzlink`: `128.84 Kops/s`
- 결과 파일:
  [perf_java_multi_linux_20260418_232515.txt](/home/hep7/project/kairos/zlink/bindings/java/perf/results/multi/report/perf_java_multi_linux_20260418_232515.txt)

이건 이전 `.NET` 측정치 `105.93 Kops/s`보다 높은 값이다.

즉 이번 최적화는 stream 전용 우회가 아니라,
Java binding의 generic `recv` 경로를 줄인 결과로 보는 것이 맞다.

## 문제 배경

`with_stream` 64B 비교에서 `jvmzlink`는 `netzlink`보다 눈에 띄게 낮은
수치를 보였다.

초기 의문은 크게 세 가지였다.

1. Java FFM 자체가 느린가
2. native `msg_t` 할당과 해제가 병목인가
3. Java binding의 `Message` 모델이 너무 무거운가

조사 과정에서 이 셋을 하나씩 분리해서 확인했다.

## 먼저 확인한 것

### 1. FFM 메모리 복사 자체는 주병목이 아니었다

`bindings/java/perf/src/main/java/dev/kairoscode/zlink/perf/MemoryInteropMicrobench.java`
를 추가해서 아래 항목을 비교했다.

- `FFM MemorySegment`
- `Unsafe`
- `DirectByteBuffer`

비교 항목:

- native로 복사
- heap으로 복사
- wrapper 생성
- raw alloc

핵심 결과는 다음과 같았다.

- `copy_to_native`
- `copy_to_heap`

이 두 항목에서 FFM은 `unsafe`나 `direct`와 비슷한 수준이었다.

즉 "FFM 메모리 복사 자체가 2배 느리다"는 가설은 맞지 않았다.

반대로 새 버퍼를 할당하는 `alloc`은 차이가 컸다.
하지만 실제 `jvmzlink` hot path는 여기보다 callback/send 경계 쪽의 영향을
더 많이 받는 것으로 보였다.

### 2. callback으로 `msg_t`를 받는 자체는 큰 차이가 아니었다

native에서 Java callback으로 `msg_t` 포인터를 전달하는 것 자체는
`.NET`과 비교해도 결정적인 차이로 보이지 않았다.

실제 차이는 callback 직후 Java binding이 그 `msg_t`를 어떻게 감싸고,
응답을 다시 만들어서 send까지 내리느냐에서 생겼다.

## 실제로 맞았던 병목

### 1. `Message.fromOwnedNative(...)` 구현이 불필요하게 무거웠다

초기 구현은 callback에서 받은 native `msg_t`를 바로 감싸지 않고,
새 `Message`를 만들면서 아래 작업을 했다.

- `new Message()`
- `Arena.ofConfined()`
- 새 `msg_t` 슬롯 allocate
- `msgMove`
- 원본 `msgClose`

이건 callback 입력 경로에서 불필요한 비용이었다.

그래서 `fromOwnedNative(...)`를 수정해서:

- callback으로 받은 native `msg_t`를 그대로 `Message`가 들고
- ownership만 인계하도록 바꿨다

즉:

- 새 `Arena.ofConfined()` 제거
- 새 `msg_t` 슬롯 allocate 제거
- `msgMove/msgClose` 제거

이 변경은 실제로 의미 있는 개선을 만들었다.

`MessagePathMicrobench` 기준:

- `callback_materialize_message,64`
  - 이전: `366.79ns`
  - 이후: `211.53ns`
- `callback_materialize_message,1024`
  - 이전: `412.37ns`
  - 이후: `252.48ns`

즉 입력 callback materialization은 실제 병목이었고,
이 부분은 고친 것이 맞았다.

### 2. outbound `Message` 경로도 따로 봐야 했다

입력 쪽을 줄인 뒤에도 end-to-end 성능이 기대만큼 올라가지 않았다.
그래서 출력 경로를 따로 분해해서 봤다.

이를 위해
`bindings/java/src/test/java/dev/kairoscode/zlink/perf/MessageOutboundMicrobench.java`
를 추가했다.

이 microbench는 응답을 만드는 일반 경로를 아래처럼 쪼갰다.

- `reply_bytes_only`
- `response_copy_of_bytes`
- `response_copy_of_bytes_send_prepare`
- `response_build_from_arrays`
- `response_build_from_arrays_send_prepare`
- `response_build_from_messages`
- `response_build_from_messages_send_prepare`

핵심 결과는 분명했다.

#### 64B

- `reply_bytes_only`: `27.97ns`
- `response_copy_of_bytes`: `102.27ns`
- `response_copy_of_bytes_send_prepare`: `173.44ns`
- `response_build_from_arrays`: `347.50ns`
- `response_build_from_arrays_send_prepare`: `429.77ns`
- `response_build_from_messages`: `394.62ns`
- `response_build_from_messages_send_prepare`: `456.27ns`

#### 1024B

- `reply_bytes_only`: `35.27ns`
- `response_copy_of_bytes`: `150.42ns`
- `response_copy_of_bytes_send_prepare`: `218.91ns`
- `response_build_from_arrays`: `401.53ns`
- `response_build_from_arrays_send_prepare`: `468.89ns`
- `response_build_from_messages`: `444.51ns`
- `response_build_from_messages_send_prepare`: `495.34ns`

이 결과는 중요한 사실을 보여준다.

- `byte[] -> Message.copyOf(...) -> transferTo/send` 경로가 가장 얇다
- 반대로 `new Message(size)`를 만들고
  `writeByte/copyFrom(...)`로 직접 채우는 builder 경로는 더 느리다

즉 직관과 달리, 현재 Java binding 구현에서는
"native payload를 직접 채우는 경로"가 더 빠르지 않았다.

### 3. single-part send에서 `msgMove`를 없애는 것은 실제 이득이 있었다

기존 `send(Message)` 경로는 send 직전에 다시 아래 작업을 했다.

- scratch `msg_t` 준비
- `msgInit`
- `msgMove`
- send 실패 시 `restoreFromNative`

하지만 core 계약상 단일 part send는:

- 성공 시 ownership이 라이브러리로 이동하고
- 실패 시 ownership은 호출자에게 남는다

그래서 Java binding도 single-part `Message` send에서
굳이 scratch `msg_t`로 한 번 더 `msgMove`할 이유가 없었다.

이 경로를 다음처럼 바꿨다.

- 기존 `Message`가 들고 있는 native `msg_t`를 바로 send
- 성공 시 `markTransferred()`
- 실패 시 원래 `Message`를 그대로 유지

이 수정은 benchmark 전용 꼼수가 아니라,
Java binding의 일반적인 single-part `Message` send 경로를 얇게 만든 것이다.

이 변경 뒤 `jvmzlink` 64B short run은 대략 `116 Kops/s -> 126 Kops/s` 수준으로 올랐다.
즉 의미 있는 개선은 있었지만, `.NET` 수준까지 붙이기에는 여전히 부족했다.

### 4. payload pointer cache가 wrapper 생성 비용을 줄였다

그 다음 JFR에서 실제로 보인 건 `FFM` copy보다
`Message`가 payload pointer를 캐시하는 과정에서 생기는 wrapper였다.

특히 반복해서 보인 스택은 이랬다.

- `NativeMsg.msgData(...)`
- `NativeMemorySegmentImpl`
- `reinterpret(...)`
- `Message.<init>(int)`

즉 `Message`가 payload를 다루려고 할 때마다
native pointer를 `MemorySegment` wrapper로 만들고,
다시 길이를 붙인 view를 만드는 비용이 쌓이고 있었다.

이를 줄이기 위해 `Message` 내부 구현을 바꿨다.

- payload는 `MemorySegment` view를 캐시하지 않고 `long address`만 캐시
- hot path인 아래 메서드는 address 기반으로 처리
  - `readByte`
  - `copyTo(byte[])`
  - `copyFrom(byte[])`
  - `copyFrom(Message)`
  - `writeByte`

이건 `Message`의 일반적인 읽기/쓰기/copy 경로 전체에 영향을 주는 수정이다.

microbench에서는 실제로 좋아졌다.

#### 64B

- `response_copy_of_bytes`: `105.24ns -> 103.80ns`
- `response_build_from_arrays`: `102.01ns -> 99.10ns`
- `response_build_from_arrays_send_prepare`: `172.99ns -> 173.47ns`
- `response_build_from_messages_send_prepare`: `171.96ns -> 169.34ns`

#### 1024B

- `response_copy_of_bytes`: `136.67ns -> 131.40ns`
- `response_build_from_arrays`: `145.89ns -> 141.83ns`
- `response_build_from_arrays_send_prepare`: `223.19ns -> 213.42ns`
- `response_build_from_messages_send_prepare`: `219.94ns -> 213.67ns`

즉 wrapper/view 생성 비용은 실제로 있었고,
payload address cache는 일반 경로에서도 의미가 있었다.

### 5. outbound `Message`에서 confined arena를 없애는 것도 의미가 있었다

그 다음에는 outbound `Message` 생성이 여전히 무거운 이유를 더 좁혔다.

기존 `new Message()` / `new Message(size)`는 내부에서 callback마다:

- `Arena.ofConfined()`
- `msg_t` slot allocate

를 했다.

이걸 바꿔서:

- owned `Message`는 `Unsafe.allocateMemory(sizeof(zlink_msg_t))`로
  고정 크기 `msg_t` slot을 직접 확보
- `close()`에서 직접 해제

하도록 바꿨다.

동시에 `u32 rid` send도 Java 쪽에서 native routing-id struct를 만들지 않고,
native bridge helper가 바로 `zlink_send_rid`를 호출하게 정리했다.

- `zlink_java_send_u32(...)`
- `Native.sendMultipartU32(...)`

이건 benchmark 전용 우회가 아니라,
Java binding의 일반 `StreamSocket.send(int rid, Message)` 경로를 더 얇게 만든 것이다.

이후 outbound microbench는 다시 눈에 띄게 좋아졌다.

#### 64B

- `response_copy_of_bytes`: `104.95ns -> 81.29ns`
- `response_build_from_arrays`: `102.91ns -> 79.98ns`
- `response_copy_of_bytes_send_prepare`: `181.33ns -> 162.33ns`
- `response_build_from_arrays_send_prepare`: `177.46ns -> 161.38ns`
- `response_build_from_messages_send_prepare`: `173.28ns -> 158.35ns`

#### 1024B

- `response_copy_of_bytes`: `133.68ns -> 122.66ns`
- `response_build_from_arrays`: `138.70ns -> 122.34ns`
- `response_copy_of_bytes_send_prepare`: `216.51ns -> 193.95ns`
- `response_build_from_arrays_send_prepare`: `210.75ns -> 197.06ns`
- `response_build_from_messages_send_prepare`: `212.92ns -> 197.34ns`

즉 outbound `Message` 생성과 send 준비 경로도
여전히 줄일 여지가 있었고, 실제로 줄일 수 있었다.

### 6. `Message` helper를 늘려서 callback 검증과 frame 조립도 줄였다

현재 `jvmzlink` stream echo server는:

- header가 `"stream.echo"`와 같은지 확인
- prefix와 header를 다시 응답 frame에 채움

이 두 작업을 매 packet마다 한다.

이를 위해 `Message`에 아래 helper를 추가했다.

- `contentEquals(byte[])`
- `writeIntBe(int offset, int value)`
- `writeShortBe(int offset, short value)`

그리고 서버 쪽은:

- `header.contentEquals(MSG_NAME)`로 header 검증
- 고정 prefix+header 바이트를 한 번에 `copyFrom(...)`
- body 길이만 `writeIntBe(...)`로 덮어쓰기

로 정리했다.

이건 benchmark 전용 꼼수라기보다,
일반적인 `Message` 읽기/쓰기 helper를 보강한 쪽이다.

### 7. 응답 `Message` 재사용은 현재 계약에서 안전하지 않았다

한 번은 `ThreadLocal<Message>`로 응답 `Message`를 재사용하는 실험도 했다.

아이디어는:

- callback마다 새 `Message`를 만들지 않고
- `reset(size)`로 payload만 다시 잡고
- 같은 Java `Message` 객체를 계속 쓰는 방식이었다.

하지만 실제 benchmark에서는 서버가 조용히 실패했다.
즉 send 성공 뒤 native 쪽 수명 규칙과 Java 재사용 시점이
지금 계약에서는 완전히 맞지 않는다고 봐야 한다.

따라서 "응답 `Message` 재사용"은 현재 API 계약 위에서는 안전한 일반 해법이
아니고, 이 실험은 제거했다.

## 틀렸던 가설

### 1. FFM copy가 느릴 것이라는 가설

틀렸다.

copy 자체는 충분히 빨랐다.
실제 병목은 copy보다 callback과 `Message` 객체화, 그리고 출력 경로 쪽이었다.

### 2. `msg_t` 메모리 풀링이 큰 효과를 줄 것이라는 가설

틀렸다.

작은 `msg_t` 슬롯 풀이나 `Arena` 재사용은 일부 비용을 줄일 수는 있지만,
현재 격차를 설명할 핵심은 아니었다.

native 쪽 `msg_t` 경로는 이미 꽤 최적화되어 있고,
여기서 얻는 이득은 제한적이었다.

### 3. outbound builder `Message` 경로가 더 자연스럽고 빠를 것이라는 가설

틀렸다.

`new Message(size)` 후 native payload에 직접 쓰는 방식은
현재 구현에서는 `copyOf(byte[])`보다 느리다.

즉 "정석처럼 보이는 경로"가 실제로는 더 무거웠다.

## 추가로 시도했지만 큰 효과가 없던 것

아래 수정이나 실험은 end-to-end 기준으로 큰 개선을 만들지 못했다.

- thread-local 응답 `Message` 재사용
- `Unsafe.copyMemory`로 일부 경로 치환
- `new Message(size)` 기반 response builder
- `copyFrom(Message)` 기반 직접 조립

이 중 일부는 microbench에서 소폭 차이는 있었지만,
실제 `jvmzlink` 요청-응답 경로의 큰 병목을 깨진 못했다.

추가로 확인한 점:

- payload address cache 이후에는 builder microbench가 좋아졌지만,
  실제 `with_stream` 서버에서 builder 경로는 다시 더 느렸다.
- 즉 `microbench상 더 나아 보이는 builder 경로`가
  실제 callback/request-response 흐름에서는 반드시 더 좋지 않았다.
- 그래서 현재 server best-known 경로는 여전히
  `byte[] reply -> Message.copyOf(...) -> send(Message)` 쪽이다.

## `.NET`과의 비교에서 남는 차이

`.NET` trace에서는 callback `Message` materialization 비용은 적지 않았다.
하지만 send 쪽은 상대적으로 얇았다.

반면 Java는:

- inbound `Message` callback materialization
- outbound `Message` 생성
- `transferTo/send`

이 세 구간의 합이 더 두껍게 나타난다.

현재까지 확인한 바로는,
Java가 `.NET`보다 느린 이유를 "FFM가 느려서"라고 설명하는 것은 맞지 않다.

더 정확한 설명은 다음과 같다.

- Java binding의 `Message` 모델이 작고 빈번한 요청-응답 경로에서
  여전히 무겁다
- 특히 입력 wrapper와 출력 `Message` 생성 경로의 고정비가 크다

## 현재 결론

현재 단계에서 가장 신뢰할 수 있는 결론은 아래와 같다.

1. `FFM copy`는 주병목이 아니다.
2. callback 입력의 `fromOwnedNative(...)`는 실제 병목이었고, 이미 고쳤다.
3. single-part `send(Message)`의 불필요한 `msgMove`도 실제 병목이었고, 이미 줄였다.
4. payload pointer를 wrapper로 캐시하던 비용도 실제로 있었고, address cache로 줄였다.
5. confined arena 제거와 `u32 rid` send bridge도 실제 병목 일부였고, 추가로 줄였다.
6. 남은 큰 비용은 여전히 callback에서의 `Message` 객체 생성과 outbound `Message` 경로다.
7. builder 경로는 이제 `copyOf(byte[])` 경로와 거의 비슷하거나 약간 더 나은 수준까지 왔지만, `.NET` 수준과의 큰 차이는 그대로 남아 있다.
8. 즉 Java binding의 일반적인 `Message` 모델 자체를 더 근본적으로 다시 볼 필요가 남아 있다.

## 현재까지의 end-to-end 변화

`with_stream` 64B, `jvmzlink`, short run 기준으로 보면:

- 아주 초기 상태: 대략 `~90-110 Kops/s`
- `fromOwnedNative(...)` 개선 후: `~110 Kops/s`
- single-part send direct path 후: `~120 Kops/s`
- payload address cache, owned-slot direct allocation, `u32 rid` send bridge 이후:
  대략 `~120-135 Kops/s`
- 수동 장시간 부하 기준 최신 측정: 대략 `131.76 Kops/s`

즉 분명히 개선은 되었지만,
여전히 `netzlink`의 `~430 Kops/s` 수준과는 큰 차이가 남아 있다.

이 차이는 이제 메모리 copy나 `msg_t` 할당이 아니라,
Java binding의 `Message` 객체화와 callback/send 경계 모델 차이로 보는 것이 맞다.

## recv 버전 비교

callback 경계가 정말 큰 원인인지 분리하려고
`with_stream`에 `jvmzlink-recv` 스택을 추가해서 같은 `64B` 조건으로 비교했다.

결과:

- `jvmzlink` packet callback 버전: `150.46 Kops/s`
- `jvmzlink-recv` 버전: `17.62 Kops/s`

리포트:

- `core/bench/with_stream/results/20260418_230402/comparison_size64.md`

이 수치만 보면 Java binding에서는 `recv()` 경로가 callback 경로보다 훨씬 불리하다.

중요한 이유는 두 가지다.

### 1. Java `recv()`는 generic multipart 경로를 그대로 탄다

`MessagePlane.recv()`는:

- `Native.recvMultipart()`
- `Socket.toRoutingId(...)`
- `Message.fromOwnedMsgVector(...)`
- `new Received(...)`

를 매번 수행한다.

즉 STREAM 전용 얇은 경로가 아니라,
generic routed multipart 수신 경로를 그대로 사용한다.

### 2. recv 서버는 user-space framing 비용까지 함께 낸다

현재 `jvmzlink-recv` 서버는 raw chunk를 받아서 Java에서 다시 frame을 조립한다.

즉 다음 비용이 같이 들어간다.

- raw chunk copy (`Message.data()`)
- routing id 기준 buffer map 관리
- frame prefix 파싱
- 완성 frame을 다시 `Message.copyOf(...)`로 생성

그래서 이 버전은 callback 경계 비교에는 쓸 수 있지만,
실제 성능 기준선으로는 적합하지 않다.

## recv 경로에서 확인한 추가 포인트

`Message.fromOwnedMsgVector(...)`는 처음 봤을 때
callback의 `fromOwnedNative(...)`와 비슷하게 줄일 수 있을 것처럼 보였다.

실제로 reusable miss 시:

- `new Message(Arena.ofShared(), false)`
- `zlink_msg_init`
- `zlink_msg_move`

를 수행하고 있었다.

그래서 이 경로를 raw-owned `msg_t` slot 기반으로 바꿔 보았다.

하지만 `jvmzlink-recv 64B`는:

- 변경 전: `17.62 Kops/s`
- 변경 후 단독 재측정: `14.76 Kops/s`

로 좋아지지 않았다.

리포트:

- `core/bench/with_stream/results/20260418_230825/comparison_size64.md`

이 결과는 recv 버전 병목이
`Message.fromOwnedMsgVector()` 한 군데보다 훨씬 더 넓게 퍼져 있다는 뜻이다.

즉 recv 경로의 큰 비용은:

- generic multipart recv materialization
- `RoutingId` / `Received` 생성
- user-space framing / reassembly

가 합쳐진 결과로 보는 편이 맞다.

## 다음 단계

다음 최적화는 미세한 copy 치환보다 아래 순서로 가는 것이 맞다.

### 1. 실제 send bridge를 분리해서 측정

지금은 `transferTo` 전후까지는 어느 정도 봤다.
다음은 실제 native send까지 포함해서:

- callback 이후
- 응답 생성
- `transferTo`
- 실제 send

를 한 축으로 더 쪼개야 한다.

### 2. `Message`의 입력 모델과 출력 모델을 분리해서 생각

현재 `Message`는:

- callback 입력 wrapper
- 응답 생성 객체
- send payload carrier

역할을 모두 맡는다.

이 역할이 하나의 무거운 모델에 묶여 있어서 작은 메시지 경로에서 불리하다.

공개 API는 유지하더라도,
내부 표현은 최소한 아래 두 방향으로 나눠서 다시 봐야 한다.

- adopted-native inbound `Message`
- outbound response `Message`

### 3. builder 경로를 다시 설계

현재 `new Message(size) + copyFrom(...)`는 느리다.
이건 단순히 "더 최적화하면 빨라질 것"으로 보기보다,
현재 API shape와 내부 동작이 맞지 않는다고 보는 편이 맞다.

## 참고한 코드와 측정 도구

실험 중 추가한 파일:

- `bindings/java/perf/src/main/java/dev/kairoscode/zlink/perf/MemoryInteropMicrobench.java`
- `bindings/java/perf/src/main/java/dev/kairoscode/zlink/MessagePathMicrobench.java`
- `bindings/java/src/test/java/dev/kairoscode/zlink/perf/MessageOutboundMicrobench.java`

실제로 수정한 핵심 구현:

- `bindings/java/src/main/java/dev/kairoscode/zlink/Message.java`

## 요약

이번 조사에서 확실하게 알게 된 것은 다음 두 가지다.

- `FFM` 자체를 의심하는 방향은 주된 답이 아니다.
- 진짜 문제는 Java binding의 `Message` 경로다.

특히 작은 요청-응답 성능을 끌어올리려면,
입력 callback wrapper와 출력 `Message` 생성 경로를 따로 보고
다시 설계해야 한다.

## 2026-04-18 추가 진행: netzlink, jvmzlink multi 64B

이번 턴에는 STREAM만 보지 않고,
binding perf의 multi `router-router`, `dealer-router` 64B도
같이 보면서 `.NET`과 `Java` 바인딩을 다시 줄였다.

기준으로 본 core 수치는 다음이다.

- `core MULTI_ROUTER_ROUTER tcp 64B`
  - `371.62 Kops/s`
  - `core/perf/results/multi/report/perf_core_multi_linux_20260418_232652.txt`
- `core MULTI_DEALER_ROUTER tcp 64B`
  - `350.09 Kops/s`
  - `core/perf/results/multi/report/perf_core_multi_linux_20260418_232827.txt`

### .NET 쪽에서 남긴 변경

#### 1. `RoutingId`의 recv/send 왕복 복사 줄이기

이전 `.NET` 경로는 routed recv에서:

- native `routingId`
- `byte[]` 생성
- `RoutingId.FromBytes(...)`

를 타면서 다시 한 번 복사했고,
send에서는 `RoutingId.ToByteArray()`로 새 배열을 다시 만들었다.

이번에 바꾼 내용은 다음과 같다.

- recv에서 native `routingId`를 읽어 `byte[]`를 한 번만 만들고
  `RoutingId`가 그 배열을 그대로 들게 변경
- send에서 `RoutingId` 내부 backing `byte[]`를 그대로 사용하도록 변경
- `RoutingId`에 hash cache 추가

관련 파일:

- `bindings/dotnet/src/Zlink/RoutingId.cs`
- `bindings/dotnet/src/Zlink/RoutingIdCodec.cs`

#### 2. `Received.Parts` single-part 래퍼 할당 제거

이전 `Received`는 single-part fast path가 있어도
`Parts`를 처음 읽는 순간 다시 1개짜리 배열을 만들고 있었다.

이건 multi `router-router`와 `dealer-router`처럼
single-part가 계속 오는 패턴에서는 매 수신마다 반복되는 비용이다.

그래서 `Received` 자체를 `IReadOnlyList<Message>`로 바꾸고,
`Parts`는 `this`를 돌려주게 바꿨다.

관련 파일:

- `bindings/dotnet/src/Zlink/Received.cs`

#### 3. .NET 64B 결과

이번 턴에서 남긴 최종 기준은 다음이다.

- `netzlink MULTI_ROUTER_ROUTER tcp 64B`
  - `107.61 Kops/s`
  - `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260418_233926.txt`
- `netzlink MULTI_DEALER_ROUTER tcp 64B`
  - `65.58 Kops/s`
  - `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260418_234130.txt`

정리하면, `.NET`은 routed recv/send에서
`RoutingId`와 `Received` 할당을 줄여도
아직 core의 90%와는 거리가 멀다.
즉 남은 큰 병목은 더 깊은 recv/send bridge 쪽이다.

### Java 쪽에서 남긴 변경

#### 1. generic recv single-part fast path 유지

이번 턴 전까지 이미 넣어 둔 변경은 다음과 같다.

- `recv()` / `recvNoWait()`에서 single-part면
  `Message[]`와 `Received.parts()`용 배열 경로를 피함
- `Received`는 single-part를 별도 필드로 유지
- `Message.copyOf(Message)`, `readShortBe()`, `readIntBe()`,
  range `copyTo(...)` 추가

관련 파일:

- `bindings/java/src/main/java/dev/kairoscode/zlink/Message.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/Received.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/MessagePlane.java`

#### 2. multi perf에서 single-message send 경로 사용

multi `router-router`, `dealer-router` 서버와 클라이언트는
실제로는 single-part만 주고받는데도
`List.of(reply)`, `List.of(request)`를 계속 만들고 있었다.

이건 binding 자체보다 사용 패턴 비용이라서,
다음처럼 바꿨다.

- server reply: `server.send(routingId, reply)`
- client send: `client.send(..., request, SendFlags.DONT_WAIT)`

관련 파일:

- `bindings/java/perf/multi/Zlink.BindingBench.Multi/src/main/java/dev/kairoscode/zlink/perf/multi/PerfMultiRouterRouter.java`
- `bindings/java/perf/multi/Zlink.BindingBench.Multi/src/main/java/dev/kairoscode/zlink/perf/multi/PerfMultiDealerRouter.java`

#### 3. Java 64B 결과

이번 턴에서 남긴 최종 기준은 다음이다.

- `jvmzlink MULTI_ROUTER_ROUTER tcp 64B`
  - `143.41 Kops/s`
  - `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260418_234537.txt`
- `jvmzlink MULTI_DEALER_ROUTER tcp 64B`
  - `159.63 Kops/s`
  - `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260418_234548.txt`

즉 Java는 이번 단계에서:

- `router-router`: `128.84 -> 143.41 Kops/s`
- `dealer-router`: `131.37 -> 159.63 Kops/s`

수준까지는 올랐다.

하지만 여전히 core 64B와 비교하면:

- `router-router`: 약 `38.6%`
- `dealer-router`: 약 `45.6%`

정도라서, 아직 큰 차이가 남아 있다.

### 이번 턴에서 버린 시도

아래 시도는 실제 수치 개선이 없어서 남기지 않았다.

- `.NET` multi echo에서 `Message.Copy()` 기반 shallow clone로 바로 응답하는 경로
  - 오히려 throughput이 내려갔다.
- Java public `recvNoWaitOrNull()` 경로 추가
  - `Optional<Received>`를 빼도 실제 multi 수치는 좋아지지 않았다.

### 이번 턴 요약

이번 턴에서 확실해진 것은 다음이다.

- `.NET`은 `RoutingId` 왕복과 `Received` single-part 래퍼 비용을 줄여도
  아직 recv/send bridge의 더 깊은 비용이 남아 있다.
- Java는 generic recv와 single-message send를 직접 타게 만들면
  실제 64B multi 수치가 분명히 올라간다.
- 하지만 두 바인딩 모두 아직 core perf에 근접했다고 보긴 어렵다.

즉 다음 단계는 여전히:

- `.NET`: routed recv/send bridge의 깊은 병목 확인
- `jvmzlink`: `Message`와 generic recv/send 경계의 추가 비용 확인

으로 가는 것이 맞다.

## 2026-04-19 추가 정리

이번에는 perf 정책을 다시 엄격하게 맞췄다.

- `bindings/<lang>/perf`는 `doc/spec/bindings`에 적힌 public API만 사용해야 한다.
- perf 코드에서 internal helper를 쓰면 수치가 높게 나와도 정책 위반이다.

이 기준으로 다시 확인한 결과, Java multi perf의 `recvNoWait()` 계열은
spec 문서에 없는 표면이라 `router-router`, `dealer-router`에서
`recv(RecvFlags.DONT_WAIT)`만 쓰도록 다시 맞췄다.

관련 파일:

- `bindings/java/perf/multi/Zlink.BindingBench.Multi/src/main/java/dev/kairoscode/zlink/perf/multi/PerfMultiRouterRouter.java`
- `bindings/java/perf/multi/Zlink.BindingBench.Multi/src/main/java/dev/kairoscode/zlink/perf/multi/PerfMultiDealerRouter.java`

### public-only 기준 재측정

- `jvmzlink MULTI_ROUTER_ROUTER tcp 64B`
  - `151.86 Kops/s`
  - `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260419_004308_rrpublic.txt`
- `jvmzlink MULTI_DEALER_ROUTER tcp 64B`
  - `153.88 Kops/s`
  - `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260419_004320_drpublic.txt`

이 수치는 이전 internal helper 기반 수치와 완전히 같지는 않지만,
적어도 정책을 어기지 않는 public 경로에서도 Java binding이
`150 Kops/s`대까지는 나온다는 점을 확인했다.

### .NET 쪽 추가 확인

`.NET`은 `Message`의 byte input 경로를 더 얇게 바꿨다.

- `Message.From(...)`로 만든 message는 managed copy를 유지하고
  single-part send에서 native `msg_t` move 대신 borrowed send를 탈 수 있게 함
- `Message.Move()` / `Message.Copy()`도 managed-backed 상태를 유지하도록 조정

관련 파일:

- `bindings/dotnet/src/Zlink/Message.cs`
- `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`

하지만 latest public-only multi 64B는 여전히 낮다.

- `netzlink MULTI_ROUTER_ROUTER tcp 64B`
  - `60.00 Kops/s`
  - `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260419_003947.txt`
- `netzlink MULTI_DEALER_ROUTER tcp 64B`
  - `61.50 Kops/s`
  - `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260419_003947.txt`

즉 `.NET`은 지금 단계에서:

- `RoutingId` 복사 축소
- `Received` single-part 경량화
- `Message` managed-backed single-part send

까지 반영했는데도, core 대비 격차가 매우 크다.

### 현재 판단

이 시점에서 가장 중요한 판단은 다음 두 가지다.

1. `FFM`/native copy 자체가 전부의 원인은 아니다.
   Java도 public-only 기준 `150 Kops/s`대가 나온다.
2. `.NET`의 낮은 수치는 단순 message copy 한 군데보다는
   public contract 전체, 특히 nonblocking recv/send와 routed recv/send bridge
   조합에서 생기는 비용일 가능성이 크다.

즉 다음 단계는 여전히:

- `.NET`: public nonblocking recv/send와 routed bridge 비용을 더 잘게 분해
- Java: public recv 경로에서 `Received`/`Message` materialization을 추가로 축소

로 가는 것이 맞다.
