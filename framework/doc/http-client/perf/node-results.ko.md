# perf 기록 — node

> 시나리오 정의는 [README](README.ko.md). 하네스는 `harness/`.

## H7 — gzip 64MiB(압축 32.1MiB) 해제 경로

- 하네스: `harness/node-h7-compression.js`, WSL2(Linux 6.6), Node v22.22.0,
  localhost in-process 서버, 3회 중앙값.
- 2026-07-12, 0.2.0(동기 zlib + eager UTF-8) → 0.3.0(async zlib + lazy body):

| 측정 | 0.2.0 | 0.3.0 | 변화 |
| --- | --- | --- | --- |
| H7a body 미접근 median | 478ms | 105ms | **−78%** |
| H7a max event loop 블로킹 | 461ms | 35ms | **−92%** |
| H7b body 접근 median | 487ms | 499ms | +2% (동등) |

- 해석: 블로킹의 주범은 zlib 자체(직접 측정 ~64-84ms)보다 64MiB
  `toString('utf8')`였다. async zlib은 디코드를 워커 풀로 옮기고, lazy body는
  텍스트를 읽지 않는 소비자의 변환 비용을 없앤다. 잔여 35ms는
  `Buffer.concat`(버퍼링 경로 본질) — 개선은 R12(스트리밍 관용화) 영역.
