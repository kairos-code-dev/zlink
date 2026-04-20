// SPDX-License-Identifier: MPL-2.0

'use strict';

const path = require('node:path');
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
const { spawnMultiPair } = require('./perf_multi_orchestrator');

const MULTI_PATTERN_RUNNERS = {
  MULTI_DEALER_DEALER: {
    server: 'perf_multi_dealer_dealer_server.js',
    client: 'perf_multi_dealer_dealer_client.js'
  },
  MULTI_DEALER_ROUTER: {
    server: 'perf_multi_dealer_router_server.js',
    client: 'perf_multi_dealer_router_client.js'
  },
  MULTI_ROUTER_ROUTER: {
    server: 'perf_multi_router_router_server.js',
    client: 'perf_multi_router_router_client.js'
  },
  MULTI_PUBSUB: {
    server: 'perf_multi_pubsub_server.js',
    client: 'perf_multi_pubsub_client.js'
  },
  MULTI_SPOT: {
    server: 'perf_multi_spot_server.js',
    client: 'perf_multi_spot_client.js'
  },
  MULTI_SPOT_REQREP: {
    server: 'perf_multi_spot_reqrep_server.js',
    client: 'perf_multi_spot_reqrep_client.js'
  },
  MULTI_STREAM: {
    server: 'perf_multi_stream_server.js',
    client: 'perf_multi_stream_client.js'
  }
};

const POLICY_TRANSPORTS = {
  MULTI_DEALER_DEALER: ['tcp', 'tls', 'ws', 'wss'],
  MULTI_DEALER_ROUTER: ['tcp', 'tls', 'ws', 'wss'],
  MULTI_ROUTER_ROUTER: ['tcp', 'tls', 'ws', 'wss'],
  MULTI_PUBSUB: ['tcp', 'tls', 'ws', 'wss'],
  MULTI_SPOT: ['tcp', 'tls', 'ws', 'wss'],
  MULTI_SPOT_REQREP: ['tcp', 'tls', 'ws', 'wss'],
  MULTI_STREAM: ['tcp', 'tls', 'ws', 'wss']
};

const RUNNABLE_TRANSPORTS = {
  MULTI_DEALER_DEALER: [],
  MULTI_DEALER_ROUTER: [],
  MULTI_ROUTER_ROUTER: [],
  MULTI_PUBSUB: [],
  MULTI_SPOT: [],
  MULTI_SPOT_REQREP: [],
  MULTI_STREAM: []
};

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
  --warmup N            Accepted for compatibility but ignored.
  --msg-sizes LIST      Comma-separated sizes.
  --transports LIST     Comma-separated transports (default: policy transport set).
  --clients N           Override number of client sockets per pattern (default: 100, stream=10000).
`);
}

function median(values) {
  const sorted = values.slice().sort((a, b) => a - b);
  const mid = Math.floor(sorted.length / 2);
  if ((sorted.length % 2) === 0) {
    return (sorted[mid - 1] + sorted[mid]) / 2;
  }
  return sorted[mid];
}

function medianMetrics(metricsList) {
  return {
    throughput: median(metricsList.map((item) => item.throughput)),
    bandwidth: median(metricsList.map((item) => item.bandwidth)),
    latency: median(metricsList.map((item) => item.latency)),
    latency_p95: median(metricsList.map((item) => item.latency_p95)),
    latency_p99: median(metricsList.map((item) => item.latency_p99))
  };
}

async function sleepMs(milliseconds) {
  await new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function isUnsupported(error) {
  const text = String(error && error.message ? error.message : error).toLowerCase();
  return text.includes('protocol not supported')
    || text.includes('listen eperm')
    || text.includes('operation not permitted');
}

async function main() {
  const options = parseCommonArgs(process.argv.slice(2), {
    pattern: 'ALL',
    duration: 5,
    warmup: 0,
    msgSizes: defaultMultiMsgSizes(['MULTI_DEALER_DEALER'], false),
    resultsDir: path.join(process.cwd(), 'perf', 'results'),
    transports: DEFAULT_MULTI_TRANSPORTS,
    clients: 100
  });

  if (options.helpRequested) {
    usage();
    return;
  }

  options.warmup = 0;
  const patternNames = resolveMultiPatternNames(options.pattern);
  const defaultMsgSizes = defaultMultiMsgSizes(patternNames, options.msgSizesExplicit);
  if (defaultMsgSizes !== null) {
    options.msgSizes = defaultMsgSizes;
  }
  const defaultClients = defaultMultiClients(patternNames, options.clientsExplicit);
  if (defaultClients !== null) {
    options.clients = defaultClients;
  }

  const resultLines = [];
  const reportSections = [];
  const runCooldownMs = Number(process.env.PERF_MULTI_RUN_COOLDOWN_MS ?? 3000);
  const transportCooldownMs = Number(process.env.PERF_MULTI_TRANSPORT_TRANSITION_MS ?? 3000);
  const patternCooldownMs = Number(process.env.PERF_MULTI_PATTERN_TRANSITION_MS ?? 3000);
  let unsupportedCount = 0;
  let failCount = 0;

  console.log('## Effective Options (start)');
  for (const line of buildEffectiveOptions({ ...options, lang: 'node', suite: 'multi', patterns: patternNames.join(',') })) {
    console.log(line);
  }
  console.log('');

  for (let patternIndex = 0; patternIndex < patternNames.length; patternIndex += 1) {
    const patternName = patternNames[patternIndex];
    const runner = MULTI_PATTERN_RUNNERS[patternName];
    if (!runner) {
      throw new Error(`unsupported multi pattern: ${patternName}`);
    }

    if (reportSections.length > 0) {
      reportSections.push('===============================================================================');
      reportSections.push('');
      console.log('===============================================================================');
      console.log('');
    }
    console.log(`## PATTERN: ${patternName}`);
    reportSections.push(`## PATTERN: ${patternName}`);

    for (let transportIndex = 0; transportIndex < options.transports.length; transportIndex += 1) {
      const transport = options.transports[transportIndex];
      if (!POLICY_TRANSPORTS[patternName].includes(transport) || !RUNNABLE_TRANSPORTS[patternName].includes(transport)) {
        const line = `UNSUPPORTED,current,${patternName},${transport}`;
        console.log(line);
        resultLines.push(line);
        unsupportedCount += 1;
        continue;
      }

      const rows = [];
      for (const msgSize of options.msgSizes) {
        const runMetricsList = [];
        let unsupported = false;
        for (let run = 1; run <= options.runs; run += 1) {
          let lines;
          try {
            lines = await spawnMultiPair(runner.server, runner.client, {
              ...options,
              pattern: patternName,
              transport,
              msgSize,
              clients: options.clients
            });
          } catch (error) {
            if (!isUnsupported(error)) {
              throw error;
            }
            const line = `UNSUPPORTED,current,${patternName},${transport}`;
            console.log(line);
            resultLines.push(line);
            unsupportedCount += 1;
            unsupported = true;
            break;
          }
          resultLines.push(...lines);
          runMetricsList.push(primaryMetricsFromResultLines(patternName, msgSize, lines));
          if (run < options.runs) {
            await sleepMs(runCooldownMs);
          }
        }
        if (unsupported) {
          break;
        }
        rows.push({
          pattern: patternName,
          msgSize,
          metrics: options.runs > 1 ? medianMetrics(runMetricsList) : runMetricsList[0]
        });
      }

      const tableLines = formatTableRows(rows);
      reportSections.push(`### Transport: ${transport}`);
      reportSections.push(...tableLines);
      reportSections.push('');
      console.log(`### Transport: ${transport}`);
      for (const line of tableLines) {
        console.log(line);
      }
      console.log('');

      if (transportIndex + 1 < options.transports.length) {
        await sleepMs(transportCooldownMs);
      }
    }

    if (patternIndex + 1 < patternNames.length) {
      await sleepMs(patternCooldownMs);
    }
  }

  console.log('## Result Data');
  for (const line of resultLines) {
    console.log(line);
  }
  console.log('');
  console.log('## Status Summary');
  console.log(`- result_lines: ${resultLines.length}`);
  console.log(`- unsupported: ${unsupportedCount}`);
  console.log(`- fail: ${failCount}`);
  console.log(`- status: ${failCount === 0 ? 'complete' : 'partial'}`);

  const report = writeReport(
    path.join(options.resultsDir, 'multi', 'report'),
    'node',
    'multi',
    resultLines,
    { ...options, patterns: patternNames.join(',') },
    reportSections
  );
  console.log(`report=${report}`);

  if (failCount > 0) {
    process.exitCode = 1;
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
