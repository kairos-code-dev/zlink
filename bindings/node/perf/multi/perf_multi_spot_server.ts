// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist');
const {
  createPayload,
  sleepImmediate,
  stampPayload
} = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');

const TOPIC = 'perf.topic';
const CONTROL_TOPIC = 'perf.control';
const DEBUG_SPOT = Boolean(process.env.PERF_DEBUG_TRANSITIONS);

function debugSpot(message) {
  if (DEBUG_SPOT) {
    console.error(`[multi-spot-server] ${message}`);
  }
}

function ensureMeshPubBudgetDefault() {
  if (!process.env.ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM) {
    process.env.ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM = '100';
  }
}

async function main() {
  ensureMeshPubBudgetDefault();
  const options = parseMultiArgs(process.argv.slice(2));
  if (!options.controlEndpoint) {
    throw new Error('missing --control-endpoint');
  }
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);
  const controlNode = new zlink.SpotNode(ctx);
  const controlSpot = new zlink.Spot(controlNode);
  const warmupPayload = createPayload(options.msgSize);
  const activePayload = createPayload(options.msgSize);
  const stopPayload = createPayload(options.msgSize);
  let controlConnected = false;
  let controlReadyCount = 0;
  let startRequested = false;
  let seq = 1n;

  const drainControlMessages = () => {
    while (true) {
      const received = controlSpot.trySubscribe();
      if (!received) {
        return;
      }
      const payload = received.parts[0] && received.parts[0].data
        ? received.parts[0].data.toString('utf8')
        : '';
      if (payload === 'CONNECTED') {
        controlConnected = true;
        debugSpot('control connected received');
        continue;
      }
      if (payload.startsWith('READY_COUNT,')) {
        const [, sizeText, countText] = payload.split(',');
        if (Number(sizeText) === options.msgSize) {
          controlReadyCount = Number(countText);
          debugSpot(`control ready count received ${controlReadyCount}`);
        }
      }
    }
  };

  const publishUntil = async (payload, phase, deadlineNs) => {
    debugSpot(`publish phase begin ${phase}`);
    while (process.hrtime.bigint() < deadlineNs) {
      stampPayload(payload, { phase, runId: 0, msgSize: options.msgSize, seq });
      spot.tryPublish(TOPIC, payload);
      seq += 1n;
      await new Promise((resolve) => setImmediate(resolve));
    }
    debugSpot(`publish phase end ${phase}`);
  };

  const publishStopFrames = async () => {
    debugSpot('publish stop begin');
    let sent = 0;
    const deadline = Date.now() + 5000;
    while (sent < options.clients && Date.now() < deadline) {
      stampPayload(stopPayload, { phase: 2, runId: 0, msgSize: options.msgSize, seq });
      const result = spot.tryPublish(TOPIC, stopPayload);
      if (result === zlink.SendResult.Sent) {
        sent += 1;
        seq += 1n;
        await new Promise((resolve) => setImmediate(resolve));
        continue;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }
    if (sent < options.clients) {
      throw new Error(`spot server stop publish timeout: sent=${sent} expected=${options.clients}`);
    }
    debugSpot('publish stop end');
    for (let i = 0; i < 4; i += 1) {
      await new Promise((resolve) => setImmediate(resolve));
    }
  };

  const publishControlStart = async () => {
    debugSpot('publish control start begin');
    const startPayload = Buffer.from(`START,${options.msgSize}`);
    while (true) {
      const result = controlSpot.tryPublish(CONTROL_TOPIC, startPayload);
      if (result === zlink.SendResult.Sent) {
        debugSpot('publish control start end');
        return;
      }
      await sleepImmediate();
    }
  };

  const waitForControlReadyCount = async (timeoutMs) => {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      drainControlMessages();
      if (controlConnected && controlReadyCount >= options.clients) {
        debugSpot(`control ready satisfied ${controlReadyCount}`);
        return;
      }
      await sleepImmediate();
    }
    throw new Error(`spot server control ready timeout: connected=${controlConnected} ready=${controlReadyCount} expected=${options.clients}`);
  };

  try {
    controlNode.bind(options.controlEndpoint);
    controlSpot.setSubscription(CONTROL_TOPIC);
    controlSpot.setLinger(0);
    controlSpot.setSendHighWaterMark(Math.max(1024, options.clients * 8));
    controlSpot.setSendTimeout(200);
    controlSpot.setReceiveHighWaterMark(Math.max(1024, options.clients * 8));
    controlSpot.setReceiveTimeout(200);
    node.bind(options.endpoint);
    console.log(`READY,${options.endpoint}`);
    console.log(`CONTROL_READY,${options.controlEndpoint}`);
    debugSpot(`ready emitted ${options.endpoint} ${options.controlEndpoint}`);

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      debugSpot(`stdin line ${line}`);
      if (line.startsWith('CONNECT_CONTROL,')) {
        const endpoint = line.slice('CONNECT_CONTROL,'.length);
        if (endpoint) {
          controlNode.connectPeer(endpoint);
          controlConnected = true;
          console.log(`CONTROL_CONNECTED,${endpoint}`);
        }
        if (startRequested && controlConnected) {
          break;
        }
        continue;
      }
      if (line !== `START,${options.msgSize}`) {
        continue;
      }
      startRequested = true;
      if (controlConnected) {
        break;
      }
    }

    debugSpot('await control ready count');
    await waitForControlReadyCount(5000);
    debugSpot('control ready count satisfied');
    await publishControlStart();

    await publishUntil(
      warmupPayload,
      0,
      process.hrtime.bigint() + BigInt(Math.floor(options.warmup * 1_000_000_000))
    );
    await publishUntil(
      activePayload,
      1,
      process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000))
    );
    await publishStopFrames();
    debugSpot('done');
  } finally {
    debugSpot('finally start');
    spot.close();
    node.close();
    controlSpot.close();
    controlNode.close();
    debugSpot('close ctx');
    ctx.close();
    debugSpot('finally done');
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
