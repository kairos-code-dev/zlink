/* SPDX-License-Identifier: Apache-2.0 */
// H7 (node): gzip 응답 해제 경로의 처리량과 event loop 블로킹 측정.
// 사용법: node node-h7-compression.js <http-client 모듈 경로>
//   예) node node-h7-compression.js ../../../../languages/node/packages/http-client/dist
// 규칙: perf/README.ko.md — baseline vs patched 비교 없이 perf 변경 커밋 금지.

const http = require('node:http');
const zlib = require('node:zlib');
const crypto = require('node:crypto');

const clientPath = process.argv[2];
if (!clientPath) {
  console.error('usage: node node-h7-compression.js <http-client module path>');
  process.exit(2);
}

// 64MiB 반압축성 데이터(텍스트+랜덤 혼합) — 실무 페이로드 근사.
const parts = [];
for (let i = 0; i < 64; i++) {
  parts.push(Buffer.alloc(512 * 1024, `json-payload-${i}-`));
  parts.push(crypto.randomBytes(512 * 1024));
}
const raw = Buffer.concat(parts);
const gz = zlib.gzipSync(raw);

async function run() {
  const { ZLinkHttpClient } = require(clientPath);
  const server = http.createServer((req, res) => {
    res.writeHead(200, { 'content-encoding': 'gzip' });
    res.end(gz);
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const client = ZLinkHttpClient.create(`http://127.0.0.1:${server.address().port}`)
    .maxResponseBodySize(256 * 1024 * 1024)
    .compression()
    .timeout(60000)
    .build();

  async function scenario(label, touchBody) {
    const times = [];
    let maxLag = 0;
    for (let i = 0; i < 3; i++) {
      let last = process.hrtime.bigint();
      const tick = setInterval(() => {
        const now = process.hrtime.bigint();
        const lag = Number(now - last) / 1e6 - 2;
        if (lag > maxLag) maxLag = lag;
        last = now;
      }, 2);
      const t = process.hrtime.bigint();
      const res = await client.get('/big').submitRaw();
      if (touchBody && res.body.length < raw.length / 2) throw new Error('body mismatch');
      if (!touchBody && res.status !== 200) throw new Error('bad status');
      times.push(Number(process.hrtime.bigint() - t) / 1e6);
      // 지연된 틱이 발화한 뒤 정리해야 블로킹이 기록된다.
      await new Promise((r) => setTimeout(r, 20));
      clearInterval(tick);
    }
    times.sort((a, b) => a - b);
    console.log(
      `${label}: median=${times[1].toFixed(0)}ms maxEventLoopLag=${maxLag.toFixed(0)}ms`,
    );
  }

  console.log(`payload: raw=${(raw.length / 1048576).toFixed(0)}MiB gz=${(gz.length / 1048576).toFixed(1)}MiB`);
  await scenario('H7a body 미접근(바이너리 소비자)', false);
  await scenario('H7b body 접근(텍스트 소비자)   ', true);
  await client.close();
  server.close();
}

run().catch((error) => {
  console.error(error);
  process.exit(1);
});
