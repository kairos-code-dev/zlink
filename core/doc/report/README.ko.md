# zlink 보안·버그 검토 리포트 인덱스

- **대상**: `core/` 런타임 + `framework/languages/{cpp,node,java,dotnet}` 프레임워크 계층 + `bindings/{c,cpp,dotnet,go,java,node,python,rust}` 바인딩 라이브러리
- **방식**: 영역별 병렬 정밀 리뷰(신뢰 불가 입력 파싱·역직렬화 · 네이티브 interop · 동시성 · 리소스 해제 우선)
- **상태**: 실행 순서의 모든 항목 종결.

> Core와 framework 리포트는 Codex 교차검증 결과를 본문에 포함한다.
> 바인딩 리포트는 실제 바인딩 코드와 Codex 에이전트 리뷰 결과를 다시 대조해 보강했다. 이전 Claude 리뷰 기록은 당시 완료 항목의 검증 결과로 보존한다.
> 실행 순서는 [2026-06-14-security-review-execution-plan.ko.md](2026-06-14-security-review-execution-plan.ko.md)에 정리한다.

## 인덱스 확인 기록

- 2026-06-14: README와 실행 순서 문서의 `.ko.md` 링크가 모두 실제 파일로 존재함을 확인했다.
- 2026-06-14: 각 리포트 요약과 README의 최고 심각도, 교차언어 공통 결함, 처리 우선순위를 대조했다.
- 2026-06-14: 공통 #2 LZ4 압축 폭탄 심각도를 Medium으로 바로잡고, Java와 .NET의 framework-core 중복 위치를 표에 함께 적었다.
- 2026-06-14: 코드 변경 없음. core runtime과 public header 변경이 없어 `bindings/dev_sync_local_core_libs.sh`는 실행 대상이 아니다.
- 2026-06-14: Claude 리뷰에서 README 인덱스 확인 항목에 대해 "추가 이슈 없음" 판정을 받았다.
- 2026-06-14: Node/TS framework 원격 DoS 항목(C1, C2, S1, S2)을 수정하고, build·targeted node test·typecheck 통과와 Claude "추가 이슈 없음" 판정을 확인했다.
- 2026-06-14: Java framework 원격 DoS 항목(F1, F2)을 수정하고, stream-connector·framework-core test 통과와 Claude "추가 이슈 없음" 판정을 확인했다.
- 2026-06-14: .NET framework 원격 DoS 1차 항목(D1, D2, D5)을 수정하고, stream-connector test와 framework LZ4 회귀 테스트 통과 및 Claude "추가 이슈 없음" 판정을 확인했다. D3/D4는 실행 순서 5-4에서 계속 처리한다.
- 2026-06-14: C++ framework 인증·HTTP 항목(CR2, H1)을 수정하고, HTTP client test·contract header test 통과 및 Claude "추가 이슈 없음" 판정을 확인했다.
- 2026-06-14: Java framework TLS 항목(F3, F5)을 수정·문서화하고, stream-connector test 통과 및 Claude "추가 이슈 없음" 판정을 확인했다.
- 2026-06-14: C++ framework Unreal 동시성·teardown 항목(H2/H3)을 수정하고, stream connector·Unreal connector·contract header test 통과 및 Codex 에이전트 "추가 이슈 없음" 판정을 확인했다. layout contract 실행은 SPOT timer 문서 문구 불일치로 실패했으며 이번 Unreal 변경 경로와는 별도다.
- 2026-06-14: Java framework handler list 동시성 항목(F4)을 수정하고, stream-connector test 통과 및 Codex 에이전트 "추가 이슈 없음" 판정을 확인했다.
- 2026-06-14: Node/TS framework JSON 입력 검증 후속 항목(C3/C4)을 수정하고, S3 타이머 누수 의심은 코드 대조 결과 보안 이슈가 아니므로 종결했다. build·typecheck·Node runtime gate 통과 및 Codex 에이전트 "추가 이슈 없음" 판정을 확인했다.
- 2026-06-14: .NET framework D3/D4 수신 메시지 보관소와 Manual dispatch callback queue에 bounded 정책을 추가하고, stream connector test 통과 및 Codex 에이전트 "추가 이슈 없음" 판정을 확인했다.
- 2026-06-14: core mtrie 재귀 소멸·순회 항목(#1)을 비재귀화하고 core/C++ binding 검증 통과 및 Claude "추가 이슈 없음" 판정을 확인했다. 당시 C binding 검증은 별도 C-BINDING-001 버전 매크로 불일치 때문에 실패했다.
- 2026-06-14: core 포트·zone id 파싱(#3)과 message/send API 가드(#6)를 수정하고 core/C++ binding 검증 통과 및 Claude "추가 이슈 없음" 판정을 확인했다. 당시 C binding 검증은 별도 C-BINDING-001 버전 매크로 불일치 때문에 실패했다.
- 2026-06-14: core IPC bind의 검증 전 unlink 항목(#4)을 수정하고 core/C++ binding 검증 통과 및 Claude "추가 이슈 없음" 판정을 확인했다. 당시 C binding 검증은 별도 C-BINDING-001 버전 매크로 불일치 때문에 실패했다.
- 2026-06-14: core WS/WSS buffering 항목(#2)의 `pending_message` 전체 사본을 제거하고 core/C++ binding 검증 통과 및 Claude "추가 이슈 없음" 판정을 확인했다. 당시 C binding 검증은 별도 C-BINDING-001 버전 매크로 불일치 때문에 실패했다.
- 2026-06-14: core decoder allocator 산술 오버플로 항목(#5)을 수정하고 core/C++ binding 검증 통과 및 Codex 에이전트 "추가 이슈 없음" 판정을 확인했다. 당시 C binding 검증은 별도 C-BINDING-001 버전 매크로 불일치 때문에 실패했다.
- 2026-06-14: core `maxmsgsize` 정책 항목(#7)은 기본값을 유지하고, 신뢰할 수 없는 listener에서 `ZLINK_OPT_MAXMSGSIZE`를 명시하도록 guide와 site 문서를 보강했다.
- 2026-06-14: core command body length clamp 항목(#9)을 수정하고 core 빌드·단위 테스트 통과 및 Codex 에이전트 "추가 이슈 없음" 판정을 확인했다. 당시 C++ binding 검증은 병렬 binding parity 변경의 `spot_node_t` 테스트 컴파일 오류로 실패했고, C binding 검증은 별도 C-BINDING-001 버전 매크로 불일치 때문에 실패했다.
- 2026-06-14: core IPC 주소 길이 방어 항목(#10)을 수정하고 core/C++ binding 검증 통과 및 Codex 에이전트 "추가 이슈 없음" 판정을 확인했다. 당시 C binding 검증은 별도 C-BINDING-001 버전 매크로 불일치 때문에 실패했다.
- 2026-06-14: 바인딩 6-1부터 6-8까지 모두 종결했다. C-BINDING-001은 `core/include/zlink/common.h`와 C 바인딩 `common.h`의 patch 값을 `6.0.4`로 맞춰 해결했고, core 공개 헤더 변경 후 `bindings/dev_sync_local_core_libs.sh`와 Python 바인딩 tests/samples로 동기화 검증을 마쳤다.

## 리포트 목록

### Core 런타임
| 리포트 | 파일 | 최고 심각도 |
|--------|------|-------------|
| Core C++ 런타임 (libzmq 파생) | [2026-06-13-core-src-security-review.ko.md](2026-06-13-core-src-security-review.ko.md) | High (mtrie 재귀 → 스택 오버플로 DoS) |

### Framework 계층 (언어별)
| 언어 | 파일 | 최고 심각도 |
|------|------|-------------|
| C++ | [2026-06-14-cpp-framework-security-review.ko.md](2026-06-14-cpp-framework-security-review.ko.md) | 추가 보안 수정 없음 (쿠키 경로, HTTP 압축 해제 상한, retry 정책, spot/actor 동기화, Axmol/Godot 수명·dispatcher, stream closed flag는 2026-06-14 수정 완료. M2/M5/L4는 코드 대조로 반박 종결) |
| Node/TS | [2026-06-14-node-framework-security-review.ko.md](2026-06-14-node-framework-security-review.ko.md) | 추가 보안 수정 없음 (인바운드/WS DoS, LZ4, JSON 입력 검증은 2026-06-14 수정 완료. S3 타이머 누수 의심은 보안 이슈 아님) |
| Java | [2026-06-14-java-framework-security-review.ko.md](2026-06-14-java-framework-security-review.ko.md) | 추가 보안 수정 없음 (부록의 codec naming 정확성 후보는 별도 품질 항목) |
| .NET | [2026-06-14-dotnet-framework-security-review.ko.md](2026-06-14-dotnet-framework-security-review.ko.md) | 추가 보안 수정 없음 (인바운드/WS/LZ4 DoS와 수신 메시지·callback queue 한도는 2026-06-14 수정 완료) |

### 바인딩 라이브러리 (언어별)
| 언어 | 파일 | 최고 심각도 |
|------|------|-------------|
| C | [2026-06-14-bindings-c-security-review.ko.md](2026-06-14-bindings-c-security-review.ko.md) | 추가 보안 수정 없음 (버전 매크로 불일치 수정 완료) |
| C++ | [2026-06-14-bindings-cpp-security-review.ko.md](2026-06-14-bindings-cpp-security-review.ko.md) | 추가 보안 수정 없음 (C++ contract/sample smoke 통과) |
| .NET | [2026-06-14-bindings-dotnet-security-review.ko.md](2026-06-14-bindings-dotnet-security-review.ko.md) | 추가 보안 수정 없음 (native library 로딩 경계 문서화, 메시지 크기 변환 정책 수정 완료) |
| Go | [2026-06-14-bindings-go-security-review.ko.md](2026-06-14-bindings-go-security-review.ko.md) | 추가 보안 수정 없음 (`Message.Data()` slice 수명 규칙 문서화와 회귀 테스트 추가 완료) |
| Java | [2026-06-14-bindings-java-security-review.ko.md](2026-06-14-bindings-java-security-review.ko.md) | 추가 보안 수정 없음 (native library 로딩 경계와 Windows DLL 검색 전제 문서화 완료) |
| Node | [2026-06-14-bindings-node-security-review.ko.md](2026-06-14-bindings-node-security-review.ko.md) | 추가 보안 수정 없음 (callback handler slot 제한 문서화 완료) |
| Python | [2026-06-14-bindings-python-security-review.ko.md](2026-06-14-bindings-python-security-review.ko.md) | 추가 보안 수정 없음 (Windows DLL 검색 경계와 native view 수명 규칙 문서화 완료) |
| Rust | [2026-06-14-bindings-rust-security-review.ko.md](2026-06-14-bindings-rust-security-review.ko.md) | 추가 보안 수정 없음 (`Context` thread-safety 계약 문서화와 회귀 테스트 추가 완료) |

> 아래 §교차언어 공통 결함은 **framework 4개 언어** 한정 분석이다. Core 런타임은 별도 트러스트 모델(직접 와이어 디코드)이라 위 core 리포트를 참조.

### Codex 교차검증 주요 정정 (framework)
- **C++ H4**(executor drain race): **반박됨** — `_stopping`이 `try_submit`과 동일 뮤텍스 하라 UAF 아님. High→Low 격하.
- **Node C3/C4**(프로토타입 오염): **과장** — `JSON.parse`가 병합 sink 없이는 오염 미입증. "신뢰 불가 JSON 미검증"으로 하향.
- **.NET S1/S2**(CTS/pending 누수): **둘 다 오탐** — disposal 멱등·caller catch가 정리. 제외.
- **Java**: F1~F4 전부 확인. + **F2 LZ4 폭탄이 framework-core에도 중복**, **TLS 인증서 전체 신뢰 opt-in footgun**(F5) 추가.
- **공통 #2 범위 확대**: LZ4 압축 폭탄이 connector 코덱뿐 아니라 **Node·Java·.NET 모두 framework-core 계층에도 중복** 존재(Node `framework/.../streams/protocol.ts:221`, Java `zlink-framework-core/.../ZLinkStreamLz4Pickler.java:51`, .NET `ZLinkStreamPacketPayloadCodec.cs:21` — 수정 시 두 곳).

---

## ★ 교차언어 공통 결함 (4개 언어 전부)

리뷰의 가장 중요한 발견은 **동일한 아키텍처 결함이 4개 언어 프레임워크에 그대로 복제**되어 있다는 점이다.

### 공통 #1 — 수신측 프레임/페이로드 크기 상한 부재 → 무제한 할당 DoS (전 언어 High~Critical)

stream-connector 와이어 프레임은 6바이트 prefix(`header_size` u16 + `payload_size` **u32**)를 가진다.
**송신 경로는 `maxSendPayloadSize`(기본 64KiB)를 강제**하지만, **디코드 경로는 u32 길이를 그대로 신뢰**해 검증 없이 버퍼를 할당한다.

| 언어 | 위치 | 증상 |
|------|------|------|
| C++ | `connector/core/src/runtime/.../zlink_stream_calls.cpp:235`, `stream_connection.cpp:351`, `:307` | 2026-06-14 `max_receive_payload_size` 검증으로 수정 완료 |
| Node | `stream-connector/.../NodeDuplexStreamConnection.ts:65`, `WebSocketFrameCodec.ts:29` | 2026-06-14 `maxReceivePayloadSize` 검증으로 수정 완료 |
| Java | `ZLinkTcpTransportConnection.java:33`, `ZLinkTlsTransportConnection.java:191` | 2026-06-14 `maxReceivePayloadSize` 검증으로 수정 완료 |
| .NET | `ZlinkStreamFrameCodec.cs:96`, `WebSocketConnection.cs:35` | 2026-06-14 `MaxReceivePayloadSize` 검증으로 수정 완료 |

**트리거**: 악의적/탈취된 서버(또는 평문 `tcp://`/`ws://`의 MITM)가 prefix에 거대한 `payload_size`를 실어 보냄 → 클라이언트 OOM/크래시.
**근본 원인**: `frame_codec`의 크기 검증이 **encode 전용**으로만 호출되고 decode에서는 호출 안 됨.
**공통 수정**: `maxReceivePayloadSize`(또는 `maxSend*` 재사용)를 모든 transport decode 경로에 주입, 할당 **이전에** `header+payload`(64비트 산술)를 상한과 비교·거부. 누적 버퍼·WS 단편 조립·메시지 큐에도 cap.

### 공통 #2 — LZ4 압축 폭탄 (Node/Java/.NET Medium, C++는 L1로 별도)

LZ4 unpickle이 **압축 헤더의 attacker-제어 original-length**로 출력 버퍼를 **먼저 할당**한다(폭탄 증폭).

| 언어 | 위치 |
|------|------|
| Node | `ZlinkStreamCompressionCodec.ts:64`, `framework/.../streams/protocol.ts:221` (2026-06-14 출력 길이 상한 적용 완료) |
| Java | `ZLinkStreamLz4Pickler.java:53`, `zlink-framework-core/.../ZLinkStreamLz4Pickler.java:51` (2026-06-14 출력 길이 상한 적용 완료) |
| .NET | `ZlinkStreamLz4CompressionCodec.cs:10`, `ZLinkStreamPacketPayloadCodec.cs:21` (2026-06-14 출력 길이 상한 적용 완료) |
| C++ | (lz4 codec 자체는 `LZ4_decompress_safe`로 안전, HTTP gzip/deflate 해제 결과도 `max_response_body_size`로 제한하도록 수정 완료) |

**수정**: 할당 전 `resultLength`를 max-decompressed-size로 clamp.

---

## 관리언어 역직렬화 — 양호 확인 (오탐 방지)

흔한 RCE 가젯은 **전 언어에서 비활성**임을 명시적으로 확인:
- **Java**: Jackson default typing OFF(`enableDefaultTyping`/`@JsonTypeInfo` 부재), `ObjectInputStream` 부재, SpEL/EL 부재.
- **.NET**: `BinaryFormatter`/`NetDataContractSerializer`/`LosFormatter` 부재, STJ 폴리모피즘 부재, MessagePack `Typeless` 미사용, protobuf 기본 recursion/size limit 유지.
- **Node**: `eval`/`new Function`/`child_process` 부재, msgpack/protobuf는 검증된 라이브러리 위임. 단 JSON 헤더 파싱은 신뢰 불가 입력의 구조와 enum 검증이 필요하다(Node 리포트 C3/C4).

**주의(Java 정확성)**: `ZLinkStreamMessagePack`/`ZLinkStreamProtobuf` 코덱이 실제로는 **JSON `JsonMapper`를 사용**(msgpack/protobuf 아님). 와이어 `codec` 태그만 다름 → protobuf의 recursion/size 보호가 적용되지 않음. 의도된 placeholder인지 확인 필요.

---

## 처리 우선순위 (교차언어)

1. **공통 #1** — 4개 언어 모두 `maxReceivePayloadSize` 도입 + decode 경로 강제. 단일 수정으로 주 OOM 벡터 제거. **원격 트리거 가능, 전 언어 영향.**
2. **공통 #2** — LZ4 출력 길이 clamp(Node/Java/.NET).
3. **C++ 전용 보안** — 리다이렉트 시 `Authorization` 스트립(CR2), HTTP body_limit(H1)은 2026-06-14 수정 완료.
4. **Java 전용** — Netty TLS 호스트명 검증(`endpointIdentificationAlgorithm("HTTPS")`, F3)은 2026-06-14 수정 완료.
5. 언어별 동시성/리소스 항목(각 리포트 참조).

## 바인딩 처리 우선순위

1. **native library 로딩 경계 문서화와 제한** — .NET, Java, Python의 환경 변수·PATH 기반 로딩은 신뢰된 실행 환경을 전제로 한다. 배포 문서와 보안 모드에서 먼저 정리한다.
2. **native buffer view 수명 문서화와 테스트** — Go `Message.Data()`, Python `memoryview`, Rust native slice 노출 규칙을 공개 계약과 테스트로 고정한다.
3. **C 버전 매크로 불일치 수정** — `bindings/c/include/zlink/common.h`의 기본 patch 값을 현재 공개 버전과 맞춘다.
4. **Node callback slot 제한** — 현재 제한을 공개 문서에 적고, 실제 사용자가 8개를 넘는 handler를 요구하면 동적 slot 구조로 바꾼다.
