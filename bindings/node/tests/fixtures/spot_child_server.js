'use strict';

const zlink = require('../../dist/canonical');

const TOPIC = 'spot:child';
const SERVICE_TYPE_SPOT = 0x3002;
const SERVICE_NAME = 'spot-child-service';

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
  const pubSocket = new zlink.PubSocket(ctx);
  const subSocket = new zlink.SubSocket(ctx);
  let spot = null;

  try {
    node.attachPubSub(SERVICE_NAME, pubSocket, subSocket);
    spot = node.createSpot();
    pubSocket.bind(options.endpoint);
    console.log(`READY,${options.endpoint}`);

    const deadline = Date.now() + 5000;
    let sent = 0;
    while (Date.now() < deadline) {
      spot.publish(SERVICE_NAME, TOPIC, Buffer.from('payload'));
      sent += 1;
      if (sent === 1) {
        console.log('PUBLISHED');
      }
      await new Promise((resolve) => setImmediate(resolve));
    }
    if (sent > 0) {
      return;
    }
    throw new Error('publish timeout');
  } finally {
    if (spot) {
      spot.close();
    }
    node.close();
    subSocket.close();
    pubSocket.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
