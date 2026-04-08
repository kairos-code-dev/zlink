// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist');
const { parseMultiArgs } = require('./perf_multi_common');

function frame(buffer) {
  const framed = Buffer.allocUnsafe(buffer.length + 4);
  framed.writeUInt32BE(buffer.length, 0);
  buffer.copy(framed, 4);
  return framed;
}

function parseFrames(state, chunk) {
  state.buffer = Buffer.concat([state.buffer, chunk]);
  const payloads = [];

  while (state.buffer.length >= 4) {
    const payloadLength = state.buffer.readUInt32BE(0);
    const frameLength = payloadLength + 4;
    if (state.buffer.length < frameLength) {
      break;
    }
    payloads.push(state.buffer.subarray(4, frameLength));
    state.buffer = state.buffer.subarray(frameLength);
  }

  return payloads;
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2), { msgSize: undefined, warmup: undefined, duration: undefined, clients: undefined });
  const ctx = new zlink.Context();
  const stream = new zlink.StreamSocket(ctx);
  const states = new Map();
  let stop = false;
  let receiveLoop = null;

  const processReceived = (received) => {
    const routingId = received.routingId;
    if (!routingId) {
      return;
    }
    const key = routingId.toString('hex');
    let state = states.get(key);
    if (!state) {
      state = { buffer: Buffer.alloc(0) };
      states.set(key, state);
    }

    for (const part of received.parts) {
      const payloads = parseFrames(state, part.data);
      for (const payload of payloads) {
        stream.send(routingId, Buffer.from(frame(payload)));
      }
    }
  };

  try {
    stream.bind(options.endpoint);
    if (options.recv === 'callback') {
      stream.onReceive((routingId, parts) => {
        processReceived({ routingId, parts });
      });
    }

    console.log(`READY,${options.endpoint}`);

    if (options.recv !== 'callback') {
      receiveLoop = (async () => {
        while (!stop) {
          try {
            const received = stream.recv();
            processReceived(received);
          } catch (error) {
            if (stop) {
              return;
            }
            throw error;
          }
        }
      })();
    }

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line === 'STOP') {
        stop = true;
        stream.close();
        break;
      }
    }
  } finally {
    if (receiveLoop) {
      await receiveLoop.catch(() => {});
    }
    try {
      stream.close();
    } catch (_) {}
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
