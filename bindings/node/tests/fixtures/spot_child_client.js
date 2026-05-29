'use strict';

const zlink = require('@zlink-systems/zlink');

const TOPIC = 'spot:child';
const CHANNEL_NAME = 'spot-child-service';

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
  const ctx = zlink.createContext();
  const node = zlink.createSpotNode(ctx);
  let spot = null;

  try {
    node.setPubBind(options.bindEndpoint);
    node.connectPeer(options.peerEndpoint);
    spot = node.createSpot();
    spot.setSubscription(TOPIC);
    console.log('CLIENT_READY');

    const deadline = Date.now() + 5000;
    const received = new zlink.TopicMessage();
    while (Date.now() < deadline) {
      let hasReceived = false;
      try {
        hasReceived = spot.subscribe(received, zlink.RecvFlags.DontWait);
      } catch (error) {
        if (!(error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData)) {
          throw error;
        }
      }
      if (hasReceived) {
        console.log(`RECEIVED,${received.topic},${received.parts[0].data().toString()}`);
        return;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }
    throw new Error(`recv timeout: ${JSON.stringify({
      status: node.status(),
      peers: node.peers(),
      subjects: node.subjects()
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
