// SPDX-License-Identifier: MPL-2.0

'use strict';

const net = require('node:net');
const { once } = require('node:events');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeader,
  stampPayload,
  summarizeMetrics
} = require('../common/perf_metrics');

function parseArgs(argv) {
  const options = { endpoint: '', msgSize: 256, warmup: 1, duration: 2 };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--endpoint') {
      options.endpoint = argv[++i];
    } else if (argv[i] === '--msg-size') {
      options.msgSize = Number(argv[++i]);
    } else if (argv[i] === '--warmup') {
      options.warmup = Number(argv[++i]);
    } else if (argv[i] === '--duration') {
      options.duration = Number(argv[++i]);
    }
  }
  return options;
}

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
  const options = parseArgs(process.argv.slice(2));
  const url = new URL(options.endpoint.replace('tcp://', 'http://'));
  const warmupPayload = createPayload(options.msgSize);
  const activePayload = createPayload(options.msgSize);
  const runId = createRunId();
  const collector = createMetricCollector({
    runId,
    msgSize: options.msgSize
  });
  const socket = net.createConnection({ host: url.hostname, port: Number(url.port) });
  const state = { buffer: Buffer.alloc(0) };
  const waiters = [];
  const queued = [];

  const pushFrame = (payloadFrame) => {
    if (waiters.length > 0) {
      waiters.shift()(payloadFrame);
      return;
    }
    queued.push(payloadFrame);
  };

  const readFrame = async () => {
    if (queued.length > 0) {
      return queued.shift();
    }
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('stream echo timeout')), 3000);
      waiters.push((payloadFrame) => {
        clearTimeout(timeout);
        resolve(payloadFrame);
      });
    });
  };

  socket.on('data', (chunk) => {
    const frames = parseFrames(state, chunk);
    for (const payloadFrame of frames) {
      pushFrame(payloadFrame);
    }
  });

  try {
    await once(socket, 'connect');
    socket.setNoDelay(true);
    console.log('CLIENT_READY');

    const startedAtNs = process.hrtime.bigint();
    const warmupUntilNs = startedAtNs
      + BigInt(Math.floor(options.warmup * 1_000_000_000));
    const stopAtNs = startedAtNs
      + BigInt(Math.floor((options.warmup + options.duration) * 1_000_000_000));
    while (process.hrtime.bigint() < stopAtNs) {
      const activePhase = process.hrtime.bigint() < warmupUntilNs ? 2 : 0;
      const payload = activePhase === 2 ? warmupPayload : activePayload;
      stampPayload(payload, {
        phase: activePhase,
        runId,
        msgSize: options.msgSize
      });
      socket.write(frame(payload));
      const echoed = await readFrame();
      collector.record(decodeMetricHeader(echoed), process.hrtime.bigint());
    }

    const result = await collector.finish();
    const lines = summarizeMetrics('MULTI_STREAM', 'tcp', options.msgSize, result.latenciesUs, options.duration);
    for (const line of lines) {
      console.log(line);
    }
  } finally {
    await collector.close();
    socket.destroy();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
