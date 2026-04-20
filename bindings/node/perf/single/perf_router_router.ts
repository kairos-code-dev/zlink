// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist/canonical');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeaderFromParts,
  currentEpochNs,
  sleepImmediate,
  summarizeMetrics,
  stampPayload
} = require('../common/perf_metrics');
const {
  applyContextPolicy,
  applySocketPolicy,
  benchmarkEndpoint,
  drainRecvSocket,
  parseSingleBinaryArgs,
  resolveSingleLatencySampleCap,
  resolveSingleIdleDrainMs,
  waitForConnectionReady,
} = require('./perf_single_common');

const RECEIVER_ID = Buffer.from('router-perf-receiver', 'ascii');
const SENDER_ID = Buffer.from('router-perf-sender', 'ascii');
const RECEIVER_ROUTING_ID = zlink.RoutingId.fromBytes(RECEIVER_ID);
const SENDER_ROUTING_ID = zlink.RoutingId.fromBytes(SENDER_ID);

function partStrings(received) {
  return received.parts.map((part) => part.data().toString());
}

async function handshake(receiver, sender) {
  sender.send(RECEIVER_ROUTING_ID, Buffer.from('PING'));
  const ping = receiver.recv();
  if (ping.routingId === null || partStrings(ping).join(',') !== 'PING') {
    throw new Error('router-router handshake receive failed');
  }

  receiver.send(SENDER_ROUTING_ID, Buffer.from('PONG'));
  const pong = sender.recv();
  if (pong.routingId === null || partStrings(pong).join(',') !== 'PONG') {
    throw new Error('router-router handshake reply failed');
  }
}

async function runRouterRouterBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const receiver = new zlink.RouterSocket(ctx);
  const sender = new zlink.RouterSocket(ctx);
  const endpoint = await benchmarkEndpoint(options.transport, `router-router-${msgSize}`);

  try {
    applySocketPolicy(receiver, options);
    applySocketPolicy(sender, options);
    receiver.setRoutingId(RECEIVER_ROUTING_ID);
    sender.setRoutingId(SENDER_ROUTING_ID);
    receiver.bind(endpoint);
    await waitForConnectionReady(sender, () => sender.connect(endpoint));
    await handshake(receiver, sender);

    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs
      + BigInt(Math.floor(options.duration * 1_000_000_000));
    const runId = createRunId(options.runId ?? 1);
    const collector = createMetricCollector({
      runId,
      msgSize,
      activeStartNs,
      activeStopNs,
      sampleCap: resolveSingleLatencySampleCap()
    });
    const payload = createPayload(msgSize);
    let seq = 1n;
    let stop = false;

    const recvTask = drainRecvSocket(
      receiver,
      (received) => {
        const header = decodeMetricHeaderFromParts(received.parts);
        collector.record(header, currentEpochNs());
      },
      () => stop
    );

    while (currentEpochNs() < activeStopNs) {
      stampPayload(payload, {
        phase: 1,
        runId,
        msgSize,
        seq
      });
      sender.send(RECEIVER_ROUTING_ID, payload);
      seq += 1n;
    }
    stampPayload(payload, { phase: 2, runId, msgSize, seq });
    sender.send(RECEIVER_ROUTING_ID, payload);
    const drainDeadlineNs = activeStopNs
      + BigInt(resolveSingleIdleDrainMs(options)) * 1_000_000n;
    while (currentEpochNs() < drainDeadlineNs) {
      await sleepImmediate();
    }
    stop = true;
    await recvTask;
    return collector.finish();
  } finally {
    sender.close();
    receiver.close();
    ctx.close();
  }
}

module.exports = { runRouterRouterBenchmark };

if (require.main === module) {
  (async () => {
    const options = parseSingleBinaryArgs(process.argv.slice(2));
    const result = await runRouterRouterBenchmark(options.msgSize, options);
    for (const line of summarizeMetrics(
      'ROUTER_ROUTER',
      options.transport,
      options.msgSize,
      result.latenciesNs,
      options.duration,
      options.libName,
      result.accepted
    )) {
      console.log(line);
    }
  })().catch((error) => {
    console.error(error);
    process.exitCode = 1;
  });
}
