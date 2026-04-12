'use strict';

const zlink = require('../../dist/canonical');

const TOPIC = 'spot:child';

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

  try {
    node.connectPeer(options.endpoint);
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
    spot.close();
    node.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
