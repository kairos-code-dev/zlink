# Framework service protocol assets

이 디렉터리는 네 runtime lane이 함께 사용하는 service wire와 durable format 생성 입력의 위치다.
Application 공개 API나 공통 native runtime을 제공하지 않는다.

- `service-wire-v1.schema.json`: command, flag, field, bound와 RouteMesh·ClientServer·fanout liveness profile의 단일
  생성 입력
- `validate-service-wire-schema.mjs`: integer encoding, 재귀 aggregate length capacity, field reference,
  정렬·중복, closed union, TLV 순서, durable checksum·semantic relation과 exact transfer state rule을 확인하는 생성
  전 gate
- `golden/durable-authority-v1.json`: 네 runtime이 full maintenance transfer authority bytes를 읽고 쓰는 golden
  fixture
- `golden/transfer-envelope-v1.json`: 네 runtime이 non-empty Instance request journal·completion bytes를 읽고
  쓰는 golden fixture
- `golden/authority-key-v1.json`: MeshName과 독립적인 global ActorId·SpotRid를 canonical Store key로 만드는
  정상 encoding fixture
- `golden/contract-amendment-v1.json`: object role·capacity descriptor, durable creation intent, generic
  Reserve·Commit·Abort fence, User Spot aggregate, bounded forwarding과 exact-ref route의 공통 golden fixture
- `golden/`: service frame의 정상·경계·오류 fixture를 추가하는 위치
- `generate-service-wire-assets.mjs`: 검증한 schema에서 네 언어 command·flag 상수와 공통 decoder fixture를
  생성하고 `--check`로 drift를 차단하는 도구
- `generated/`: C++·.NET·JVM·Node.js runtime이 복사하지 않고 사용하는 생성 상수
- `traces/`: schema 승인 뒤 생성하는 normalized behavior trace

Codec table이나 fixture를 생성하기 전에 다음 명령이 성공해야 한다. 현재 gate는 37개 command, 151개 type,
4개 flag, 35개 bound, durable fixture 3개와 logical·JSON·authority key fixture를 확인한다. `--self-test`는
contract amendment fixture 1개와 154가지 invalid mutation이 실제로
거부되는지도 확인한다. 여기에는 integer overflow, length capacity 초과, 잘못된 정렬 field, enum domain 이탈,
conditional discriminator 오류, TLV 순서·required capability 제약 변경, transfer vector 불일치, durable
magic·version·length·checksum·semantic·order·range 훼손, transfer graph·policy 오류와 fanout socket·beacon·deadline
변경이 포함된다.

```bash
node framework/runtime/protocol/validate-service-wire-schema.mjs \
  --self-test framework/runtime/protocol/service-wire-v1.schema.json
node framework/runtime/protocol/generate-service-wire-assets.mjs
node framework/runtime/protocol/generate-service-wire-assets.mjs --check
node framework/runtime/protocol/verify-service-wire-decoder-fixtures.mjs
```

Durable fixture의 header는 4-byte magic, version `1`, zero flags, big-endian `u32` body length, exact body와
trailing CRC32C 순서다. CRC32C는 magic부터 body의 마지막 byte까지 포함한다. Location·Transfer Store provider는
이 bytes를 해석하지 않고 opaque value로 저장한다. Validator는 fixture를 semantic value로 decode한 뒤 다시
encode한 body도 원래 bytes와 일치하는지 확인한다.

현재 목표 의미와 validation 순서는
[`service-wire-protocol.ko.md`](../../doc/framework/common/internals/service-wire-protocol.ko.md)가 설명한다. 언어별 상수
파일은 검증한 schema에서 생성하며 직접 편집하지 않는다.
