// SPDX-License-Identifier: MPL-2.0

'use strict';

const path = require('node:path');
const {
  parseCommonArgs,
  summarizeMetrics,
  writeReport
} = require('../common/perf_metrics');
const { runPairBenchmark } = require('./perf_pair');
const { runPubSubBenchmark } = require('./perf_pubsub');
const { runDealerDealerBenchmark } = require('./perf_dealer_dealer');
const { runDealerRouterBenchmark } = require('./perf_dealer_router');
const { runRouterRouterBenchmark } = require('./perf_router_router');
const { runSpotBenchmark } = require('./perf_spot');

const PATTERNS = {
  PAIR: runPairBenchmark,
  PUBSUB: runPubSubBenchmark,
  DEALER_DEALER: runDealerDealerBenchmark,
  DEALER_ROUTER: runDealerRouterBenchmark,
  ROUTER_ROUTER: runRouterRouterBenchmark,
  SPOT: runSpotBenchmark
};

async function main() {
  const options = parseCommonArgs(process.argv.slice(2), {
    pattern: 'ALL',
    recv: 'callback',
    duration: 2,
    warmup: 1,
    msgSizes: [256],
    resultsDir: path.join(process.cwd(), 'perf', 'results')
  });

  if (options.recv !== 'callback') {
    throw new Error('single perf supports only --recv callback');
  }

  const names = options.pattern === 'ALL'
    ? Object.keys(PATTERNS)
    : options.pattern.split(',').map((value) => value.trim().toUpperCase());
  const resultLines = [];

  for (const name of names) {
    const runner = PATTERNS[name];
    if (!runner) {
      throw new Error(`unsupported single pattern: ${name}`);
    }
    for (const msgSize of options.msgSizes) {
      const latenciesUs = await runner(msgSize, options);
      const lines = summarizeMetrics(name, 'inproc', msgSize, latenciesUs, options.duration);
      for (const line of lines) {
        console.log(line);
        resultLines.push(line);
      }
    }
  }

  const report = writeReport(
    path.join(options.resultsDir, 'single', 'report'),
    options.recv,
    resultLines,
    options
  );
  console.log(`report=${report}`);
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
