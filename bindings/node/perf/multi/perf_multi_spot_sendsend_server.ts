// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { configureTlsServer } = require('../common/perf_tls');
const { sleepImmediate } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const {
  POLLOUT,
  applyAutoHwmMsgUnit,
  applyContextPolicy,
  applySocketPolicy,
  applySpotNodeAdmission,
  createCallbackEventWaiter,
  createSocketEventWaiter,
  emitMultiSocketHwmDetail,
  publishControlUntilSent,
  subscribeNoWait,
} = require('./perf_multi_runtime');

const CONTROL_TOPIC = 'bench';
const SERVER_NODE_ROUTING_ID = zlink.RoutingId.fromBytes(
  Buffer.from('PERF_SPOT_SENDSEND_NODE', 'ascii')
);
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.fromBytes(
  Buffer.from('PERF_SPOT_SENDSEND_SPOT', 'ascii')
);

function closeQuietly(resource) {
  try {
    resource?.close();
  } catch (err) {
    console.error(`[multi-spot-sendsend-server] close failed: ${err}`);
  }
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'server', 'MULTI_SPOT_SENDSEND');
  const node = new zlink.SpotNode(ctx);
  let spot = null;
  const controlPub = new zlink.PubSocket(ctx);
  const controlSub = new zlink.SubSocket(ctx);
  const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
  const connectedDataEndpoints = new Set();
  let readyCount = 0;
  let connected = false;
  let startRequested = false;
  let stop = false;
  let connectedControlEndpoint = '';
  let rl = null;

  try {
    applySpotNodeAdmission(node);
    configureTlsServer(node, options.transport);
    node.setRoutingId(SERVER_NODE_ROUTING_ID);
    node.bind(options.peerEndpoint);
    spot = node.createSpot();
    spot.setRoutingId(SERVER_SPOT_ROUTING_ID);
    const spotSendWaiter = createCallbackEventWaiter((handler) => spot.onSendReady(handler));

    applySocketPolicy(controlPub);
    applySocketPolicy(controlSub);
    applyAutoHwmMsgUnit(controlPub, options.msgSize);
    applyAutoHwmMsgUnit(controlSub, options.msgSize);
    ctx.recalculateAutoHwm();
    emitMultiSocketHwmDetail(controlPub, 'spotnode_control_pub', options.transport, options.msgSize);
    emitMultiSocketHwmDetail(controlSub, 'spotnode_control_sub', options.transport, options.msgSize);
    emitMultiSocketHwmDetail(node, 'spotnode_data', options.transport, options.msgSize);
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
          if (clientEndpoint && clientEndpoint !== connectedControlEndpoint) {
            connectedControlEndpoint = clientEndpoint;
            controlSub.connect(clientEndpoint);
            console.log(`CONTROL_CONNECTED,${clientEndpoint}`);
          }
        } else if (line === 'STOP' || line === 'QUIT') {
          stop = true;
        }
      }
    })();

    spot.onRoutedReceive(async (received) => {
      try {
        while (!stop) {
          const sent = received.send().message(received.parts[0].data())
            .flags(zlink.SendFlags.DontWait)
            .submit();
          if (sent) {
            break;
          }
          await spotSendWaiter.wait();
        }
      } finally {
        received.close();
      }
    });

    while (!stop && !(connected && readyCount >= options.clients && startRequested)) {
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
        } else if (payloadText.startsWith('DATA_ENDPOINT,')) {
          const endpoint = payloadText.slice('DATA_ENDPOINT,'.length).trim();
          if (endpoint && !connectedDataEndpoints.has(endpoint)) {
            node.connectPeer(endpoint);
            connectedDataEndpoints.add(endpoint);
          }
        } else if (payloadText.startsWith(`READY_COUNT,${options.msgSize},`)) {
          readyCount = Number(payloadText.split(',')[2]);
        }
        received.close();
      }
      if (!(connected && readyCount >= options.clients && startRequested) && !drained) {
        await sleepImmediate();
      }
    }
    if (stop) {
      return;
    }

    await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `START,${options.msgSize}`);

    while (!stop) {
      await new Promise((resolve) => setImmediate(resolve));
    }
  } finally {
    stop = true;
    rl?.close();
    controlPubWaiter.close();
    closeQuietly(controlSub);
    closeQuietly(controlPub);
    closeQuietly(spot);
    closeQuietly(node);
    closeQuietly(ctx);
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
