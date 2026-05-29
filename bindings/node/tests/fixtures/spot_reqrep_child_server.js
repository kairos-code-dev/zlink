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

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const ctx = zlink.createContext();
  const node = zlink.createSpotNode(ctx);
  const spot = node.createSpot();
  const received = new zlink.Received();

  try {
    node.setRoutingId(rid('REQREP_SERVER_NODE'));
    spot.setRoutingId(rid('REQREP_SERVER_SPOT'));
    node.setRouterBind(options.routerEndpoint);
    node.setPubBind(options.peerEndpoint);
    if (options.connectEndpoint) {
      node.connectPeer(options.connectEndpoint);
    }
    console.log('SERVER_READY');

    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      let hasReceived = false;
      try {
        hasReceived = spot.recvRouted(received, zlink.RecvFlags.DontWait);
      } catch (error) {
        if (!(error instanceof zlink.RecvError
            && (error.result === zlink.RecvResult.NoData
                || /Resource temporarily unavailable|Device or resource busy/i.test(String(error.message))))) {
          throw error;
        }
      }
      if (hasReceived) {
        received.reply().message(Buffer.from('mesh-pong')).submit();
        console.log('SERVER_REPLIED');
        return;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }
    throw new Error(`server request timeout: ${JSON.stringify({
      status: node.status(),
      peers: node.peers(),
      subjects: node.subjects()
    }, (_key, value) => typeof value === 'bigint' ? value.toString() : value)}`);
  } finally {
    received.close();
    spot.close();
    node.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
