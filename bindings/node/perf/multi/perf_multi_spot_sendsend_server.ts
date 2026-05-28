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

function sleepMillis(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function echoRouted(received) {
  if (!received.routingId || !received.spotRid || received.parts.length === 0) {
    received.close();
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
  } finally {
    received.close();
  }
}

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
  let controlPoller = null;
  let controlEvents = null;
  let rl = null;

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
    controlPoller = new zlink.Poller();
    controlEvents = new zlink.PollEvents(1);
    controlPoller.add(controlSub, pollEvents(POLLIN), 0);

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

    spot.onDispatchEvent((info) => {
      if (info.event !== zlink.SpotDispatchEvent.RoutedReadable) {
        return;
      }
      while (true) {
        const received = spot.recvRouted(zlink.RecvFlags.DontWait);
        if (!received) {
          return;
        }
        echoRouted(received);
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
        controlPoller.wait(controlEvents, 50);
      }
      await sleepMillis(0);
    }
    if (stop) {
      return;
    }

    await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `START,${options.msgSize}`);

    while (!stop) {
      await sleepMillis(50);
    }
  } finally {
    stop = true;
    controlEvents?.close();
    controlPoller?.close();
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
