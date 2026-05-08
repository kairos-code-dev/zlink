'use strict';

const zlink = require('../..');

const TOPIC = 'spot:child';
const SERVICE_NAME = 'spot-child-service';

function parseArgs(argv) {
  const options = { bindEndpoint: '', peerEndpoint: '' };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--bind-endpoint') {
      options.bindEndpoint = argv[++i];
    } else if (argv[i] === '--peer-endpoint') {
      options.peerEndpoint = argv[++i];
    }
  }
  return options;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  let spot = null;

  try {
    node.bind(options.bindEndpoint);
    node.connectPeer(options.peerEndpoint);
    spot = node.createSpot();
    spot.setSubscription(TOPIC);
    console.log('CLIENT_READY');

    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      let received = null;
      try {
        received = spot.subscribe(zlink.RecvFlags.DontWait);
      } catch (error) {
        if (!(error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData)) {
          throw error;
        }
      }
      if (received) {
        if (received.serviceName !== SERVICE_NAME) {
          throw new Error(`unexpected service name: ${received.serviceName}`);
        }
        console.log(`RECEIVED,${received.topic},${received.parts[0].data().toString()}`);
        return;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }
    throw new Error(`recv timeout: ${JSON.stringify({
      status: node.statusSnapshot(),
      peers: node.peersSnapshot(),
      subjects: node.subjectsSnapshot()
    })}`);
  } finally {
    if (spot) {
      spot.close();
    }
    node.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
