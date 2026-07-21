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
- `golden/checkpoint-envelope-v1.json`: 네 runtime이 non-empty Instance request journal·completion bytes를 읽고
  쓰는 golden fixture
- `golden/`: service frame의 정상·경계·오류 fixture를 추가하는 위치
- `traces/`: schema 승인 뒤 생성하는 normalized behavior trace

Codec table이나 fixture를 생성하기 전에 다음 명령이 성공해야 한다. 현재 gate는 30개 command, 81개 type,
4개 flag, 15개 bound와 durable fixture 2개를 확인한다. `--self-test`는 75가지 invalid mutation이 실제로
거부되는지도 확인한다. 여기에는 integer overflow, length capacity 초과, 잘못된 정렬 field, enum domain 이탈,
conditional discriminator 오류, TLV 순서·required capability 제약 변경, checkpoint vector 불일치, durable
magic·version·length·checksum·semantic·order·range 훼손, transfer graph·policy 오류와 fanout socket·beacon·deadline
변경이 포함된다.

```bash
node framework/runtime/protocol/validate-service-wire-schema.mjs \
  --self-test framework/runtime/protocol/service-wire-v1.schema.json
```

Durable fixture의 header는 4-byte magic, version `1`, zero flags, big-endian `u32` body length, exact body와
trailing CRC32C 순서다. CRC32C는 magic부터 body의 마지막 byte까지 포함한다. Location·Checkpoint Store provider는
이 bytes를 해석하지 않고 opaque value로 저장한다. Validator는 fixture를 semantic value로 decode한 뒤 다시
encode한 body도 원래 bytes와 일치하는지 확인한다.

현재 목표 의미와 validation 순서는
[`02-wire-protocol.ko.md`](../../doc/plan/v11.0/target-internals/02-wire-protocol.ko.md)가 설명한다. 언어별 상수
파일은 검증한 schema에서 생성하며 직접 편집하지 않는다.
