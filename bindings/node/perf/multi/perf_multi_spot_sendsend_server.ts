// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { configureTlsServer } = require('../common/perf_tls');
const { benchmarkEndpoint, parseMultiArgs } = require('./perf_multi_common');
const {
  POLLIN,
  POLLOUT,
  applyAutoHwmMsgUnit,
  applyContextPolicy,
  applySocketPolicy,
  applySpotNodeAdmission,
  createSocketEventWaiter,
  emitMultiSocketHwmDetail,
  pollEvents,
  publishControlUntilSent,
  subscribeNoWait,
} = require('./perf_multi_runtime');

const CONTROL_TOPIC = 'bench';
const SERVER_NODE_ROUTING_ID = zlink.RoutingId.from(
  Buffer.from('PERF_SPOT_SENDSEND_NODE', 'ascii')
);
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.from(
  Buffer.from('PERF_SPOT_SENDSEND_SPOT', 'ascii')
);
const TRACE = process.env.PERF_MULTI_SPOT_SENDSEND_TRACE === '1';

function trace(message) {
  if (TRACE) {
    console.error(`[multi-spot-sendsend-server] ${message}`);
  }
}

function sleepMillis(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function echoRouted(spot, received) {
  if (!received.routingId || !received.spotRid || received.parts.length === 0) {
    return;
  }
  try {
    let op = received.send();
    for (const part of received.parts) {
      op = op.message(part);
    }
    op.flags(zlink.SendFlags.DontWait).submit();
  } catch (error) {
    if (!(error instanceof zlink.SubmitError
          && (error.result === zlink.SubmitResult.Backpressured
              || error.result === zlink.SubmitResult.NotConnected
              || error.result === zlink.SubmitResult.NotFound
              || error.result === zlink.SubmitResult.NotAdmitted))) {
      throw error;
    }
  }
}

function closeQuietly(resource) {
  try {
    resource?.close();
  } catch (err) {
    console.error(`[multi-spot-sendsend-server] close failed: ${err}`);
  }
}

function recvRoutedNoWait(spot, received) {
  try {
    return spot.recvRouted(received, zlink.RecvFlags.DontWait);
  } catch (error) {
    const message = String(error?.message ?? error);
    if (error instanceof zlink.RecvError
        && (error.result === zlink.RecvResult.NoData
            || /Device or resource busy|Resource temporarily unavailable/i.test(message))) {
      return false;
    }
    throw error;
  }
}

function drainRoutedMessages(spot, received) {
  let count = 0;
  while (recvRoutedNoWait(spot, received)) {
    count += 1;
    echoRouted(spot, received);
  }
  return count;
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = zlink.createContext();
  applyContextPolicy(ctx, 'server', 'MULTI_SPOT_SENDSEND');
  const node = zlink.createSpotNode(ctx);
  let spot = null;
  const controlPub = zlink.createPubSocket(ctx);
  const controlSub = zlink.createSubSocket(ctx);
  const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
  const connectedDataEndpoints = new Set();
  let readyCount = 0;
  let connected = false;
  let dataReadySince = 0;
  let startRequested = false;
  let stop = false;
  let connectedControlEndpoint = '';
  let controlPoller = null;
  let controlEvents = null;
  let dataPoller = null;
  let dataEvents = null;
  let rl = null;
  const routedReceived = new zlink.Received();

  try {
    applySpotNodeAdmission(node);
    configureTlsServer(node, options.transport);
    node.setRoutingId(SERVER_NODE_ROUTING_ID);
    node.setRouterBind(
      await benchmarkEndpoint(options.transport, `multi-spot-sendsend-router-${process.pid}`)
    );
    node.setPubBind(options.peerEndpoint);
    spot = node.createSpot();
    spot.setRoutingId(SERVER_SPOT_ROUTING_ID);

    applySocketPolicy(controlPub);
    applySocketPolicy(controlSub);
    applyAutoHwmMsgUnit(ctx, options.msgSize);
    ctx.recalculateAutoHwm();
    emitMultiSocketHwmDetail(controlPub, 'spotnode_control_pub', options.transport, options.msgSize);
    emitMultiSocketHwmDetail(controlSub, 'spotnode_control_sub', options.transport, options.msgSize);
    emitMultiSocketHwmDetail(node, 'spotnode_data', options.transport, options.msgSize);
    controlPub.bind(options.controlEndpoint);
    controlSub.setSubscription(CONTROL_TOPIC);
    controlPoller = zlink.createPoller();
    controlEvents = zlink.createPollEvents(1);
    controlPoller.add(controlSub, pollEvents(POLLIN), 0);
    dataPoller = zlink.createPoller();
    dataEvents = zlink.createPollEvents(1);
    dataPoller.add(spot, pollEvents(POLLIN), 0);
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

    const dataRouteSettleMs = 250;
    const isStartReady = () => {
      const dataReady = connectedDataEndpoints.size > 0
        && node.status().connectedPeerCount >= Math.max(1, connectedDataEndpoints.size);
      if (dataReady) {
        if (dataReadySince === 0) {
          dataReadySince = Date.now();
        }
      } else {
        dataReadySince = 0;
      }
      return connected && dataReady && Date.now() - dataReadySince >= dataRouteSettleMs
        && readyCount >= options.clients && startRequested;
    };
    while (!stop && !isStartReady()) {
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
      if (!isStartReady() && !drained) {
        controlPoller.wait(controlEvents, 50);
      }
      await sleepMillis(0);
    }
    if (stop) {
      return;
    }
    trace(`control-ready connected=${connected} ready=${readyCount} start=${startRequested}`);

    await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `START,${options.msgSize}`);

    let idleWaits = 0;
    while (!stop) {
      if (drainRoutedMessages(spot, routedReceived) === 0) {
        dataPoller.wait(dataEvents, 1);
        idleWaits += 1;
        if (idleWaits >= 16) {
          idleWaits = 0;
          await sleepMillis(0);
        }
      } else {
        idleWaits = 0;
      }
    }
    trace('stop');
  } finally {
    stop = true;
    dataEvents?.close();
    dataPoller?.close();
    controlEvents?.close();
    controlPoller?.close();
    rl?.close();
    controlPubWaiter.close();
    closeQuietly(routedReceived);
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
