// SPDX-License-Identifier: MPL-2.0

'use strict';

const path = require('node:path');
const {
  buildEffectiveOptions,
  completionLines,
  computeMetrics,
  defaultSingleMsgSizes,
  DEFAULT_SINGLE_TRANSPORTS,
  formatTableHeader,
  formatTableRow,
  parseCommonArgs,
  patternDirection,
  resolveSinglePatternNames,
  writeReport
} = require('../common/perf_metrics');
const { runPairBenchmark } = require('./perf_pair');
const { runPubSubBenchmark } = require('./perf_pubsub');
const { runDealerDealerBenchmark } = require('./perf_dealer_dealer');
const { runDealerRouterBenchmark } = require('./perf_dealer_router');
const { runRouterRouterBenchmark } = require('./perf_router_router');
const { runSpotBenchmark } = require('./perf_spot');
const { runSpotReqRepBenchmark } = require('./perf_spot_reqrep');

const PATTERNS = {
  PAIR: runPairBenchmark,
  PUBSUB: runPubSubBenchmark,
  DEALER_DEALER: runDealerDealerBenchmark,
  DEALER_ROUTER: runDealerRouterBenchmark,
  ROUTER_ROUTER: runRouterRouterBenchmark,
  SPOT: runSpotBenchmark,
  SPOT_REQREP: runSpotReqRepBenchmark
};

function policyTransports(pattern) {
  const raw = pattern === 'SPOT' || pattern === 'SPOT_REQREP'
    ? ['tcp', 'tls', 'ws', 'wss']
    : ['tcp', 'tls', 'ws', 'wss', 'inproc', 'ipc'];
  if (process.platform === 'win32') {
    return raw.filter((transport) => transport !== 'ipc');
  }
  return raw;
}

function usage() {
  console.log(`Usage: bindings/node/perf/run_benchmarks.sh [options]

Measure current zlink Node single-pattern performance.

Options:
  -h, --help            Show this help.
  --pattern NAME        Pattern list (comma-separated) or ALL.
  --results-dir PATH    Override result root directory.
  --results-tag NAME    Optional tag in saved result filename.
  --runs N              Iterations per configuration (default: 1).
  --duration N          Override single duration seconds (default: 5).
  --msg-sizes LIST      Comma-separated sizes (default: 64,256,1024,65536,131072,262144).
  --transports LIST     Comma-separated transports (default: policy transport set).

Notes:
  - result is saved under perf/results/single/report/ as
    perf_node_single_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt.`);
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

function metricLines(pattern, transport, msgSize, metrics) {
  return [
    `RESULT,current,${pattern},${transport},${msgSize},throughput,${metrics.throughput.toFixed(2)}`,
    `RESULT,current,${pattern},${transport},${msgSize},bandwidth,${metrics.bandwidth.toFixed(2)}`,
    `RESULT,current,${pattern},${transport},${msgSize},latency,${metrics.latency.toFixed(6)}`,
    `RESULT,current,${pattern},${transport},${msgSize},latency_p95,${metrics.latency_p95.toFixed(6)}`,
    `RESULT,current,${pattern},${transport},${msgSize},latency_p99,${metrics.latency_p99.toFixed(6)}`
  ];
}

function formatFailureRow(msgSize, label = 'FAIL') {
  const cell = String(label).padStart(16);
  return `| ${String(msgSize).padEnd(8)}B | ${cell} | ${'FAIL'.padStart(10)} | ${'FAIL'.padStart(13)} | ${'FAIL'.padStart(13)} | ${'FAIL'.padStart(13)} |`;
}

function isPlatformSkip(pattern, transport) {
  return process.platform === 'win32' && transport === 'ipc' && pattern !== 'SPOT' && pattern !== 'SPOT_REQREP';
}

function errorText(error) {
  return String(error && error.message ? error.message : error);
}

async function main() {
  const options = parseCommonArgs(process.argv.slice(2), {
    pattern: 'ALL',
    duration: 5,
    msgSizes: defaultSingleMsgSizes(),
    resultsDir: path.join(process.cwd(), 'perf', 'results'),
    transports: DEFAULT_SINGLE_TRANSPORTS
  });

  if (options.helpRequested) {
    usage();
    return;
  }

  const names = resolveSinglePatternNames(options.pattern);
  const failFast = process.env.PERF_FAIL_FAST === '1';
  const resultLines = [];
  const reportLines = [];
  const failures = [];
  const skips = [];
  let unsupportedCombos = 0;
  let skippedCombos = 0;
  let caseOrdinal = 1;

  const emit = (line = '') => {
    console.log(line);
    reportLines.push(line);
  };
  const emitIndented = (prefix, lines) => {
    for (const line of lines) {
      emit(`${prefix}${line}`);
    }
  };

  console.log('## Effective Options (start)');
  for (const line of buildEffectiveOptions({ ...options, lang: 'node', suite: 'single', patterns: names.join(',') })) {
    console.log(line);
  }
  console.log('');

  for (let patternIndex = 0; patternIndex < names.length; patternIndex += 1) {
    const name = names[patternIndex];
    const runner = PATTERNS[name];
    if (!runner) {
      throw new Error(`unsupported single pattern: ${name}`);
    }
    if (patternIndex > 0) {
      emit('===============================================================================');
      emit('');
    }
    emit(`## PATTERN: ${name} (${patternDirection(name)})`);
    emit('');
    emit(`  > Benchmarking current for ${name}...`);

    for (const transport of options.transports) {
      if (isPlatformSkip(name, transport)) {
        skippedCombos += options.msgSizes.length;
        skips.push(`${name} current ${transport}: platform constraint`);
        emit(`    Testing ${transport}: skip Done`);
        continue;
      }
      const allowed = policyTransports(name);
      if (!allowed.includes(transport)) {
        unsupportedCombos += options.msgSizes.length;
        emit(`    Testing ${transport}: unsupported Done`);
        continue;
      }

      const transportFailuresBefore = failures.length;
      emit(`    Testing ${transport}:`);

      if (options.runs === 1) {
        emitIndented('      ', formatTableHeader());
        const perSizeMetrics = new Map();
        for (const msgSize of options.msgSizes) {
          try {
            const latenciesNs = await runner(msgSize, {
              ...options,
              transport,
              runId: caseOrdinal
            });
            caseOrdinal += 1;
            const metrics = computeMetrics(
              latenciesNs,
              options.duration,
              msgSize,
              name === 'SPOT_REQREP' ? 2 : 1
            );
            const row = { pattern: name, msgSize, metrics };
            emit(`      ${formatTableRow(row)}`);
            perSizeMetrics.set(msgSize, metrics);
          } catch (error) {
            failures.push(`${name} current ${transport} ${msgSize}B: ${errorText(error)}`);
            emit(`      ${formatFailureRow(msgSize)}`);
            if (failFast) {
              throw error;
            }
          }
        }
        for (const msgSize of options.msgSizes) {
          const metrics = perSizeMetrics.get(msgSize);
          if (!metrics) {
            continue;
          }
          resultLines.push(...metricLines(name, transport, msgSize, metrics));
        }
      } else {
        const runResults = new Map(options.msgSizes.map((msgSize) => [msgSize, []]));
        for (let run = 1; run <= options.runs; run += 1) {
          emit(`      run ${run}/${options.runs}:`);
          emitIndented('        ', formatTableHeader());
          for (const msgSize of options.msgSizes) {
            try {
              const latenciesNs = await runner(msgSize, {
                ...options,
                transport,
                runId: caseOrdinal
              });
              caseOrdinal += 1;
              const metrics = computeMetrics(
                latenciesNs,
                options.duration,
                msgSize,
                name === 'SPOT_REQREP' ? 2 : 1
              );
              runResults.get(msgSize).push(metrics);
              emit(`        ${formatTableRow({ pattern: name, msgSize, metrics })}`);
            } catch (error) {
              failures.push(`${name} current ${transport} ${msgSize}B: ${errorText(error)}`);
              emit(`        ${formatFailureRow(msgSize)}`);
              if (failFast) {
                throw error;
              }
            }
          }
        }

        emit('      median:');
        emitIndented('        ', formatTableHeader());
        for (const msgSize of options.msgSizes) {
          const metricsList = runResults.get(msgSize);
          if (!metricsList || metricsList.length !== options.runs) {
            emit(`        ${formatFailureRow(msgSize)}`);
            continue;
          }
          const metrics = medianMetrics(metricsList);
          emit(`        ${formatTableRow({ pattern: name, msgSize, metrics })}`);
          resultLines.push(...metricLines(name, transport, msgSize, metrics));
        }
      }

      const transportFailures = failures.length - transportFailuresBefore;
      emit(`    Testing ${transport}: ${transportFailures > 0 ? `(failures=${transportFailures}) Done` : 'Done'}`);
    }
  }

  if (failures.length > 0) {
    emit('');
    emit('## Failures');
    for (const failure of failures) {
      emit(`- ${failure}`);
    }
  }

  if (skips.length > 0) {
    emit('');
    emit('## Skips');
    for (const skip of skips) {
      emit(`- ${skip}`);
    }
  }

  const expectedCombos = (names.length * options.transports.length * options.msgSizes.length)
    - unsupportedCombos
    - skippedCombos;
  const expectedResultLines = expectedCombos * 5;
  const actualResultLines = resultLines.length;
  const status = expectedResultLines === actualResultLines ? 'complete' : 'partial';

  console.log('');
  console.log('## Effective Options (result)');
  for (const line of buildEffectiveOptions({ ...options, lang: 'node', suite: 'single', patterns: names.join(',') })) {
    console.log(line);
  }
  console.log('');
  for (const line of completionLines(status, expectedResultLines, actualResultLines)) {
    console.log(line);
  }

  writeReport(
    path.join(options.resultsDir, 'single', 'report'),
    'node',
    'single',
    { ...options, patterns: names.join(',') },
    reportLines,
    completionLines(status, expectedResultLines, actualResultLines)
  );

  if (status !== 'complete') {
    process.exitCode = 1;
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
