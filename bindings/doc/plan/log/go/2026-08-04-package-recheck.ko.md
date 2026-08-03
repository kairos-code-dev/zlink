# Go binding Core 11 package 재검증 기록

이 기록은 2026-08-04에 확인한 Go binding의 candidate-bound package evidence를 정리한다. Linux x86_64
package·clean consumer·sample·perf smoke 범위는 통과했지만, Go 작업 전체는 공통 submit 계약 승인, 독립
frontier review와 다른 platform native consumer가 남아 `PARTIAL / NOT CLEAN`이다.

## Candidate와 package evidence

| 항목 | 값 |
|------|-----|
| Candidate manifest | `.artifacts/v11/evidence/V11-M3-CORE-VERIFY/candidate-reply-match-completion-hwm-20260801.json` |
| Candidate manifest SHA-256 | `d318525a4cf8496b6bef5d900c9a88330ea6d7e10ed4120ac0fd9f19d23f6765` |
| Candidate aggregate SHA-256 | `327587596195a162374498b630f51a043977dd392eb556061af615bf05186703` |
| Core package provenance SHA-256 | `46f7bd17c0be3987fed14ca3cb594139e3edb778d3996248d23b6d2d6b53f693` |
| Core runtime SHA-256 | `b6fadc481c649b50637a9c0eb01d15a016e6ba4cd5bab967bdb6da4497a3c0c4` |
| Binding source revision | `f83290b28cb63afdc88e06110fb4c50a8d8b7053` |
| Binding source manifest SHA-256 | `d6bd4bebf649f74dcc9d966d438f1e35da5435cc546140755c75cd135b1a5004` |
| Binding source aggregate SHA-256 | `8ca3316878060954edae601ea70351464a192df06d718de0d10988679eadcad7` |
| Module zip SHA-256 | `12898bb155df9eb5c29dda9b1e5d872df465f4ea2d770468e3d86e04e3fbc1ef` |
| Package evidence SHA-256 | `7badf3d397c56139c74410173d391b0d1368ab28a6dd9949487bac6aa48b883d` |
| Package platforms | `linux-x86_64` |

Package evidence는 `zlink.systems/zlink/v11@v11.1.0` module, package-local Core 11 header와 candidate runtime을
사용한다. `cleanConsumer`는 `replace` 없이 실제 message roundtrip과 module-cache runtime load를 통과했다.
Package에는 service·Spot·Actor·build·results forbidden entry가 없다.

## 실행 결과

- `bindings/go/tests/run_tests.sh`: raw allowlist, source test, sample process를 포함해 `pass=7 fail=0`
- fresh `go test -count=1 ./...`: exit code `0`
- `go vet ./...`: exit code `0`
- Single smoke: `PAIR`, `inproc`, message size `64`, duration `1`, run `1`, exit code `0`
- Multi smoke: `MULTI_DEALER_ROUTER`, `tcp`, clients `1`, message size `64`, duration `1`, run `1`, exit code `0`

Perf smoke는 runtime path와 SHA-256을 출력하고 공식 report를 만들지 않는 범위로 실행했다. 이 결과는 성능
수치 개선을 증명하지 않는다.

## 설계 검토와 남은 조건

Go raw cgo boundary, ownership·error mapping, request progress owner와 hot-path cost inventory는 현재
계획의 PASS 범위에 있다. Service projection과 Core 10 compatibility surface도 제거했다.

다음 조건은 이 package evidence만으로 닫히지 않는다.

1. Go·Rust submit 반환 초안은 아직 공통 승인되지 않았다. 현재 Go terminal method의 `(bool, error)`와
   request completion 정책을 승인 전에는 `error` only로 바꾸지 않는다.
2. 현재 V11-R2 review는 `coordinator_self_review`, `independent: false`이다. 다른 candidate에 대한 과거
   independent review evidence를 현재 candidate `d318...`에 재사용하지 않는다.
3. Linux arm64 payload는 Core 9 `libzlink.so.9`이고, Darwin payload는 이 candidate runtime과 native
   consumer로 검증되지 않았다. Windows는 현재 Go 계획 범위 밖이다.
4. 따라서 parity inventory의 행이 채워졌더라도 최종 parity 판정, 정식 common contract 문서와 GO-07의 독립
   `CLEAN` review를 완료로 기록하지 않는다.
