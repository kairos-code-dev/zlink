// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist');
const {
  createPayload,
  latencyUsFromPayload,
  payloadPhase,
  summarizeMetrics
} = require('../common/perf_metrics');

function parseArgs(argv) {
  const options = { endpoint: '', msgSize: 256, duration: 2, clients: 1 };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--endpoint') {
      options.endpoint = argv[++i];
    } else if (argv[i] === '--msg-size') {
      options.msgSize = Number(argv[++i]);
    } else if (argv[i] === '--duration') {
      options.duration = Number(argv[++i]);
    } else if (argv[i] === '--clients') {
      options.clients = Number(argv[++i]);
    }
  }
  return options;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const server = new zlink.DealerSocket(ctx);
  const latenciesUs = [];
  let stopCount = 0;

  try {
    server.bind(options.endpoint);
    server.recvHandler((_, parts) => {
      const payload = parts[0].toBuffer();
      const phase = payloadPhase(payload);
      if (phase === 0) {
        latenciesUs.push(latencyUsFromPayload(payload));
      } else if (phase === 1) {
        stopCount += 1;
      }
    });

    console.log(`READY,${options.endpoint}`);
    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line === 'STOP') {
        break;
      }
    }

    if (stopCount > 0) {
      const resultLines = summarizeMetrics(
        'MULTI_DEALER_DEALER',
        'tcp',
        options.msgSize,
        latenciesUs,
        options.duration
      );
      for (const line of resultLines) {
        console.log(line);
      }
    }
  } finally {
    server.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
