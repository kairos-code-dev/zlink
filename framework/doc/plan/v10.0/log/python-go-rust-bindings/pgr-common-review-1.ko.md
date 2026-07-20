# PGR 공통 준비 review 1

## 1. 대상

2026-07-20에 구현 전 draft와 candidate package 도구를 읽기 전용으로 검토했다. 이 review는 Python,
Go 또는 Rust 구현 완료 review가 아니며, PGR-00~02의 공통 입력만 다룬다.

| 파일 | SHA-256 |
|---|---|
| `bindings/doc/spec/draft/route-mesh-python-go-rust.ko.md` | `8d4fb47276647b2fc419d18ad97fc6a5abbef4b8fbe00bd1ad1c996af3e3df64` |
| `scripts/local-package/bindings-candidate/create-manifest.sh` | `20f35dfbe5672bb58a40fb9b4928848589033475d0e0404bf4f016480d0eade3` |
| `scripts/local-package/bindings-candidate/build-wsl.sh` | `ce917903d8fbfea6f897cc8ede6e976a76b70ef227919cea6dd39daa8c1d95d4` |

세 파일의 `sha256sum` 출력 전체에 대한 SHA-256은
`a127799c970973bccfec6ec3142443a5fb9600f75f16dbd66461fd6fc2ff49e2`다.

## 2. 첫 검토와 수정

Codex reviewer session `019f7d7d-eaa8-7e30-bbd1-7621e01f7862`가 다음 범주의 차단 항목을 보고했다.

- package payload가 후보와 같은 byte인지 확인하지 않음
- Rust reply token의 실패 후 재시도 계약 충돌
- Actor 생성 결과·flag와 일부 exact interface·값 field 누락
- revision, export, ABI와 layout provenance 검증 부족
- package version과 Core version 관계를 patch까지 같게 강제함
- 제거 inventory, perf 대응과 platform loader inventory 누락
- 결과 디렉터리, checksum과 clean consumer 검증이 불완전함

구현 전 draft는 실패한 submit 뒤 reply token 재시도를 허용하고 성공한 submit에서만 소비하도록
고정했다. Actor와 dispatch의 exact interface 및 값 field를 보충했다. package 도구는 Core base version과
binding patch version을 구분하고, 실제 package의 native payload·header와 provenance를 검증하도록
수정했다. inventory에는 live 검색 파일 목록, C perf source 대응과 실제 platform loader 차이를 추가했다.

## 3. 재검토

Codex reviewer session `019f7d95-7081-70f2-9f31-b30d68690713`는 첫 검토에서 남아 있던 네 범주를 같은
파일 snapshot에서 다시 확인했다.

1. Rust remote Actor 조회와 Actor 삭제 interface
2. `SpotStatus`, `ActorRef`, `PeerChannel`, `ActorControlRecord`, `ActorJoinCompletion`,
   `ActorTransferControl`의 exact field
3. Core revision, service ABI와 public struct layout 재계산
4. wheel·module archive·crate 내부 payload/header 검증과 전체 provenance 기록

재검토 결과는 `CLEAN`이다. `bash -n`과 `git diff --check`도 통과했다. 다만 현재 공식
`core/build/lib/libzlink.so.10.6.0`이 source보다 오래되어 manifest 생성은 의도대로 실패한다. 따라서 이
review clean은 공통 계약과 도구의 정적 완료 증거이며, final Core candidate manifest와 언어별 package
실행 증거를 대신하지 않는다.
