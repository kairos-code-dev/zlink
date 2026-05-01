// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { configureTlsServer } = require('../common/perf_tls');
const {
  createPayload,
  createRunId,
  sleepImmediate,
  stampPayload
} = require('../common/perf_metrics');
const { benchmarkEndpoint, parseMultiArgs } = require('./perf_multi_common');
const {
  POLLIN,
  POLLOUT,
  applyContextPolicy,
  applySocketPolicy,
  applySpotNodeAdmission,
  createSocketEventWaiter,
  subscribeNoWait,
  trySocketPublish,
  waitForConnectionReady
} = require('./perf_multi_runtime');

const TOPIC = 'perf.topic';
const CONTROL_TOPIC = 'perf.control';
const SERVICE_NAME = 'perf.spot';
const SERVICE_TYPE_SPOT = 0x3002;

function trySpotPublish(spot, serviceName, topic, payload) {
  return trySocketPublish({
    publish(currentTopic, currentPayload, flags) {
      return spot.publish(serviceName, currentTopic, currentPayload, flags);
    }
  }, topic, payload);
}

function closeQuietly(resource) {
  try {
    resource?.close();
  } catch {
  }
}

function connectDataEndpoint(node, connectedDataEndpoints, endpoint) {
  if (!endpoint || connectedDataEndpoints.has(endpoint)) {
    return;
  }
  try {
    node.connectPeer(endpoint);
  } catch (error) {
    const text = String(error && error.message ? error.message : error);
    if (!/Device or resource busy|resource busy|already/i.test(text)) {
      throw error;
    }
  }
  connectedDataEndpoints.add(endpoint);
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'server', 'MULTI_SPOT');
  const registry = new zlink.Registry(ctx);
  const discovery = new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, SERVICE_NAME);
  const node = new zlink.SpotNode(ctx);
  const controlPub = new zlink.PubSocket(ctx);
  const controlSub = new zlink.SubSocket(ctx);
  const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
  const controlSubWaiter = createSocketEventWaiter(controlSub, POLLIN);
  let spot = null;
  const payload = createPayload(options.msgSize);
  let readyCount = 0;
  let connected = false;
  let startRequested = false;
  let stopRequested = false;
  let connectedControlEndpoint = '';
  const connectedDataEndpoints = new Set();
  let rl = null;

  try {
    configureTlsServer(node, options.transport);
    const registryPubEndpoint = await benchmarkEndpoint(options.transport, 'multi-spot-registry-pub');
    const registryRouterEndpoint = options.endpoint;
    const dataBindEndpoint = options.peerEndpoint ||
      await benchmarkEndpoint(options.transport, 'multi-spot-data');
    registry.bind(registryPubEndpoint, registryRouterEndpoint);
    discovery.connectRegistry(registryRouterEndpoint);
    node.attachDiscovery(discovery);
    applySpotNodeAdmission(node);
    node.bind(dataBindEndpoint);
    spot = node.createSpot();
    spot.setLinger(Number(process.env.PERF_MULTI_LINGER_MS ?? 0));
    applySocketPolicy(controlPub);
    applySocketPolicy(controlSub);
    controlPub.bind(options.controlEndpoint);
    controlSub.setSubscription(CONTROL_TOPIC);

    console.log(`READY,${options.endpoint}`);
    console.log(`CONTROL_READY,${options.controlEndpoint}`);

    rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    (async () => {
      for await (const line of rl) {
        if (line === `START,${options.msgSize}`) {
          startRequested = true;
        } else if (line.startsWith('CONNECT_CONTROL,')) {
          const clientEndpoint = line.slice('CONNECT_CONTROL,'.length).trim();
          if (!clientEndpoint || clientEndpoint === connectedControlEndpoint) {
            continue;
          }
          await waitForConnectionReady(controlSub, () => controlSub.connect(clientEndpoint));
          connectedControlEndpoint = clientEndpoint;
        } else if (line.startsWith('DATA_ENDPOINT,')) {
          const endpoint = line.slice('DATA_ENDPOINT,'.length).trim();
          connectDataEndpoint(node, connectedDataEndpoints, endpoint);
        } else if (line === 'STOP' || line === 'QUIT') {
          stopRequested = true;
          break;
        }
      }
    })();

    if (process.env.PERF_NODE_MULTI_SPOT_LOCAL_RUN !== '0') {
      while (!stopRequested && !startRequested) {
        await sleepImmediate();
      }
      if (!stopRequested) {
        await new Promise((resolve) => setTimeout(resolve, Math.max(0, options.duration * 1000 + 500)));
      }
      return;
    }

    const readyDeadlineMs = Date.now() + 20_000;
    while (!stopRequested && readyCount < options.clients && Date.now() < readyDeadlineMs) {
      let drained = false;
      while (true) {
        const received = subscribeNoWait(controlSub);
        if (!received) {
          break;
        }
        drained = true;
        const payloadText = received.parts[0].data().toString('utf8');
        if (payloadText === 'CONNECTED') {
          connected = true;
          continue;
        }
        if (payloadText.startsWith('DATA_ENDPOINT,')) {
          const endpoint = payloadText.slice('DATA_ENDPOINT,'.length).trim();
          connectDataEndpoint(node, connectedDataEndpoints, endpoint);
          continue;
        }
        if (payloadText.startsWith(`READY_COUNT,${options.msgSize},`)) {
          readyCount = Number(payloadText.split(',')[2]);
        }
      }
      stampPayload(payload, { phase: 0, runId: createRunId(1), msgSize: options.msgSize, seq: 0n });
      trySpotPublish(spot, SERVICE_NAME, TOPIC, payload);
      if (readyCount < options.clients && !drained) {
        await sleepImmediate();
      }
    }
    if (readyCount < options.clients) {
      throw new Error(`spot warmup readiness timeout ${readyCount}/${options.clients}`);
    }

    while (!stopRequested && !startRequested) {
      let drained = false;
      while (true) {
        const received = subscribeNoWait(controlSub);
        if (!received) {
          break;
        }
        drained = true;
        const payloadText = received.parts[0].data().toString('utf8');
        if (payloadText === 'CONNECTED') {
          connected = true;
          continue;
        }
        if (payloadText.startsWith('DATA_ENDPOINT,')) {
          const endpoint = payloadText.slice('DATA_ENDPOINT,'.length).trim();
          connectDataEndpoint(node, connectedDataEndpoints, endpoint);
          continue;
        }
        if (payloadText.startsWith(`READY_COUNT,${options.msgSize},`)) {
          readyCount = Number(payloadText.split(',')[2]);
        }
      }
      if (!startRequested && !drained) {
        await sleepImmediate();
      }
    }

    if (stopRequested) {
      return;
    }

    for (;;) {
      if (trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from(`START,${options.msgSize}`))) {
        break;
      }
      await controlPubWaiter.wait(POLLOUT);
    }

    const runId = createRunId(1);
    const activeStopNs = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
    let seq = 1n;
    while (process.hrtime.bigint() < activeStopNs) {
      stampPayload(payload, { phase: 1, runId, msgSize: options.msgSize, seq });
      if (trySpotPublish(spot, SERVICE_NAME, TOPIC, payload)) {
        seq += 1n;
        continue;
      }
      await sleepImmediate();
    }
    stampPayload(payload, { phase: 2, runId, msgSize: options.msgSize, seq });
    for (;;) {
      if (trySpotPublish(spot, SERVICE_NAME, TOPIC, payload)) {
        break;
      }
      await sleepImmediate();
    }
  } finally {
    rl?.close();
    controlSubWaiter.close();
    controlPubWaiter.close();
    closeQuietly(spot);
    closeQuietly(controlPub);
    closeQuietly(controlSub);
    closeQuietly(discovery);
    closeQuietly(registry);
    closeQuietly(node);
    closeQuietly(ctx);
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
