// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist');
const { parseMultiArgs } = require('./perf_multi_common');
const { drainRecvSocket } = require('./perf_multi_runtime');

async function main() {
  const options = parseMultiArgs(process.argv.slice(2), { msgSize: undefined, warmup: undefined, duration: undefined, clients: undefined });
  const ctx = new zlink.Context();
  const router = new zlink.RouterSocket(ctx);
  let stop = false;

  try {
    router.bind(options.endpoint);
    const receiveLoop = drainRecvSocket(
      router,
      (received) => {
        if (received.routingId) {
          router.send(received.routingId, received.parts.map((part) => part.data));
        }
      },
      () => stop
    );
    console.log(`READY,${options.endpoint}`);

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line === 'STOP') {
        stop = true;
        break;
      }
    }

    await receiveLoop;
  } finally {
    router.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
