// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist/canonical');
const {
  createPayload,
  sleepImmediate,
  stampPayload
} = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');

const TOPIC = 'perf.topic';
const CONTROL_TOPIC = 'perf.control';
const SERVICE_TYPE_SPOT = 0x3002;
const SERVICE_NAME = 'perf.spot';
const DEBUG_SPOT = Boolean(process.env.PERF_DEBUG_TRANSITIONS);
const tryRawSubscribe = (socket) => {
  try {
    return socket.subscribe(zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return null;
    }
    throw error;
  }
};
const trySpotSubscribe = (spot) => {
  try {
    return spot.subscribe(zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return null;
    }
    throw error;
  }
};
const trySpotPublish = (spot, serviceName, topic, payload) => {
  try {
    spot.publish(serviceName, topic, payload, zlink.SendFlags.DontWait);
    return true;
  } catch (error) {
    if (error instanceof zlink.SubmitError && error.result === zlink.SubmitResult.Backpressured) {
      return false;
    }
    throw error;
  }
};

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
  if (!options.peerEndpoint) {
    throw new Error('missing --peer-endpoint');
  }
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const nodePubSend = new zlink.PubSocket(ctx);
  const nodePubRecv = new zlink.SubSocket(ctx);
  const controlSub = new zlink.SubSocket(ctx);
  let spot = null;
  const warmupPayload = createPayload(options.msgSize);
  const activePayload = createPayload(options.msgSize);
  const stopPayload = createPayload(options.msgSize);
  let controlConnected = false;
  let controlReadyCount = 0;
  let controlDoneCount = 0;
  let seq = 1n;

  const drainControlMessages = () => {
    while (true) {
      const received = tryRawSubscribe(controlSub);
      if (!received) {
        return;
      }
      const payload = received.parts[0] && received.parts[0].data()
        ? received.parts[0].data().toString('utf8')
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
        continue;
      }
      if (payload.startsWith('DONE_COUNT,')) {
        const [, sizeText, countText] = payload.split(',');
        if (Number(sizeText) === options.msgSize) {
          controlDoneCount = Number(countText);
          debugSpot(`control done count received ${controlDoneCount}`);
        }
      }
    }
  };

  const publishUntil = async (payload, phase, deadlineNs) => {
    debugSpot(`publish phase begin ${phase}`);
    while (process.hrtime.bigint() < deadlineNs) {
      stampPayload(payload, { phase, runId: 0, msgSize: options.msgSize, seq });
      trySpotPublish(spot, SERVICE_NAME, TOPIC, Buffer.from(payload));
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
      const result = trySpotPublish(spot, SERVICE_NAME, TOPIC, Buffer.from(stopPayload));
      if (result) {
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

  const waitForControlDoneCount = async (timeoutMs) => {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      drainControlMessages();
      if (controlDoneCount >= options.clients) {
        debugSpot(`control done satisfied ${controlDoneCount}`);
        return;
      }
      await sleepImmediate();
    }
    throw new Error(`spot server control done timeout: done=${controlDoneCount} expected=${options.clients}`);
  };

  try {
    node.attachPubSub(SERVICE_NAME, nodePubSend, nodePubRecv);
    nodePubSend.bind(options.endpoint);
    node.bind(options.peerEndpoint);
    spot = node.createSpot();
    controlSub.setSubscription(CONTROL_TOPIC);
    controlSub.connect(options.controlEndpoint);
    console.log(`READY,${options.endpoint}`);
    console.log(`CONTROL_READY,${options.controlEndpoint}`);
    debugSpot(`ready emitted ${options.endpoint} ${options.controlEndpoint}`);

    debugSpot('await control ready count');
    await waitForControlReadyCount(5000);
    debugSpot('control ready count satisfied');

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
    debugSpot('await control done');
    await waitForControlDoneCount(5000);
    debugSpot('control done satisfied');
    debugSpot('done');
  } finally {
    debugSpot('finally start');
    if (spot) {
      spot.close();
    }
    node.close();
    controlSub.close();
    nodePubRecv.close();
    nodePubSend.close();
    debugSpot('close ctx');
    ctx.close();
    debugSpot('finally done');
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
