// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { sleepImmediate } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const {
  POLLIN,
  POLLOUT,
  applyContextPolicy,
  applySocketPolicy,
  recvNoWait,
  trySocketSend,
  waitForConnectionReadyCount
} = require('./perf_multi_runtime');

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'server', 'MULTI_DEALER_ROUTER');
  const router = new zlink.RouterSocket(ctx);
  const poller = new zlink.Poller();
  const pending = [];
  let rl = null;
  let stop = false;

  try {
    applySocketPolicy(router);
    router.bind(options.endpoint);
    poller.addSocket(router, POLLIN);
    const readyBarrier = waitForConnectionReadyCount(router, options.clients);
    console.log(`READY,${options.endpoint}`);

    rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    (async () => {
      for await (const line of rl) {
        if (line === 'STOP' || line === 'QUIT') {
          stop = true;
          break;
        }
      }
    })();

    await readyBarrier;

    while (!stop) {
      poller.modifySocket(router, pending.length > 0 ? (POLLIN | POLLOUT) : POLLIN);
      const ready = poller.wait(25);
      if (!ready) {
        await sleepImmediate();
        continue;
      }

      if ((ready.events & POLLIN) !== 0) {
        while (true) {
          const received = recvNoWait(router);
          if (!received) {
            break;
          }
          try {
            if (!received.routingId) {
              received.close();
              continue;
            }
            if (pending.length === 0 && trySocketSend(router, received.routingId, received.parts)) {
              received.close();
              continue;
            }
            pending.push(received);
          } catch (error) {
            received.close();
            throw error;
          }
        }
      }

      if ((ready.events & POLLOUT) !== 0) {
        while (pending.length > 0) {
          const current = pending[0];
          if (!trySocketSend(router, current.routingId, current.parts)) {
            break;
          }
          pending.shift();
          current.close();
        }
      }
    }
  } finally {
    rl?.close();
    while (pending.length > 0) {
      pending.shift().close();
    }
    poller.close();
    router.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
