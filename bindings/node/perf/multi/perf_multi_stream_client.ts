// SPDX-License-Identifier: MPL-2.0

'use strict';

const net = require('node:net');
const { once } = require('node:events');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeader,
  currentEpochNs,
  summarizeMetrics,
  stampPayload
} = require('../common/perf_metrics');
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

function createFrameReader(socket) {
  const state = {
    buffer: Buffer.alloc(0),
    pending: [],
    waiters: []
  };

  const flushWaiters = () => {
    while (state.pending.length > 0 && state.waiters.length > 0) {
      const waiter = state.waiters.shift();
      waiter.resolve(state.pending.shift());
    }
  };

  const onData = (chunk) => {
    const payloads = parseFrames(state, chunk);
    if (payloads.length > 0) {
      state.pending.push(...payloads);
      flushWaiters();
    }
  };

  const onError = (error) => {
    while (state.waiters.length > 0) {
      state.waiters.shift().reject(error);
    }
  };

  const onClose = () => {
    const error = new Error('stream socket closed');
    while (state.waiters.length > 0) {
      state.waiters.shift().reject(error);
    }
  };

  socket.on('data', onData);
  socket.on('error', onError);
  socket.on('close', onClose);

  return {
    async nextFrame() {
      if (state.pending.length > 0) {
        return state.pending.shift();
      }
      return new Promise((resolve, reject) => {
        state.waiters.push({ resolve, reject });
      });
    },
    close() {
      socket.off('data', onData);
      socket.off('error', onError);
      socket.off('close', onClose);
    }
  };
}

async function connectStreamSocket(endpoint) {
  const url = new URL(endpoint);
  const port = Number(url.port);
  const host = url.hostname || '127.0.0.1';
  const socket = net.createConnection({ host, port });
  socket.setNoDelay(true);
  await once(socket, 'connect');
  return socket;
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const runId = createRunId();
  const collector = createMetricCollector({
    runId,
    msgSize: options.msgSize
  });
  const sockets = [];
  const readers = [];
  const payloads = [];

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const socket = await connectStreamSocket(options.endpoint);
      sockets.push(socket);
      readers.push(createFrameReader(socket));
      payloads.push({
        warmup: createPayload(options.msgSize),
        active: createPayload(options.msgSize)
      });
    }

    console.log('CLIENT_READY');

    const warmupUntilNs = process.hrtime.bigint()
      + BigInt(Math.floor(options.warmup * 1_000_000_000));
    let seq = 1n;
    while (process.hrtime.bigint() < warmupUntilNs) {
      for (let i = 0; i < sockets.length; i += 1) {
        const payload = payloads[i].warmup;
        stampPayload(payload, { phase: 0, runId, msgSize: options.msgSize, seq });
        if (!sockets[i].write(frame(payload))) {
          await once(sockets[i], 'drain');
        }
        await readers[i].nextFrame();
        seq += 1n;
      }
    }

    const stopAtNs = process.hrtime.bigint()
      + BigInt(Math.floor(options.duration * 1_000_000_000));
    while (process.hrtime.bigint() < stopAtNs) {
      for (let i = 0; i < sockets.length; i += 1) {
        const payload = payloads[i].active;
        stampPayload(payload, { phase: 1, runId, msgSize: options.msgSize, seq });
        if (!sockets[i].write(frame(payload))) {
          await once(sockets[i], 'drain');
        }
        const echoed = await readers[i].nextFrame();
        collector.record(decodeMetricHeader(echoed), currentEpochNs());
        seq += 1n;
      }
    }

    const result = await collector.finish();
    const lines = summarizeMetrics(
      'MULTI_STREAM',
      'tcp',
      options.msgSize,
      result.latenciesNs,
      options.duration
    );
    for (const line of lines) {
      console.log(line);
    }
  } finally {
    await collector.close();
    for (const reader of readers) {
      reader.close();
    }
    for (const socket of sockets) {
      socket.destroy();
    }
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
