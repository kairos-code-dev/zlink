'use strict';

const readline = require('node:readline');
const zlink = require('../../dist');

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
    node.bind(options.endpoint);
    console.log(`READY,${options.endpoint}`);

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line !== 'START') {
        continue;
      }
      const deadline = Date.now() + 5000;
      let sent = 0;
      while (Date.now() < deadline) {
        if (spot.tryPublish(TOPIC, Buffer.from('payload')) === zlink.SendResult.Sent) {
          sent += 1;
          if (sent === 1) {
            console.log('PUBLISHED');
          }
        }
        await new Promise((resolve) => setImmediate(resolve));
      }
      if (sent > 0) {
        return;
      }
      throw new Error('publish timeout');
    }
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
