// SPDX-License-Identifier: MPL-2.0

'use strict';

const fs = require('node:fs');
const path = require('node:path');
const {
  buildEffectiveOptions,
  completionLines,
  defaultMultiMsgSizes,
  DEFAULT_MULTI_TRANSPORTS,
  formatTableHeader,
  formatTableRow,
  hasPrimaryMetricsFromResultLines,
  parseCommonArgs,
  patternDirection,
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

function patternMsgSizes(patternName, requestedSizes) {
  const allowed = patternName === 'MULTI_STREAM'
    ? [64, 256, 1024, 65536]
    : [64, 256, 1024, 65536, 131072, 262144];
  return requestedSizes.filter((size) => allowed.includes(size));
}

function usage() {
  console.log(`Usage: bindings/node/perf/run_benchmarks_multi.sh [options]

Measure current zlink Node multi-pattern performance.

Options:
  -h, --help            Show this help.
  --pattern NAME        Pattern list (comma-separated) or ALL.
  --build-dir PATH      Accepted for policy compatibility.
  --results-dir PATH    Override result root directory.
  --results-tag NAME    Optional tag in saved result filename.
  --output PATH         Tee final rendered output to a file.
  --runs N              Iterations per configuration (default: 1).
  --duration N          Override multi duration seconds (default: 5).
  --msg-sizes LIST      Comma-separated sizes.
  --transports LIST     Comma-separated transports (default: policy transport set).
  --clients N           Override number of client sockets per pattern (default: 100, stream=10000).
  --hwm N               Override PERF_MULTI_HWM (default baseline: 100, stream=10).
  --send-hwm N          Override PERF_MULTI_SNDHWM.
  --recv-hwm N          Override PERF_MULTI_RCVHWM.
  --sndtimeo N          Override PERF_MULTI_SNDTIMEO_MS.
  --rcvtimeo N          Override PERF_MULTI_RCVTIMEO_MS.
  --connect-concurrency N
                       Override PERF_MULTI_CONNECT_CONCURRENCY.
  --transport-transition-ms N
                       Override transport cooldown.
  --pattern-transition-ms N
                       Override pattern cooldown.
  --server-ready-timeout-ms N
                       Override READY wait timeout.
  --connect-ready-timeout-ms N
                       Override connection-ready wait timeout.
  --monitor-hwm N       Override PERF_MULTI_MONITOR_HWM.
  --server-shutdown-timeout-ms N
                       Override graceful server shutdown wait.
  --server-bind-port N Override benchmark bind port (default: 0 = auto).
  --pin-cpu            Pin server/client benchmark processes to CPU 0 on Linux.`);
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

function errorText(error) {
  return String(error && error.message ? error.message : error);
}

function isUnsupported(error) {
  return errorText(error).toLowerCase().includes('protocol not supported');
}

function hasUnsupportedToken(lines) {
  return lines.some((line) => line.startsWith('UNSUPPORTED,'));
}

function hasSkipToken(lines) {
  return lines.some((line) => line.startsWith('SKIP,'));
}

function explicitClientCount() {
  const configured = Number(process.env.PERF_MULTI_CLIENTS || NaN);
  if (Number.isFinite(configured) && configured > 0) {
    return Math.trunc(configured);
  }
  return null;
}

function defaultClientsForPattern(patternName) {
  return patternName === 'MULTI_STREAM' ? 10000 : 100;
}

function currentSoftNofileLimit() {
  if (process.platform !== 'linux') {
    return null;
  }
  try {
    const limits = fs.readFileSync('/proc/self/limits', 'utf8');
    const line = limits.split('\n').find((entry) => entry.startsWith('Max open files'));
    if (!line) {
      return null;
    }
    const fields = line.trim().split(/\s+/);
    if (fields.length < 5 || fields[3] === 'unlimited') {
      return null;
    }
    const soft = Number(fields[3]);
    return Number.isFinite(soft) && soft > 0 ? soft : null;
  } catch {
    return null;
  }
}

function memoryAvailableKb() {
  if (process.platform !== 'linux') {
    return null;
  }
  try {
    const meminfo = fs.readFileSync('/proc/meminfo', 'utf8');
    const line = meminfo.split('\n').find((entry) => entry.startsWith('MemAvailable:'));
    if (!line) {
      return null;
    }
    const match = line.match(/^MemAvailable:\s+(\d+)\s+kB$/);
    if (!match) {
      return null;
    }
    return Number(match[1]);
  } catch {
    return null;
  }
}

function resolveMemoryMaxClients() {
  const availableKb = memoryAvailableKb();
  if (!Number.isFinite(availableKb) || availableKb <= 0) {
    return null;
  }
  const budgetPct = Number(process.env.PERF_MULTI_MEMORY_BUDGET_PCT || 70);
  const baseMb = Number(process.env.PERF_MULTI_MEMORY_BASE_MB || 512);
  const perClientKb = Number(process.env.PERF_MULTI_MEMORY_PER_CLIENT_KB || 1024);
  if (!Number.isFinite(budgetPct) || budgetPct < 1 || budgetPct > 95) {
    return null;
  }
  if (!Number.isFinite(baseMb) || baseMb < 0) {
    return null;
  }
  if (!Number.isFinite(perClientKb) || perClientKb < 1) {
    return null;
  }
  const usableKb = Math.trunc(availableKb * budgetPct / 100);
  const baseKb = Math.trunc(baseMb * 1024);
  if (usableKb <= baseKb) {
    return 1;
  }
  return Math.max(1, Math.trunc((usableKb - baseKb) / perClientKb));
}

function resolvePatternClients(patternName, options, clientSource) {
  const requested = clientSource === 'policy'
    ? defaultClientsForPattern(patternName)
    : options.clients;
  let effectiveClients = requested;

  if (!Number.isFinite(requested) || requested <= 0) {
    throw new Error(`invalid multi clients for ${patternName}: ${requested}`);
  }

  if (process.env.PERF_SKIP_MEMORY_CHECK !== '1') {
    const maxClients = resolveMemoryMaxClients();
    if (Number.isFinite(maxClients) && maxClients > 0) {
      if (clientSource === 'policy') {
        effectiveClients = Math.min(requested, maxClients);
      } else if (requested > maxClients) {
        return {
          clients: requested,
          skipReason: `memory_guard_clients=${requested},max_clients=${maxClients},mem_available_kb=${memoryAvailableKb()},budget_pct=${process.env.PERF_MULTI_MEMORY_BUDGET_PCT || 70},base_mb=${process.env.PERF_MULTI_MEMORY_BASE_MB || 512},per_client_kb=${process.env.PERF_MULTI_MEMORY_PER_CLIENT_KB || 1024}`
        };
      }
    }
  }

  if (process.env.PERF_SKIP_NOFILE_CHECK !== '1') {
    const required = effectiveClients * 3 + 4096;
    const softLimit = currentSoftNofileLimit();
    if (Number.isFinite(softLimit) && softLimit < required) {
      return {
        clients: effectiveClients,
        skipReason: `nofile_guard_clients=${effectiveClients},required=${required},soft=${softLimit}`
      };
    }
  }

  return {
    clients: effectiveClients,
    skipReason: ''
  };
}

async function sleepMs(milliseconds) {
  await new Promise((resolve) => setTimeout(resolve, milliseconds));
}

async function main() {
  const options = parseCommonArgs(process.argv.slice(2), {
    pattern: 'ALL',
    duration: 5,
    msgSizes: defaultMultiMsgSizes(['MULTI_DEALER_DEALER'], false),
    resultsDir: path.join(process.cwd(), 'perf', 'results'),
    transports: DEFAULT_MULTI_TRANSPORTS,
    clients: defaultClientsForPattern('MULTI_DEALER_DEALER')
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
  const envClients = explicitClientCount();
  const clientSource = options.clientsExplicit
    ? 'cli'
    : (envClients !== null ? 'env' : 'policy');
  if (clientSource === 'env') {
    options.clients = envClients;
  }

  const failFast = process.env.PERF_FAIL_FAST === '1';
  const runCooldownMs = Number(process.env.PERF_MULTI_RUN_COOLDOWN_MS ?? 3000);
  const transportCooldownMs = Number.isFinite(options.transportTransitionMs)
    ? options.transportTransitionMs
    : Number(process.env.PERF_MULTI_TRANSPORT_TRANSITION_MS ?? 3000);
  const patternCooldownMs = Number.isFinite(options.patternTransitionMs)
    ? options.patternTransitionMs
    : Number(process.env.PERF_MULTI_PATTERN_TRANSITION_MS ?? 3000);
  const resultLines = [];
  const reportLines = [];
  const failures = [];
  const skips = [];
  let requestedCombos = 0;
  let unsupportedCombos = 0;
  let skippedCombos = 0;
  const runnablePatterns = [];

  const emit = (line = '') => {
    console.log(line);
    reportLines.push(line);
  };
  const emitIndented = (prefix, lines) => {
    for (const line of lines) {
      emit(`${prefix}${line}`);
    }
  };

  for (const patternName of patternNames) {
    const runner = MULTI_PATTERN_RUNNERS[patternName];
    if (!runner) {
      throw new Error(`unsupported multi pattern: ${patternName}`);
    }
    const resolution = resolvePatternClients(patternName, options, clientSource);
    if (resolution.skipReason) {
      skips.push(`${patternName}: ${resolution.skipReason}`);
      continue;
    }
    runnablePatterns.push({
      patternName,
      runner,
      clients: resolution.clients
    });
  }

  if (runnablePatterns.length === 0) {
    if (skips.length > 0) {
      console.log('## Skips');
      for (const skip of skips) {
        console.log(`- ${skip}`);
      }
      return;
    }
    throw new Error('no multi patterns selected to run');
  }

  const headerOptions = {
    ...options,
    clients: clientSource === 'policy' ? undefined : options.clients,
    clientsLabel: clientSource === 'policy'
      ? 'pattern default (non-stream=100, stream=10000)'
      : String(options.clients)
  };

  console.log('## Effective Options (start)');
  for (const line of buildEffectiveOptions({
    ...headerOptions,
    lang: 'node',
    suite: 'multi',
    patterns: patternNames.join(',')
  })) {
    console.log(line);
  }
  console.log('');

  for (let patternIndex = 0; patternIndex < runnablePatterns.length; patternIndex += 1) {
    const { patternName, runner, clients } = runnablePatterns[patternIndex];

    if (patternIndex > 0) {
      emit('===============================================================================');
      emit('');
    }
    emit(`## PATTERN: ${patternName} (${patternDirection(patternName)})`);
    emit('');
    emit(`  > Benchmarking current for ${patternName}...`);
    if (clientSource === 'policy' && clients !== defaultClientsForPattern(patternName)) {
      emit(`  > Memory guard capped clients to ${clients}.`);
    }
    const msgSizes = patternMsgSizes(patternName, options.msgSizes);

    for (let transportIndex = 0; transportIndex < options.transports.length; transportIndex += 1) {
      const transport = options.transports[transportIndex];
      if (!POLICY_TRANSPORTS[patternName].includes(transport)) {
        requestedCombos += msgSizes.length;
        unsupportedCombos += msgSizes.length;
        emit(`    Testing ${transport}: unsupported Done`);
        continue;
      }

      const transportFailuresBefore = failures.length;
      requestedCombos += msgSizes.length;
      const sizesLabel = msgSizes.map((size) => `${size}B`).join(',');
      emit(`    Testing ${transport} | ${sizesLabel}:`);

      if (options.runs === 1) {
        let transportUnsupported = false;
        emitIndented('      ', formatTableHeader());
        for (const msgSize of msgSizes) {
          try {
            const lines = await spawnMultiPair(runner.server, runner.client, {
              ...options,
              pattern: patternName,
              transport,
              msgSize,
              clients
            });
            const hasMetrics = hasPrimaryMetricsFromResultLines(patternName, msgSize, lines);
            if (!hasMetrics && hasUnsupportedToken(lines)) {
              unsupportedCombos += msgSizes.length;
              transportUnsupported = true;
              break;
            }
            if (!hasMetrics && hasSkipToken(lines)) {
              skippedCombos += 1;
              skips.push(`${patternName} current ${transport} ${msgSize}B: skipped`);
              emit(`      ${formatFailureRow(msgSize, 'SKIP')}`);
              continue;
            }
            const metrics = primaryMetricsFromResultLines(patternName, msgSize, lines);
            resultLines.push(...metricLines(patternName, transport, msgSize, metrics));
            emit(`      ${formatTableRow({ pattern: patternName, msgSize, metrics })}`);
          } catch (error) {
            if (isUnsupported(error)) {
              unsupportedCombos += msgSizes.length;
              transportUnsupported = true;
              break;
            }
            failures.push(`${patternName} current ${transport} ${msgSize}B: ${errorText(error)}`);
            emit(`      ${formatFailureRow(msgSize)}`);
            if (failFast) {
              throw error;
            }
          }
        }
        if (transportUnsupported) {
          emit(`    Testing ${transport}: unsupported Done`);
          continue;
        }
      } else {
        const runResults = new Map(msgSizes.map((msgSize) => [msgSize, []]));
        let transportUnsupported = false;
        for (let run = 1; run <= options.runs; run += 1) {
          emit(`      run ${run}/${options.runs}:`);
          emitIndented('        ', formatTableHeader());
          for (const msgSize of msgSizes) {
            try {
              const lines = await spawnMultiPair(runner.server, runner.client, {
                ...options,
                pattern: patternName,
                transport,
                msgSize,
                clients
              });
              const hasMetrics = hasPrimaryMetricsFromResultLines(patternName, msgSize, lines);
              if (!hasMetrics && hasUnsupportedToken(lines)) {
                unsupportedCombos += msgSizes.length;
                transportUnsupported = true;
                break;
              }
              if (!hasMetrics && hasSkipToken(lines)) {
                skippedCombos += 1;
                skips.push(`${patternName} current ${transport} ${msgSize}B: skipped`);
                emit(`        ${formatFailureRow(msgSize, 'SKIP')}`);
                continue;
              }
              const metrics = primaryMetricsFromResultLines(patternName, msgSize, lines);
              runResults.get(msgSize).push(metrics);
              emit(`        ${formatTableRow({ pattern: patternName, msgSize, metrics })}`);
            } catch (error) {
              if (isUnsupported(error)) {
                unsupportedCombos += msgSizes.length;
                transportUnsupported = true;
                break;
              }
              failures.push(`${patternName} current ${transport} ${msgSize}B: ${errorText(error)}`);
              emit(`        ${formatFailureRow(msgSize)}`);
              if (failFast) {
                throw error;
              }
            }
          }
          if (transportUnsupported) {
            emit(`    Testing ${transport}: unsupported Done`);
            break;
          }
          if (run < options.runs) {
            emit(`      [cooldown ${runCooldownMs}ms]`);
            if (runCooldownMs > 0) {
              await sleepMs(runCooldownMs);
            }
          }
        }

        if (transportUnsupported) {
          continue;
        }

        emit('      median:');
        emitIndented('        ', formatTableHeader());
        for (const msgSize of msgSizes) {
          const metricsList = runResults.get(msgSize);
          if (!metricsList || metricsList.length !== options.runs) {
            emit(`        ${formatFailureRow(msgSize)}`);
            continue;
          }
          const metrics = medianMetrics(metricsList);
          resultLines.push(...metricLines(patternName, transport, msgSize, metrics));
          emit(`        ${formatTableRow({ pattern: patternName, msgSize, metrics })}`);
        }
      }

      const transportFailures = failures.length - transportFailuresBefore;
      emit(`    Testing ${transport}: ${transportFailures > 0 ? `(failures=${transportFailures}) Done` : 'Done'}`);
      if (transportIndex + 1 < options.transports.length) {
        emit(`    [transport cooldown ${transportCooldownMs}ms]`);
        if (transportCooldownMs > 0) {
          await sleepMs(transportCooldownMs);
        }
      }
    }

    if (patternIndex + 1 < runnablePatterns.length && patternCooldownMs > 0) {
      await sleepMs(patternCooldownMs);
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

  const expectedCombos = requestedCombos - unsupportedCombos - skippedCombos;
  const expectedResultLines = expectedCombos * 5;
  const actualResultLines = resultLines.length;
  const status = expectedResultLines === actualResultLines ? 'complete' : 'partial';

  console.log('');
  console.log('## Effective Options (result)');
  for (const line of buildEffectiveOptions({
    ...headerOptions,
    lang: 'node',
    suite: 'multi',
    patterns: patternNames.join(',')
  })) {
    console.log(line);
  }
  console.log('');
  for (const line of completionLines(status, expectedResultLines, actualResultLines)) {
    console.log(line);
  }

  writeReport(
    path.join(options.resultsDir, 'multi', 'report'),
    'node',
    'multi',
    { ...headerOptions, patterns: patternNames.join(',') },
    reportLines,
    completionLines(status, expectedResultLines, actualResultLines)
  );

  if (status !== 'complete') {
    process.exitCode = 1;
  }
}

function exitAfterFlush(code) {
  process.stdout.write('', () => {
    process.stderr.write('', () => {
      process.exit(code);
    });
  });
}

main()
  .then(() => {
    exitAfterFlush(process.exitCode || 0);
  })
  .catch((error) => {
    console.error(error);
    exitAfterFlush(1);
  });
