'use strict';

const zlink = require('@zlink-systems/zlink');

function parseArgs(argv) {
  const options = {
    peerEndpoint: '',
    routerEndpoint: '',
    connectEndpoint: ''
  };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--peer-endpoint') {
      options.peerEndpoint = argv[++i];
    } else if (argv[i] === '--router-endpoint') {
      options.routerEndpoint = argv[++i];
    } else if (argv[i] === '--connect-endpoint') {
      options.connectEndpoint = argv[++i];
    }
  }
  return options;
}

function rid(text) {
  return zlink.RoutingId.from(Buffer.from(text, 'ascii'));
}

async function waitForPeer(node) {
  const deadline = Date.now() + 5000;
  while (Date.now() < deadline) {
    if (node.status().connectedPeerCount > 0) {
      return;
    }
    await new Promise((resolve) => setImmediate(resolve));
  }
  throw new Error('client peer connection timeout');
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const ctx = zlink.createContext();
  const node = zlink.createSpotNode(ctx);
  const spot = node.createSpot();

  try {
    node.setRoutingId(rid('REQREP_CLIENT_NODE'));
    spot.setRoutingId(rid('REQREP_CLIENT_SPOT'));
    node.setRouterBind(options.routerEndpoint);
    node.setPubBind(options.peerEndpoint);
    node.connectPeer(options.connectEndpoint);
    await waitForPeer(node);
    console.log('CLIENT_READY');

    const reply = await spot.requestToSpot(
      rid('REQREP_SERVER_NODE'),
      rid('REQREP_SERVER_SPOT')
    ).message(Buffer.from('mesh-ping')).timeout(2000).submitAsync();
    console.log(`CLIENT_REPLY,${reply[0].data().toString()}`);
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
