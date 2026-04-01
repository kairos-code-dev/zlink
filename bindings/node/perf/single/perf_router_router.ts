// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const {
  attachCallbackCollector,
  driveSender,
  finishCollector
} = require('./perf_single_common');

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

  try {
    receiver.setRoutingId(RECEIVER_ID);
    sender.setRoutingId(SENDER_ID);
    receiver.bind(endpoint);
    sender.connect(endpoint);
    await handshake(receiver, sender);

    const state = attachCallbackCollector(
      (handler) => receiver.recvHandler(handler),
      msgSize,
      options,
      (_, parts) => parts[0].toBuffer()
    );

    await driveSender((payload) => (
      sender.trySend(RECEIVER_ID, payload) === zlink.SendResult.Sent
    ), state);
    return await finishCollector(state);
  } finally {
    sender.close();
    receiver.close();
    ctx.close();
  }
}

module.exports = { runRouterRouterBenchmark };
