'use strict';

const zlink = require('../../dist');

const TOPIC = 'spot:child:monitor';

function parseArgs(argv) {
  const options = { endpoint: '' };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--endpoint') {
      options.endpoint = argv[++i];
    }
  }
  return options;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);
  const monitor = spot.monitorOpen(
    zlink.ServiceMonitorEvent.SPOT_SUB_DELIVERY_READY_CHANGED
      | zlink.ServiceMonitorEvent.ERROR
  );

  try {
    node.connectPeer(options.endpoint);
    spot.setSubscription(TOPIC);
    console.log('CLIENT_READY');

    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      const event = monitor.tryRecv();
      if (!event) {
        await new Promise((resolve) => setImmediate(resolve));
        continue;
      }
      if (event.eventType === zlink.ServiceMonitorEvent.ERROR) {
        throw new Error(`monitor error: ${JSON.stringify(event)}`);
      }
      if (event.eventType === zlink.ServiceMonitorEvent.SPOT_SUB_DELIVERY_READY_CHANGED && event.value > 0) {
        const status = node.statusSnapshot();
        const subjects = node.subjectsSnapshot();
        if (status.connectedPeerCount <= 0 || status.readySubjectCount <= 0) {
          throw new Error(`monitor false positive: ${JSON.stringify({
            event,
            status,
            peers: node.peersSnapshot(),
            subjects
          })}`);
        }
        console.log(`EVENT_READY,${event.value}`);
        return;
      }
    }

    throw new Error(`monitor timeout: ${JSON.stringify({
      snapshot: monitor.snapshot(),
      status: node.statusSnapshot(),
      peers: node.peersSnapshot(),
      subjects: node.subjectsSnapshot()
    })}`);
  } finally {
    monitor.close();
    spot.close();
    node.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
