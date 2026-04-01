// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const {
  createMetricCollector,
  decodeMetricHeader,
  summarizeMetrics
} = require('../common/perf_metrics');

const TOPIC = 'perf.topic';
const SPOT_READY_EVENTS = zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED
  | zlink.ServiceMonitorEvent.SPOT_SUBSCRIPTION_READY_CHANGED
  | zlink.ServiceMonitorEvent.SPOT_READY_CHANGED
  | zlink.ServiceMonitorEvent.ERROR;

function parseArgs(argv) {
  const options = { endpoint: '', msgSize: 256, warmup: 1, duration: 2, clients: 1, recv: 'recv' };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--endpoint') {
      options.endpoint = argv[++i];
    } else if (argv[i] === '--msg-size') {
      options.msgSize = Number(argv[++i]);
    } else if (argv[i] === '--warmup') {
      options.warmup = Number(argv[++i]);
    } else if (argv[i] === '--duration') {
      options.duration = Number(argv[++i]);
    } else if (argv[i] === '--clients') {
      options.clients = Number(argv[++i]);
    } else if (argv[i] === '--recv') {
      options.recv = argv[++i];
    }
  }
  return options;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const collector = createMetricCollector({
    runId: 0,
    msgSize: options.msgSize
  });
  const slots = [];
  let stopCount = 0;
  let stop = false;
  let stopResolve;
  let timeoutId = null;
  const stopped = new Promise((resolve) => {
    stopResolve = resolve;
  });

  const handleDelivery = (received) => {
    const header = decodeMetricHeader(received.parts[0].toBuffer());
    if (!header) {
      return;
    }
    if (header.phase === 1) {
      stopCount += 1;
      if (stopCount >= options.clients) {
        stopResolve();
      }
      return;
    }
    collector.record(header, process.hrtime.bigint());
  };

  const waitForSubDeliveryReady = async (slot, timeoutMs) => {
    const deadline = Date.now() + timeoutMs;
    let filterApplied = false;
    let subscriptionReady = false;
    while (Date.now() < deadline) {
      const snapshot = slot.monitor.snapshot();
      const status = slot.node.statusSnapshot();
      const subjects = slot.node.subjectsSnapshot();
      if (
        filterApplied
        && subscriptionReady
        && (snapshot.stateFlags & zlink.MonitorState.READY) !== 0
        && snapshot.readyCount > 0
        && status.connectedPeerCount > 0
        && status.readySubjectCount > 0
        && subjects.some((subject) => (
          subject.role === zlink.SpotSocketRole.SUB
          && subject.subject === TOPIC
          && subject.readyPeerCount > 0
        ))
      ) {
        return;
      }
      const event = slot.monitor.tryRecv();
      if (!event) {
        await new Promise((resolve) => setImmediate(resolve));
        continue;
      }
      if (event.eventType === zlink.ServiceMonitorEvent.ERROR) {
        throw new Error(`spot client ready monitor error: ${JSON.stringify(event)}`);
      }
      if (event.eventType === zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED) {
        filterApplied = true;
        continue;
      }
      if (
        event.eventType === zlink.ServiceMonitorEvent.SPOT_SUBSCRIPTION_READY_CHANGED
        && event.endpoint === options.endpoint
        && event.value > 0
      ) {
        subscriptionReady = true;
      }
    }
    throw new Error(`spot client ready timeout: ${JSON.stringify({
      filterApplied,
      subscriptionReady,
      snapshot: slot.monitor.snapshot(),
      status: slot.node.statusSnapshot(),
      peers: slot.node.peersSnapshot(),
      subjects: slot.node.subjectsSnapshot()
    })}`);
  };

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const node = new zlink.SpotNode(ctx);
      const spot = new zlink.Spot(node);
      const monitor = spot.openMonitor(SPOT_READY_EVENTS);
      slots.push({ node, spot, monitor });
    }

    for (const slot of slots) {
      slot.node.connectPeer(options.endpoint);
      slot.spot.setSubscription(TOPIC);
      if (options.recv === 'callback') {
        slot.spot.subscribeHandler((routingId, topic, parts) => {
          void routingId;
          void topic;
          handleDelivery({ parts });
        });
      } else {
        (async () => {
          while (!stop) {
            let received = null;
            try {
              received = slot.spot.trySubscribe();
            } catch (_) {
              await new Promise((resolve) => setImmediate(resolve));
              continue;
            }
            if (!received) {
              await new Promise((resolve) => setImmediate(resolve));
              continue;
            }
            handleDelivery(received);
          }
        })();
      }
    }

    for (const slot of slots) {
      await waitForSubDeliveryReady(slot, 10000);
      slot.monitor.close();
      slot.monitor = null;
    }

    console.log('CLIENT_READY');

    await Promise.race([
      stopped,
      new Promise((_, reject) => {
        timeoutId = setTimeout(
          () => {
            console.error(
              `spot client debug: ${JSON.stringify(slots.map((slot) => ({
                status: slot.node.statusSnapshot(),
                peers: slot.node.peersSnapshot(),
                subjects: slot.node.subjectsSnapshot()
              })))}`
            );
            reject(new Error('spot client timeout'));
          },
          Math.ceil((options.warmup + options.duration + 10) * 1000)
        );
      })
    ]);
    stop = true;
    if (timeoutId) {
      clearTimeout(timeoutId);
      timeoutId = null;
    }

    const result = await collector.finish();
    const lines = summarizeMetrics(
      'MULTI_SPOT',
      'tcp',
      options.msgSize,
      result.latenciesUs,
      options.duration
    );
    for (const line of lines) {
      console.log(line);
    }
  } finally {
    if (timeoutId) {
      clearTimeout(timeoutId);
      timeoutId = null;
    }
    await collector.close();
    for (const slot of slots) {
      if (slot.monitor) {
        slot.monitor.close();
      }
      slot.spot.close();
      slot.node.close();
    }
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
