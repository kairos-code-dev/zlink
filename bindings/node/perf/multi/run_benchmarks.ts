// SPDX-License-Identifier: MPL-2.0

'use strict';

const path = require('node:path');
const { once } = require('node:events');
const { spawn } = require('node:child_process');
const {
  buildEffectiveOptions,
  defaultMultiClients,
  defaultMultiMsgSizes,
  DEFAULT_MULTI_TRANSPORTS,
  formatTableRows,
  parseCommonArgs,
  primaryMetricsFromResultLines,
  resolveMultiPatternNames,
  writeReport
} = require('../common/perf_metrics');
const {
  reservePort,
} = require('./perf_multi_common');
const {
  attachProcessCapture,
  spawnMultiPair,
  stopServer,
  waitForLine
} = require('./perf_multi_orchestrator');

function usage() {
  console.log(`Usage: bindings/node/perf/run_benchmarks_multi.sh [options]

Measure current zlink Node multi-pattern performance.

Options:
  -h, --help            Show this help.
  --pattern NAME        Pattern list (comma-separated) or ALL.
  --results-dir PATH    Override result root directory.
  --results-tag NAME    Optional tag in saved result filename.
  --runs N              Iterations per configuration (default: 1).
  --duration N          Override multi duration seconds (default: 5).
  --warmup N            Override multi warmup seconds (default: 2).
  --msg-sizes LIST      Comma-separated sizes.
  --transports LIST     Comma-separated transports (default: tcp).
  --clients N           Override number of client sockets per pattern (default: 8).

Notes:
  - result is saved under perf/results/multi/report/ as
    perf_node_multi_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt.`);
}

async function spawnSharedStreamPair(args) {
  const serverPath = path.join(__dirname, 'perf_multi_stream_server.js');
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const resultLines = [];
  const serverArgs = [
    '--endpoint', endpoint,
    '--msg-size', String(args.msgSize),
    '--warmup', String(args.warmup),
    '--duration', String(args.duration),
    '--clients', String(args.clients)
  ];
  const server = spawn(process.execPath, [serverPath, ...serverArgs], {
    cwd: process.cwd(),
    stdio: ['pipe', 'pipe', 'pipe'],
    detached: true
  });
  attachProcessCapture(server, resultLines);
  await waitForLine(server, `READY,${endpoint}`, 'perf_multi_stream_server.js', 5000);

  const clientPath = path.join(__dirname, 'perf_multi_stream_client.js');
  const clientArgs = [
    '--endpoint', endpoint,
    '--msg-size', String(args.msgSize),
    '--warmup', String(args.warmup),
    '--duration', String(args.duration),
    '--clients', String(args.clients)
  ];
  const client = spawn(process.execPath, [clientPath, ...clientArgs], {
    cwd: process.cwd(),
    stdio: ['pipe', 'pipe', 'pipe'],
    detached: true
  });
  attachProcessCapture(client, resultLines, 'RESULT,current,');
  const [code] = await once(client, 'exit');
  if (code !== 0) {
    throw new Error(`shared stream client failed: ${code}`);
  }

  await stopServer(server, 'perf_multi_stream_server.js');
  return resultLines;
}

const MULTI_PATTERN_RUNNERS = {
  MULTI_DEALER_DEALER: {
    server: 'perf_multi_dealer_dealer_server.js',
    client: 'perf_multi_dealer_dealer_client.js',
  },
  MULTI_PUBSUB: {
    server: 'perf_multi_pubsub_server.js',
    client: 'perf_multi_pubsub_client.js',
  },
  MULTI_DEALER_ROUTER: {
    server: 'perf_multi_dealer_router_server.js',
    client: 'perf_multi_dealer_router_client.js',
  },
  MULTI_ROUTER_ROUTER: {
    server: 'perf_multi_router_router_server.js',
    client: 'perf_multi_router_router_client.js',
  },
  MULTI_SPOT: {
    server: 'perf_multi_spot_server.js',
    client: 'perf_multi_spot_client.js'
  },
  MULTI_STREAM: {
    run: spawnSharedStreamPair
  }
};

function assertMultiPatternAllowed(patternName) {
  const runner = MULTI_PATTERN_RUNNERS[patternName];
  if (!runner) {
    throw new Error(`unsupported multi pattern: ${patternName}`);
  }
  return runner;
}

async function runMultiPattern(patternName, options, msgSize) {
  const runner = assertMultiPatternAllowed(patternName);
  if (typeof runner.run === 'function') {
    return runner.run({ ...options, pattern: patternName, msgSize, clients: options.clients });
  }
  return spawnMultiPair(runner.server, runner.client, {
    ...options,
    pattern: patternName,
    msgSize,
    clients: options.clients
  });
}

async function main() {
  const options = parseCommonArgs(process.argv.slice(2), {
    pattern: 'ALL',
    duration: 5,
    warmup: 2,
    msgSizes: defaultMultiMsgSizes(['MULTI_DEALER_DEALER'], false),
    resultsDir: path.join(process.cwd(), 'perf', 'results'),
    transports: DEFAULT_MULTI_TRANSPORTS,
    clients: 100
  });

  if (options.helpRequested) {
    usage();
    return;
  }

  const patternNames = resolveMultiPatternNames(options.pattern);
  const defaultMsgSizes = defaultMultiMsgSizes(patternNames, options.msgSizesExplicit);
  if (defaultMsgSizes !== null) {
    options.msgSizes = defaultMsgSizes;
  }
  const defaultClients = defaultMultiClients(patternNames, options.clientsExplicit);
  if (defaultClients !== null) {
    options.clients = defaultClients;
  }
  if (options.transports.some((transport) => transport !== 'tcp')) {
    throw new Error('multi perf currently supports only --transports tcp');
  }

  const resultLines = [];
  const reportSections = [];
  console.log('## Effective Options (start)');
  for (const line of buildEffectiveOptions({ ...options, lang: 'node', suite: 'multi', patterns: patternNames.join(',') }, [
    '- default_clients: 8',
    '- default_stream_clients: 8'
  ])) {
    console.log(line);
  }
  console.log('');

  for (const patternName of patternNames) {
    const patternRows = [];
    for (const msgSize of options.msgSizes) {
      const lines = await runMultiPattern(patternName, options, msgSize);
      patternRows.push({
        pattern: patternName,
        msgSize,
        metrics: primaryMetricsFromResultLines(patternName, msgSize, lines)
      });
      resultLines.push(...lines);
    }

    if (patternRows.length > 0) {
      const tableLines = formatTableRows(patternRows);
      const needsSeparator = reportSections.length > 0;
      if (needsSeparator) {
        reportSections.push('===============================================================================');
        reportSections.push('');
      }
      reportSections.push(`## PATTERN: ${patternName}`);
      reportSections.push(...tableLines);
      reportSections.push('');
      if (needsSeparator) {
        console.log('===============================================================================');
        console.log('');
      }
      console.log(`## PATTERN: ${patternName}`);
      for (const line of tableLines) {
        console.log(line);
      }
      console.log('');
    }
  }

  console.log('## Result Data');
  for (const line of resultLines) {
    console.log(line);
  }
  console.log('');
  console.log('## Status Summary');
  console.log(`- result_lines: ${resultLines.length}`);
  console.log(`- status: ${resultLines.length > 0 ? 'complete' : 'partial'}`);
  console.log(`- expected_result_lines: ${resultLines.length}`);
  console.log(`- actual_result_lines: ${resultLines.length}`);

  const report = writeReport(
    path.join(options.resultsDir, 'multi', 'report'),
    'node',
    'multi',
    resultLines,
    { ...options, patterns: patternNames.join(',') },
    reportSections
  );
  console.log(`report=${report}`);
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
