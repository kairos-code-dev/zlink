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
  createSocketEventWaiter,
  emitMultiSocketHwmDetail,
  publishControlUntilSent,
  subscribeNoWait,
} = require('./perf_multi_runtime');

const CONTROL_TOPIC = 'bench';
const SERVER_NODE_ROUTING_ID = zlink.RoutingId.fromBytes(
  Buffer.from('PERF_SPOT_REQREP_NODE', 'ascii')
);
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.fromBytes(
  Buffer.from('PERF_SPOT_REQREP_SPOT', 'ascii')
);
const TRACE = process.env.PERF_MULTI_SPOT_REQREP_TRACE === '1';

function trace(message) {
  if (TRACE) {
    console.error(`[multi-spot-reqrep-server] ${message}`);
  }
}

function closeQuietly(resource) {
  try {
    resource?.close();
  } catch (err) {
    console.error(`[multi-spot-reqrep-server] close failed: ${err}`);
  }
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'server', 'MULTI_SPOT_REQREP');
  const node = new zlink.SpotNode(ctx);
  const controlPub = new zlink.PubSocket(ctx);
  const controlSub = new zlink.SubSocket(ctx);
  const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
  let spot = null;
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
    trace('ready');

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
          connectedControlEndpoint = clientEndpoint;
          controlSub.connect(clientEndpoint);
          console.log(`CONTROL_CONNECTED,${clientEndpoint}`);
        } else if (line === 'STOP' || line === 'QUIT') {
          stop = true;
        }
      }
    })();

    spot.onRoutedReceive((received) => {
      try {
        let reply = received.reply();
        for (const part of received.parts) reply = reply.message(part);
        reply.submit();
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
          continue;
        }
        if (payloadText.startsWith(`READY_COUNT,${options.msgSize},`)) {
          readyCount = Number(payloadText.split(',')[2]);
        }
      }
      if (!(connected && readyCount >= options.clients && startRequested) && !drained) {
        await sleepImmediate();
      }
    }
    trace(`control-ready connected=${connected} ready=${readyCount} start=${startRequested}`);

    if (stop) {
      return;
    }

    await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `START,${options.msgSize}`);

    while (!stop) {
      await sleepImmediate();
    }
    trace('stop');
  } finally {
    stop = true;
    rl?.close();
    controlPubWaiter.close();
    closeQuietly(spot);
    closeQuietly(controlPub);
    closeQuietly(controlSub);
    closeQuietly(node);
    closeQuietly(ctx);
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
