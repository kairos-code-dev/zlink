// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const { createPayload, latencyUsFromPayload, stampPayload } = require('../common/perf_metrics');

const RECEIVER_ID = Buffer.from('router-perf-receiver', 'ascii');
const SENDER_ID = Buffer.from('router-perf-sender', 'ascii');

function partStrings(received) {
  return received.parts.map((part) => part.toBuffer().toString());
}

async function handshake(receiver, sender) {
  sender.send(RECEIVER_ID, zlink.Message.copyOf('PING'));
  const ping = receiver.receive();
  if (ping.routingId === null || partStrings(ping).join(',') !== 'PING') {
    throw new Error('router-router handshake receive failed');
  }

  receiver.send(SENDER_ID, zlink.Message.copyOf('PONG'));
  const pong = sender.receive();
  if (pong.routingId === null || partStrings(pong).join(',') !== 'PONG') {
    throw new Error('router-router handshake reply failed');
  }
}

async function runRouterRouterBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  const receiver = new zlink.RouterSocket(ctx);
  const sender = new zlink.RouterSocket(ctx);
  const endpoint = `inproc://perf-router-router-${process.pid}-${msgSize}`;
  const payload = createPayload(msgSize);
  const latenciesUs = [];
  let done = false;
  const startedAt = process.hrtime.bigint();
  const warmupNs = BigInt(Math.floor(options.warmup * 1_000_000_000));
  const activeNs = BigInt(Math.floor(options.duration * 1_000_000_000));

  try {
    receiver.setRoutingId(RECEIVER_ID);
    sender.setRoutingId(SENDER_ID);
    receiver.bind(endpoint);
    sender.connect(endpoint);
    await handshake(receiver, sender);

    receiver.recvHandler((_, parts) => {
      const elapsed = process.hrtime.bigint() - startedAt;
      if (elapsed >= warmupNs && elapsed < warmupNs + activeNs) {
        latenciesUs.push(latencyUsFromPayload(parts[0].toBuffer()));
      }
      if (elapsed >= warmupNs + activeNs) {
        done = true;
      }
    });

    while (!done) {
      stampPayload(payload);
      const result = sender.trySend(RECEIVER_ID, payload);
      if (result !== zlink.SendResult.Sent) {
        await new Promise((resolve) => setImmediate(resolve));
        continue;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }

    return latenciesUs;
  } finally {
    sender.close();
    receiver.close();
    ctx.close();
  }
}

module.exports = { runRouterRouterBenchmark };
