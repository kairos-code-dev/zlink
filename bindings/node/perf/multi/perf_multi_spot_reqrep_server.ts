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
  Buffer.from('PERF_SPOT_REQREP_NODE', 'ascii')
);
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.from(
  Buffer.from('PERF_SPOT_REQREP_SPOT', 'ascii')
);
const TRACE = process.env.PERF_MULTI_SPOT_REQREP_TRACE === '1';

function sleepMillis(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function trace(message) {
  if (TRACE) {
    console.error(`[multi-spot-reqrep-server] ${message}`);
  }
}

function traceValue(label, value) {
  trace(`${label}=${JSON.stringify(value, (_key, item) =>
    typeof item === 'bigint' ? item.toString() : item
  )}`);
}

function closeQuietly(resource) {
  try {
    resource?.close();
  } catch (err) {
    console.error(`[multi-spot-reqrep-server] close failed: ${err}`);
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

function drainRoutedRequests(spot) {
  const received = new zlink.Received();
  let count = 0;
  try {
    while (recvRoutedNoWait(spot, received)) {
      count += 1;
      let reply = received.reply();
      for (const part of received.parts) reply = reply.message(part);
      reply.submit();
    }
  } finally {
    received.close();
  }
  if (count > 0) {
    trace(`drained routed requests=${count}`);
  }
  return count;
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = zlink.createContext();
  applyContextPolicy(ctx, 'server', 'MULTI_SPOT_REQREP');
  const node = zlink.createSpotNode(ctx);
  const controlPub = zlink.createPubSocket(ctx);
  const controlSub = zlink.createSubSocket(ctx);
  const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
  let spot = null;
  let readyCount = 0;
  let connected = false;
  let dataConnected = false;
  let expectedDataPeers = 0;
  let dataReadySince = 0;
  let startRequested = false;
  let stop = false;
  let connectedControlEndpoint = '';
  let controlPoller = null;
  let controlEvents = null;
  let rl = null;

  try {
    applySpotNodeAdmission(node);
    configureTlsServer(node, options.transport);
    node.setRoutingId(SERVER_NODE_ROUTING_ID);
    node.setRouterBind(
      await benchmarkEndpoint(options.transport, `multi-spot-reqrep-router-${process.pid}`)
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

    const dataRouteSettleMs = 250;
    const isStartReady = () => {
      const dataReady = dataConnected && node.status().connectedPeerCount >= Math.max(1, expectedDataPeers);
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
        try {
          const payloadText = received.parts[0].data().toString('utf8');
          if (payloadText === 'CONNECTED') {
            connected = true;
            continue;
          }
          if (payloadText.startsWith('DATA_ENDPOINT,')) {
            const endpoint = payloadText.slice('DATA_ENDPOINT,'.length).trim();
            if (endpoint && !dataConnected) {
              node.connectPeer(endpoint);
              dataConnected = true;
              expectedDataPeers = 1;
            }
            continue;
          }
          if (payloadText.startsWith(`READY_COUNT,${options.msgSize},`)) {
            readyCount = Number(payloadText.split(',')[2]);
          }
        } finally {
          received.close();
        }
      }
      if (!isStartReady() && !drained) {
        controlPoller.wait(controlEvents, 50);
      }
      await sleepMillis(0);
    }
    trace(`control-ready connected=${connected} data=${dataConnected} ready=${readyCount} start=${startRequested}`);
    traceValue('status', node.status());
    traceValue('peers', node.peers());
    traceValue('subjects', node.subjects());

    if (stop) {
      return;
    }

    await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `START,${options.msgSize}`);

    while (!stop) {
      if (drainRoutedRequests(spot) === 0) {
        await sleepMillis(1);
      }
    }
    trace('stop');
  } finally {
    stop = true;
    controlEvents?.close();
    controlPoller?.close();
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
