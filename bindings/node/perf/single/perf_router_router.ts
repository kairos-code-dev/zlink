// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist/canonical');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeader,
  currentEpochNs,
  sleepImmediate,
  stampPayload
} = require('../common/perf_metrics');
const {
  drainRecvNow,
  drainRecvSocket,
  waitForConnectionReady,
  trySocketSend
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
  const receiver = new zlink.RouterSocket(ctx);
  const sender = new zlink.RouterSocket(ctx);
  const endpoint = `inproc://perf-router-router-${process.pid}-${msgSize}`;

    try {
      receiver.setRoutingId(RECEIVER_ROUTING_ID);
      sender.setRoutingId(SENDER_ROUTING_ID);
      receiver.bind(endpoint);
      await waitForConnectionReady(sender, () => sender.connect(endpoint));
      await handshake(receiver, sender);

    const startedAtNs = process.hrtime.bigint();
    const runId = createRunId();
    const collector = createMetricCollector({ runId, msgSize });
    const payload = createPayload(msgSize);
    let seq = 1n;
    const warmupUntilNs = startedAtNs
      + BigInt(Math.floor(options.warmup * 1_000_000_000));
    const stopAtNs = startedAtNs
      + BigInt(Math.floor((options.warmup + options.duration) * 1_000_000_000));
    let stop = false;

    const recvTask = drainRecvSocket(
      receiver,
      (received) => {
        const header = decodeMetricHeader(received.parts[0].data());
        collector.record(header, currentEpochNs());
      },
      () => stop
    );

    while (process.hrtime.bigint() < stopAtNs) {
      for (let i = 0; i < 256 && process.hrtime.bigint() < stopAtNs; i += 1) {
        stampPayload(payload, {
          phase: process.hrtime.bigint() < warmupUntilNs ? 0 : 1,
          runId,
          msgSize,
          seq
        });
        if (!trySocketSend(sender, RECEIVER_ROUTING_ID, payload)) {
          break;
        }
        seq += 1n;
      }
      drainRecvNow(receiver, (received) => {
        const header = decodeMetricHeader(received.parts[0].data());
        collector.record(header, currentEpochNs());
      });
      if ((Number(seq) & 0x03) === 0) {
        await sleepImmediate();
      }
    }

    for (let i = 0; i < 4; i += 1) {
      drainRecvNow(receiver, (received) => {
        const header = decodeMetricHeader(received.parts[0].data());
        collector.record(header, currentEpochNs());
      });
      await sleepImmediate();
    }
    stop = true;
    await recvTask;
    const result = await collector.finish();
    return result.latenciesNs;
  } finally {
    sender.close();
    receiver.close();
    ctx.close();
  }
}

module.exports = { runRouterRouterBenchmark };
