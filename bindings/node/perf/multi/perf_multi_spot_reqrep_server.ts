// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { sleepImmediate } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { subscribeNoWait } = require('./perf_multi_runtime');

const CONTROL_TOPIC = 'perf.control';
const SERVICE_NAME = 'perf.spot';

function tryRecvRouted(spot) {
  try {
    return spot.recvRouted(zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const dealer = new zlink.DealerSocket(ctx);
  const controlSub = new zlink.SubSocket(ctx);
  let spot = null;
  let readyCount = 0;
  let connected = false;
  let startRequested = false;
  let stop = false;

  try {
    node.attachChannelDealerManual(SERVICE_NAME, dealer);
    node.bind(options.peerEndpoint);
    spot = node.createSpot();
    spot.onDispatchEvent(() => {
      while (true) {
        const received = tryRecvRouted(spot);
        if (!received) {
          return;
        }
        try {
          received.reply(received.parts.map((part) => part.data()));
        } finally {
          received.close();
        }
      }
    });

    controlSub.setSubscription(CONTROL_TOPIC);
    controlSub.connect(options.controlEndpoint);

    console.log(`READY,${options.endpoint}`);
    console.log(`ROUTE_READY,${node.routingId.toBytes().toString('hex')},${spot.routingId.toBytes().toString('hex')}`);

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    (async () => {
      for await (const line of rl) {
        if (line === `START,${options.msgSize}`) {
          startRequested = true;
        } else if (line === 'STOP') {
          stop = true;
        }
      }
    })();

    while (!(connected && readyCount >= options.clients && startRequested && stop)) {
      while (true) {
        const received = subscribeNoWait(controlSub);
        if (!received) {
          break;
        }
        const payloadText = received.parts[0].data().toString('utf8');
        if (payloadText === 'CONNECTED') {
          connected = true;
          continue;
        }
        if (payloadText.startsWith(`READY_COUNT,${options.msgSize},`)) {
          readyCount = Number(payloadText.split(',')[2]);
        }
      }
      await sleepImmediate();
    }
  } finally {
    if (spot) {
      spot.close();
    }
    controlSub.close();
    dealer.close();
    node.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
