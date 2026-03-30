// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const { createPayload, setPayloadPhase, stampPayload } = require('../common/perf_metrics');

function parseArgs(argv) {
  const options = { endpoint: '', msgSize: 256, warmup: 1, duration: 2, clients: 1 };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--endpoint') {
      options.endpoint = argv[++i];
    } else if (argv[i] === '--msg-size') {
      options.msgSize = Number(argv[++i]);
    } else if (argv[i] === '--warmup') {
      options.warmup = Number(argv[++i]);
    } else if (argv[i] === '--duration') {
      options.duration = Number(argv[++i]);
    } else if (argv[i] === '--clients') {
      options.clients = Number(argv[++i]);
    }
  }
  return options;
}

async function waitConnectionReady(socket) {
  const monitor = socket.monitorOpen(zlink.MonitorEvent.CONNECTION_READY_CHANGED);
  try {
    const deadline = Date.now() + 2000;
    while (Date.now() < deadline) {
      const event = monitor.tryRecv();
      if (
        event
        && event.event === zlink.MonitorEvent.CONNECTION_READY_CHANGED
        && event.value >= 1
      ) {
        return;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }
    throw new Error('dealer connection ready timeout');
  } finally {
    monitor.close();
  }
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const payload = createPayload(options.msgSize);
  const clients = [];

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const dealer = new zlink.DealerSocket(ctx);
      dealer.connect(options.endpoint);
      clients.push(dealer);
    }
    for (const dealer of clients) {
      await waitConnectionReady(dealer);
    }

    const sendLoop = async (seconds, phase) => {
      const until = process.hrtime.bigint() + BigInt(Math.floor(seconds * 1_000_000_000));
      while (process.hrtime.bigint() < until) {
        for (const dealer of clients) {
          stampPayload(payload);
          setPayloadPhase(payload, phase);
          const result = dealer.trySend(payload);
          if (result !== zlink.SendResult.Sent) {
            await new Promise((resolve) => setImmediate(resolve));
          }
        }
      }
    };

    await sendLoop(options.warmup, 2);
    await sendLoop(options.duration, 0);
    for (const dealer of clients) {
      stampPayload(payload);
      setPayloadPhase(payload, 1);
      dealer.send(payload);
    }
  } finally {
    for (const dealer of clients) {
      dealer.close();
    }
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
