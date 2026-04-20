// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeader,
  currentEpochNs,
  sleepImmediate,
  summarizeMetrics,
  stampPayload
} = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const {
  POLLIN,
  POLLOUT,
  recvNoWait,
  trySocketSend,
  waitForConnectionReady
} = require('./perf_multi_runtime');

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const dealers = [];
  const payloads = [];
  const waiting = [];
  const sendPending = [];
  const poller = new zlink.Poller();

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const dealer = new zlink.DealerSocket(ctx);
      dealer.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from(`CLIENT-${i}`, 'ascii')));
      dealers.push(dealer);
      payloads.push(createPayload(options.msgSize));
      waiting.push(false);
      sendPending.push(false);
    }
    for (let i = 0; i < dealers.length; i += 1) {
      await waitForConnectionReady(dealers[i], () => dealers[i].connect(options.endpoint));
      poller.addSocket(dealers[i], POLLIN | POLLOUT, i);
    }
    console.log(`CLIENT_READY,${options.msgSize}`);

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line !== `START,${options.msgSize}`) {
        continue;
      }

      const runId = createRunId(1);
      const activeStartNs = currentEpochNs();
      const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
      const collector = createMetricCollector({
        runId,
        msgSize: options.msgSize,
        activeStartNs,
        activeStopNs,
        roundTrip: true
      });
      let seq = 1n;

      const drainReply = (index) => {
        let progressed = false;
        while (true) {
          const echoed = recvNoWait(dealers[index]);
          if (!echoed) {
            break;
          }
          waiting[index] = false;
          collector.record(
            decodeMetricHeader(echoed.parts[0].data()),
            currentEpochNs()
          );
          progressed = true;
        }
        return progressed;
      };

      while (currentEpochNs() < activeStopNs) {
        let progressed = false;
        for (let i = 0; i < dealers.length; i += 1) {
          if (waiting[i] || sendPending[i]) {
            continue;
          }
          stampPayload(payloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
          if (!trySocketSend(dealers[i], payloads[i])) {
            sendPending[i] = true;
            continue;
          }
          waiting[i] = true;
          seq += 1n;
          progressed = true;
        }
        for (let i = 0; i < dealers.length; i += 1) {
          progressed = drainReply(i) || progressed;
        }
        if (progressed) {
          continue;
        }

        const ready = poller.waitAll(poller.size, 25);
        if (ready.length === 0) {
          await sleepImmediate();
          continue;
        }
        for (const event of ready) {
          const index = event.userData;
          if (!Number.isInteger(index)) {
            continue;
          }
          if ((event.events & POLLOUT) !== 0) {
            sendPending[index] = false;
          }
          if ((event.events & POLLIN) !== 0) {
            drainReply(index);
          }
        }
      }

      const drainDeadline = Date.now() + 2000;
      while (waiting.some(Boolean) && Date.now() < drainDeadline) {
        let progressed = false;
        const ready = poller.waitAll(poller.size, 25);
        for (const event of ready) {
          const index = event.userData;
          if (!Number.isInteger(index)) {
            continue;
          }
          if ((event.events & POLLIN) !== 0) {
            progressed = drainReply(index) || progressed;
          }
          if ((event.events & POLLOUT) !== 0) {
            sendPending[index] = false;
          }
        }
        if (!progressed) {
          await sleepImmediate();
        }
      }

      const result = await collector.finish();
      for (const metricLine of summarizeMetrics(
        'MULTI_DEALER_ROUTER',
        options.transport,
        options.msgSize,
        result.latenciesNs,
        options.duration
      )) {
        console.log(metricLine);
      }
      break;
    }
  } finally {
    poller.close();
    for (const dealer of dealers) {
      dealer.close();
    }
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
