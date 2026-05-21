// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { configureTlsServer } = require('../common/perf_tls');
const { requireNative } = require('../../dist/zlink/runtime/native/native');
const { parseMultiArgs } = require('./perf_multi_common');
const {
  POLLIN,
  POLLOUT,
  applyAutoHwmMsgUnit,
  applyContextPolicy,
  applySocketPolicy,
  emitMultiSocketHwmDetail,
  pollEvents,
  waitPollerOne,
  waitForConnectionReadyCount
} = require('./perf_multi_runtime');

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'server', 'MULTI_ROUTER_ROUTER');
  const router = new zlink.RouterSocket(ctx);
  const poller = new zlink.Poller();
  let pollBuffer = null;
  let rl = null;
  let stop = false;

  try {
    applySocketPolicy(router);
    configureTlsServer(router, options.transport);
    router.setRoutingId(
      zlink.RoutingId.fromBytes(Buffer.from('multi-router-router-server', 'ascii'))
    );
    router.bind(options.endpoint);
    applyAutoHwmMsgUnit(ctx, options.msgSize);
    ctx.recalculateAutoHwm();
    emitMultiSocketHwmDetail(router, 'endpoint', options.transport, options.msgSize);
    poller.add(router, pollEvents(POLLIN), 0);
    pollBuffer = new zlink.PollEvents(1);
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
      poller.modify(router, pollEvents(POLLIN | POLLOUT));
      const ready = waitPollerOne(poller, pollBuffer, -1);
      if (!ready) {
        continue;
      }
      requireNative().socketRouteEchoStep(router.nativeHandle());
    }
  } finally {
    rl?.close();
    pollBuffer?.close();
    poller.close();
    router.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
