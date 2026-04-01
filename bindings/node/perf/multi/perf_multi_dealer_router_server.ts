// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist');

function parseArgs(argv) {
  const options = { endpoint: '' };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--endpoint') {
      options.endpoint = argv[++i];
    }
  }
  return options;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const router = new zlink.RouterSocket(ctx);
  let stop = false;
  let receiveLoop = null;

  try {
    router.bind(options.endpoint);
    console.log(`READY,${options.endpoint}`);

    receiveLoop = (async () => {
      while (!stop) {
        const received = router.tryReceive();
        if (!received) {
          await new Promise((resolve) => setImmediate(resolve));
          continue;
        }
        if (received.routingId) {
          router.send(received.routingId, received.parts.map((part) => part.toBuffer()));
        }
      }
    })();

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line === 'STOP') {
        stop = true;
        break;
      }
    }
  } finally {
    if (receiveLoop) {
      await receiveLoop;
    }
    router.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
