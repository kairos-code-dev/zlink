// SPDX-License-Identifier: MPL-2.0

'use strict';

const { spawn } = require('node:child_process');
const path = require('node:path');
const {
  buildEffectiveOptions,
  completionLines,
  defaultSingleMsgSizes,
  DEFAULT_SINGLE_TRANSPORTS,
  formatTableHeader,
  formatTableRow,
  hasPrimaryMetricsFromResultLines,
  parseCommonArgs,
  patternDirection,
  primaryMetricsFromResultLines,
  resolveSinglePatternNames,
  writeReport
} = require('../common/perf_metrics');

const PATTERNS = {
  PAIR: { script: 'perf_pair.js' },
  PUBSUB: { script: 'perf_pubsub.js' },
  DEALER_DEALER: { script: 'perf_dealer_dealer.js' },
  DEALER_ROUTER: { script: 'perf_dealer_router.js' },
  ROUTER_ROUTER: { script: 'perf_router_router.js' },
  SPOT: { script: 'perf_spot.js' }
};

function policyTransports(pattern) {
  const raw = pattern === 'SPOT'
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
  --build-dir PATH      Accepted for policy compatibility.
  --results-dir PATH    Override result root directory.
  --results-tag NAME    Optional tag in saved result filename.
  --output PATH         Tee final rendered output to a file.
  --runs N              Iterations per configuration (default: 1).
  --duration N          Override single duration seconds (default: 5).
  --msg-sizes LIST      Comma-separated sizes (default: 64,256,1024,65536,131072,262144).
  --transports LIST     Comma-separated transports (default: policy transport set).
  --pin-cpu             Pin child benchmark processes to CPU 0 on Linux.
  --io-threads N        Override PERF_IO_THREADS for child cases.
  --hwm N               Override PERF_SINGLE_HWM.
  --send-hwm N          Override PERF_SINGLE_SNDHWM.
  --recv-hwm N          Override PERF_SINGLE_RCVHWM.
  --buf SIZE            Override both PERF_SINGLE_SNDBUF and PERF_SINGLE_RCVBUF.
  --sndbuf SIZE         Override PERF_SINGLE_SNDBUF.
  --rcvbuf SIZE         Override PERF_SINGLE_RCVBUF.
  --sndtimeo N          Override PERF_SINGLE_SNDTIMEO_MS.
  --rcvtimeo N          Override PERF_SINGLE_RCVTIMEO_MS.
  --send-timeout-ms N   Alias of --sndtimeo.
  --recv-timeout-ms N   Alias of --rcvtimeo.
  --auto-hwm-profile N  Set auto-HWM profile.

Notes:
  - result is saved under perf/results/single/report/ as
    perf_node_single_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt.`);
}

function pinCpuEnabled(options) {
  return Boolean(options.pinCpu || process.env.PERF_TASKSET === '1');
}

function buildPinnedSpawn(command, args, options) {
  if (!pinCpuEnabled(options)) {
    return { command, args };
  }
  if (process.platform !== 'linux') {
    throw new Error('--pin-cpu is only supported on Linux in this runner');
  }
  return {
    command: 'taskset',
    args: ['-c', '0', command, ...args]
  };
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
    `RESULT,current,${pattern},${transport},${msgSize},throughput,${metrics.throughput.toFixed(3)}`,
    `RESULT,current,${pattern},${transport},${msgSize},bandwidth,${metrics.bandwidth.toFixed(3)}`,
    `RESULT,current,${pattern},${transport},${msgSize},latency,${metrics.latency.toFixed(3)}`,
    `RESULT,current,${pattern},${transport},${msgSize},latency_p95,${metrics.latency_p95.toFixed(3)}`,
    `RESULT,current,${pattern},${transport},${msgSize},latency_p99,${metrics.latency_p99.toFixed(3)}`
  ];
}

function parseAutoHwmDetailLine(line) {
  const stripped = String(line || '').trim();
  if (!stripped.startsWith('AUTO_HWM_DETAIL,')) {
    return null;
  }
  const fields = {};
  for (const item of stripped.split(',').slice(1)) {
    const pos = item.indexOf('=');
    if (pos < 0) {
      continue;
    }
    fields[item.slice(0, pos).trim()] = item.slice(pos + 1).trim();
  }
  return Object.keys(fields).length > 0 ? fields : null;
}

function bytesToKbDisplay(value) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed) || parsed < 0) {
    return '?';
  }
  if (parsed <= 0) {
    return '0';
  }
  if ((parsed % 1024) === 0) {
    return String(parsed / 1024);
  }
  return (parsed / 1024).toFixed(1);
}

function cellWidths(rows, columns) {
  return columns.map(([header, key]) => rows.reduce(
    (width, row) => Math.max(width, String(row[key] ?? '?').length),
    header.length
  ));
}

function autoHwmDetailTableLines(rows, pattern) {
  const patternRows = rows
    .filter((row) => String(row.pattern || '').toUpperCase() === pattern.toUpperCase());
  if (patternRows.length === 0) {
    return [];
  }

  const seen = new Set();
  const displayRows = [];
  for (const row of patternRows) {
    const display = {
      ...row,
      sndbuf_kb: bytesToKbDisplay(row.effective_sndbuf),
      rcvbuf_kb: bytesToKbDisplay(row.effective_rcvbuf),
    };
    const key = [
      'msg_size',
      'component',
      'owner',
      'socket',
      'socket_type',
      'role',
      'sndhwm',
      'rcvhwm',
      'sndbuf_kb',
      'rcvbuf_kb',
      'effective_message_bytes',
      'socket_message_slots',
    ].map((name) => display[name] ?? '').join('\0');
    if (seen.has(key)) {
      continue;
    }
    seen.add(key);
    displayRows.push(display);
  }
  if (displayRows.length === 0) {
    return [];
  }
  displayRows.sort((lhs, rhs) => {
    const sizeDiff = Number(lhs.msg_size || 0) - Number(rhs.msg_size || 0);
    if (sizeDiff !== 0) return sizeDiff;
    return String(lhs.component || '').localeCompare(String(rhs.component || ''))
      || String(lhs.owner || '').localeCompare(String(rhs.owner || ''))
      || String(lhs.socket || '').localeCompare(String(rhs.socket || ''));
  });

  const columns = [
    ['Size(B)', 'msg_size'],
    ['Component', 'component'],
    ['Owner', 'owner'],
    ['Socket', 'socket'],
    ['Type', 'socket_type'],
    ['Role', 'role'],
    ['SNDHWM', 'sndhwm'],
    ['RCVHWM', 'rcvhwm'],
    ['SNDBUF(KB)', 'sndbuf_kb'],
    ['RCVBUF(KB)', 'rcvbuf_kb'],
    ['MsgUnit(B)', 'effective_message_bytes'],
    ['Slots', 'socket_message_slots'],
  ];
  const widths = cellWidths(displayRows, columns);
  const header = '| ' + columns
    .map(([name], index) => name.padEnd(widths[index]))
    .join(' | ') + ' |';
  const separator = '|-' + widths.map((width) => '-'.repeat(width)).join('-|-') + '-|';
  return [
    '',
    '## Auto-HWM Detail',
    `- pattern: ${pattern}`,
    header,
    separator,
    ...displayRows.map((row) => '| ' + columns
      .map(([, key], index) => String(row[key] ?? '?').padEnd(widths[index]))
      .join(' | ') + ' |')
  ];
}

function formatFailureRow(msgSize, label = 'FAIL') {
  const cell = String(label).padStart(16);
  return `| ${String(msgSize).padEnd(8)}B | ${cell} | ${'FAIL'.padStart(10)} | ${'FAIL'.padStart(13)} | ${'FAIL'.padStart(13)} | ${'FAIL'.padStart(13)} |`;
}

function isPlatformSkip(pattern, transport) {
  return process.platform === 'win32' && transport === 'ipc' && pattern !== 'SPOT';
}

function isUnsupportedLines(lines) {
  return lines.some((line) => line.startsWith('UNSUPPORTED,'));
}

function errorText(error) {
  return String(error && error.message ? error.message : error);
}

function buildBinaryEnv(options) {
  const env = {
    ...process.env,
    PERF_SINGLE_DURATION_SECONDS: String(options.duration)
  };
  if (Number.isFinite(options.hwm)) {
    env.PERF_SINGLE_HWM = String(options.hwm);
  }
  if (Number.isFinite(options.sendHwm)) {
    env.PERF_SINGLE_SNDHWM = String(options.sendHwm);
  }
  if (Number.isFinite(options.recvHwm)) {
    env.PERF_SINGLE_RCVHWM = String(options.recvHwm);
  }
  if (options.sndbuf) {
    env.PERF_SINGLE_SNDBUF = String(options.sndbuf);
  }
  if (options.rcvbuf) {
    env.PERF_SINGLE_RCVBUF = String(options.rcvbuf);
  }
  if (options.autoHwmProfile) {
    env.PERF_CTX_AUTO_HWM_PROFILE = String(options.autoHwmProfile);
  }
  if (Number.isFinite(options.sendTimeoutMs)) {
    env.PERF_SINGLE_SNDTIMEO_MS = String(options.sendTimeoutMs);
  }
  if (Number.isFinite(options.recvTimeoutMs)) {
    env.PERF_SINGLE_RCVTIMEO_MS = String(options.recvTimeoutMs);
  }
  if (Number.isFinite(options.ioThreads)) {
    env.PERF_IO_THREADS = String(options.ioThreads);
  }
  return env;
}

function resolveSingleTimeoutSeconds(durationSeconds) {
  const override = Number(process.env.PERF_SINGLE_TIMEOUT_SECONDS || 0);
  if (Number.isFinite(override) && override > 0) {
    return Math.trunc(override);
  }
  return Math.max(30, Math.ceil(Number(durationSeconds) * 6) + 15);
}

async function runSingleCase(scriptName, transport, msgSize, options) {
  const scriptPath = path.join(__dirname, scriptName);
  const spawnSpec = buildPinnedSpawn(
    process.execPath,
    [scriptPath, 'current', transport, String(msgSize)],
    options
  );
  const child = spawn(
    spawnSpec.command,
    spawnSpec.args,
    {
      cwd: process.cwd(),
      env: buildBinaryEnv(options),
      stdio: ['ignore', 'pipe', 'pipe']
    }
  );
  const stdoutLines = [];
  const stderrLines = [];
  let stdoutBuffer = '';
  let stderrBuffer = '';
  let timedOut = false;
  const timeoutMs = resolveSingleTimeoutSeconds(options.duration) * 1000;
  const timeout = setTimeout(() => {
    timedOut = true;
    child.kill('SIGTERM');
    setTimeout(() => child.kill('SIGKILL'), 1000).unref();
  }, timeoutMs);

  child.stdout.setEncoding('utf8');
  child.stdout.on('data', (chunk) => {
    stdoutBuffer += chunk;
    while (true) {
      const newline = stdoutBuffer.indexOf('\n');
      if (newline === -1) {
        break;
      }
      const line = stdoutBuffer.slice(0, newline).trim();
      stdoutBuffer = stdoutBuffer.slice(newline + 1);
      if (line) {
        stdoutLines.push(line);
      }
    }
  });

  child.stderr.setEncoding('utf8');
  child.stderr.on('data', (chunk) => {
    stderrBuffer += chunk;
    while (true) {
      const newline = stderrBuffer.indexOf('\n');
      if (newline === -1) {
        break;
      }
      const line = stderrBuffer.slice(0, newline).trim();
      stderrBuffer = stderrBuffer.slice(newline + 1);
      if (line) {
        stderrLines.push(line);
      }
    }
  });

  const exitCode = await new Promise((resolve, reject) => {
    child.once('error', reject);
    child.once('exit', (code) => resolve(code ?? 1));
  });
  clearTimeout(timeout);

  if (stdoutBuffer.trim()) {
    stdoutLines.push(stdoutBuffer.trim());
  }
  if (stderrBuffer.trim()) {
    stderrLines.push(stderrBuffer.trim());
  }

  if (timedOut) {
    const stderrText = stderrLines.join('\n');
    throw new Error(
      stderrText
        ? `single case timeout after ${timeoutMs}ms\n${stderrText}`
        : `single case timeout after ${timeoutMs}ms`
    );
  }

  if (exitCode !== 0) {
    const stderrText = stderrLines.join('\n');
    throw new Error(stderrText ? `single case failed: ${stderrText}` : `single case failed: ${exitCode}`);
  }

  return stdoutLines;
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
  const autoHwmDetails = [];
  let unsupportedCombos = 0;
  let skippedCombos = 0;

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
        let transportUnsupported = false;
        for (const msgSize of options.msgSizes) {
          try {
            const lines = await runSingleCase(runner.script, transport, msgSize, options);
            for (const line of lines) {
              const detail = parseAutoHwmDetailLine(line);
              if (detail) {
                autoHwmDetails.push(detail);
              }
            }
            if (!hasPrimaryMetricsFromResultLines(name, msgSize, lines) && isUnsupportedLines(lines)) {
              unsupportedCombos += options.msgSizes.length;
              transportUnsupported = true;
              break;
            }
            const metrics = primaryMetricsFromResultLines(name, msgSize, lines);
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
        if (transportUnsupported) {
          emit(`    Testing ${transport}: unsupported Done`);
          continue;
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
        let transportUnsupported = false;
        for (let run = 1; run <= options.runs; run += 1) {
          emit(`      run ${run}/${options.runs}:`);
          emitIndented('        ', formatTableHeader());
          for (const msgSize of options.msgSizes) {
            try {
              const lines = await runSingleCase(runner.script, transport, msgSize, options);
              for (const line of lines) {
                const detail = parseAutoHwmDetailLine(line);
                if (detail) {
                  autoHwmDetails.push(detail);
                }
              }
              if (!hasPrimaryMetricsFromResultLines(name, msgSize, lines) && isUnsupportedLines(lines)) {
                unsupportedCombos += options.msgSizes.length;
                transportUnsupported = true;
                break;
              }
              const metrics = primaryMetricsFromResultLines(name, msgSize, lines);
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
          if (transportUnsupported) {
            emit(`    Testing ${transport}: unsupported Done`);
            break;
          }
        }

        if (transportUnsupported) {
          continue;
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

  for (const name of names) {
    for (const line of autoHwmDetailTableLines(autoHwmDetails, name)) {
      emit(line);
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
