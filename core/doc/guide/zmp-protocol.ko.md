[한국어](zmp-protocol.ko.md)

[가이드 목록](README.ko.md)

# ZMP 프로토콜 레퍼런스

ZMP(zlink Message Protocol)는 zlink가 wire 위에서 쓰는 프레이밍 프로토콜이다. 이
문서는 **새 언어 바인딩을 작성하거나 다른 시스템과 interop**하려는 독자를 위한
사용자용 요약이다. 비트 단위 정식 정의와 인코딩/디코딩 흐름은
[internals/protocol-zmp](../internals/protocol-zmp.ko.md)가 소유한다.

> **ZMP는 ZMTP(ZeroMQ 프로토콜)와 호환되지 않는 별개 프로토콜이다.** 이름이나
> 표면이 비슷해 보여도 wire 형식이 달라 ZMTP 구현과 직접 통신할 수 없다
> ([internals/design-decisions](../internals/design-decisions.ko.md)).

---

## 1. 공통 프레임 헤더 (8바이트 고정)

모든 프레임은 8바이트 고정 헤더로 시작한다.

```text
 Byte:   0         1         2         3         4    5    6    7
      +---------+---------+---------+---------+---------------------+
      |  MAGIC  | VERSION |  FLAGS  |RESERVED |   PAYLOAD SIZE      |
      |  (0x5A) |  (0x01) |         | (0x00)  |   (32-bit BE)       |
      +---------+---------+---------+---------+---------------------+
```

| 필드 | 오프셋 | 크기 | 값 |
|------|--------|------|-----|
| MAGIC | 0 | 1B | `0x5A` (ASCII 'Z') |
| VERSION | 1 | 1B | `0x01` |
| FLAGS | 2 | 1B | 프레임 플래그(아래) |
| RESERVED | 3 | 1B | `0x00` |
| PAYLOAD SIZE | 4–7 | 4B | payload 길이, **Big Endian** 32비트 |

멀티바이트 정수는 Big Endian이다.

## 2. FLAGS 비트

| 비트 | 이름 | 값 | 의미 |
|------|------|-----|------|
| 0 | MORE | `0x01` | 멀티파트 — 뒤에 프레임이 더 있음 |
| 1 | CONTROL | `0x02` | control part(핸드셰이크·프로토콜 오류 등) |
| 2 | IDENTITY | `0x04` | routing id 관련 프레임(ROUTER) |
| 3 | SUBSCRIBE | `0x08` | 구독 요청(PUB/SUB) |
| 4 | CANCEL | `0x10` | 구독 취소 |

`CONTROL`과 `IDENTITY`는 동시 설정 불가, `CONTROL`과 `MORE`도 동시 설정 불가.
`SUBSCRIBE` 또는 `CANCEL`이 설정된 frame은 그 bit 하나만 설정한다(둘은 상호 배타이며
다른 flag와 함께 올 수 없다; 디코더가 검증).

## 3. 연결 핸드셰이크

데이터가 흐르기 전에 control 프레임으로 핸드셰이크한다.

```text
Client ──── HELLO ────▶ Server
       ◀─── HELLO ────
       ──── READY ───▶
       ◀─── READY ────
       ═══ 데이터 교환 시작 ═══
```

control part 타입은 `HELLO`(인사), `READY`(메타데이터 교환)와 `ERROR`다. HELLO는 socket type과 routing id를
싣는다. READY는
기본적으로 control type만 보내고, `ZLINK_OPT_ZMP_METADATA`가 켜진 경우에만
`Socket-Type`/`Routing-Id` 속성을 싣는다(기본값 off). 정확한 payload 레이아웃은 internals를 본다.

## 4. 상위 envelope (요청/응답 · SPOT routed)

ZMP 데이터 프레임 위에서, 요청/응답과 SPOT 라우팅은 **멀티파트 control part**를
payload 앞에 덧붙이는 방식으로 구현한다(메시지 구조에 끼워 넣지 않음).

- **요청/응답 envelope**: payload 앞에 4개 control part(protocol id · version ·
  message type[request/reply/error] · request seq[8B BE uint64]).
- **SPOT routed envelope**: protocol/version/class·node rid·endpoint rid를 하나의
  packed header part에 담는다. SPOT 요청/응답은 이 header part 1개 뒤에 요청/응답
  control part 4개와 payload가 따른다.

정확한 part 수와 바이트 레이아웃, 인코딩 순서는
[internals/protocol-zmp](../internals/protocol-zmp.ko.md) §3~6이 정식으로 정의한다.

## 5. VSM과 wire의 관계

VSM(Very Small Message, 64-bit에서 41바이트 이하 inline 저장)은 **메모리 최적화일 뿐 wire
형식에 영향이 없다.** 헤더의 PAYLOAD SIZE가 항상 길이를 담으므로, 수신 측은 송신
측이 inline 저장을 썼는지 알 필요가 없다([설계 근거](design-rationale.ko.md)).

## 6. 새 바인딩을 만든다면

새 언어 바인딩은 보통 ZMP를 직접 구현하지 않는다 — **C 코어(C ABI)를 그대로 링크**해
래핑하는 것이 기준이다([바인딩 가이드](../../../bindings/doc/guide/README.ko.md)). ZMP 레퍼런스가
필요한 경우는 다음과 같다.

- C 코어를 쓰지 않고 **순수 그 언어로 ZMP를 재구현**할 때.
- zlink가 아닌 **다른 시스템과 wire interop**할 때.

두 경우 모두 비트 단위 정확성이 필요하므로 [internals/protocol-zmp](../internals/protocol-zmp.ko.md)와
`core/src/runtime/protocol/`의 인코더·디코더를 정식 출처로 삼는다.

---

> 더 보기: [internals/protocol-zmp](../internals/protocol-zmp.ko.md)(정식 정의) ·
> [internals/protocol-raw](../internals/protocol-raw.ko.md)(STREAM raw 프레이밍) ·
> [설계 근거](design-rationale.ko.md).
