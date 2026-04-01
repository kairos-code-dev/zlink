// SPDX-License-Identifier: MPL-2.0

'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { Worker } = require('node:worker_threads');

const METRIC_MAGIC = 0x5a4c5046;
const HEADER_SIZE = 24;
const STANDARD_MSG_SIZES = [64, 256, 1024, 65536, 131072, 262144];
const STREAM_MSG_SIZES = [64, 256, 1024, 65536];
const DEFAULT_SINGLE_TRANSPORTS = ['inproc'];
const DEFAULT_MULTI_TRANSPORTS = ['tcp'];

function parseSizeList(value, fallback) {
  if (!value) {
    return fallback;
  }
  return value.split(',').map((part) => {
    const size = Number(part.trim());
    if (!Number.isFinite(size) || size <= 0) {
      throw new Error(`invalid msg size: ${part}`);
    }
    return size;
  });
}

function parseStringList(value, fallback) {
  if (!value) {
    return fallback;
  }
  return value.split(',').map((part) => part.trim()).filter(Boolean);
}

function parseCommonArgs(argv, defaults) {
  const options = {
    pattern: defaults.pattern,
    recv: defaults.recv,
    duration: defaults.duration,
    warmup: defaults.warmup,
    msgSizes: defaults.msgSizes,
    resultsDir: defaults.resultsDir,
    resultsTag: '',
    output: '',
    runs: 1,
    transports: defaults.transports || [],
    clients: defaults.clients,
    msgSizesExplicit: false,
    transportsExplicit: false,
    clientsExplicit: false,
    helpRequested: false
  };

  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--pattern') {
      options.pattern = argv[i + 1];
      i += 1;
    } else if (arg === '--recv') {
      options.recv = argv[i + 1];
      i += 1;
    } else if (arg === '--duration') {
      options.duration = Number(argv[i + 1]);
      i += 1;
    } else if (arg === '--warmup') {
      options.warmup = Number(argv[i + 1]);
      i += 1;
    } else if (arg === '--msg-sizes') {
      options.msgSizes = parseSizeList(argv[i + 1], defaults.msgSizes);
      options.msgSizesExplicit = true;
      i += 1;
    } else if (arg === '--results-dir') {
      options.resultsDir = argv[i + 1];
      i += 1;
    } else if (arg === '--results-tag') {
      options.resultsTag = argv[i + 1];
      i += 1;
    } else if (arg === '--output') {
      options.output = argv[i + 1];
      i += 1;
    } else if (arg === '--runs') {
      options.runs = Number(argv[i + 1]);
      i += 1;
    } else if (arg === '--transports') {
      options.transports = parseStringList(argv[i + 1], defaults.transports || []);
      options.transportsExplicit = true;
      i += 1;
    } else if (arg === '--clients') {
      if (typeof defaults.clients === 'undefined') {
        throw new Error('--clients is not supported for this runner');
      }
      options.clients = Number(argv[i + 1]);
      options.clientsExplicit = true;
      i += 1;
    } else if (
      arg === '--build-dir'
      || arg === '--reuse-build'
      || arg === '--clean-build'
    ) {
      if (arg.startsWith('--') && argv[i + 1] && !argv[i + 1].startsWith('--')) {
        i += 1;
      }
    } else if (arg === '--help' || arg === '-h') {
      options.helpRequested = true;
    } else if (arg === '--callback') {
      options.recv = 'callback';
    } else {
      throw new Error(`unsupported argument: ${arg}`);
    }
  }

  return options;
}

function resolveSinglePatternNames(pattern) {
  return pattern === 'ALL'
    ? ['PAIR', 'PUBSUB', 'DEALER_DEALER', 'DEALER_ROUTER', 'ROUTER_ROUTER', 'SPOT']
    : pattern.split(',').map((value) => value.trim().toUpperCase()).filter(Boolean);
}

function normalizeMultiPatternName(pattern) {
  const upper = pattern.trim().toUpperCase();
  if (upper.startsWith('MULTI_')) {
    return upper;
  }
  return upper === 'STREAM' ? 'MULTI_STREAM' : `MULTI_${upper}`;
}

function resolveMultiPatternNames(pattern) {
  return pattern === 'ALL'
    ? [
      'MULTI_DEALER_DEALER',
      'MULTI_DEALER_ROUTER',
      'MULTI_ROUTER_ROUTER',
      'MULTI_PUBSUB',
      'MULTI_SPOT',
      'MULTI_STREAM'
    ]
    : pattern.split(',').map(normalizeMultiPatternName).filter(Boolean);
}

function defaultSingleMsgSizes() {
  return STANDARD_MSG_SIZES.slice();
}

function defaultMultiMsgSizes(patternNames, explicitMsgSizes) {
  if (explicitMsgSizes) {
    return null;
  }
  const onlyStream = patternNames.length > 0
    && patternNames.every((name) => name === 'MULTI_STREAM');
  return onlyStream ? STREAM_MSG_SIZES.slice() : STANDARD_MSG_SIZES.slice();
}

function defaultMultiClients(patternNames, explicitClients) {
  if (explicitClients) {
    return null;
  }
  const onlyStream = patternNames.length > 0
    && patternNames.every((name) => name === 'MULTI_STREAM');
  return onlyStream ? 10000 : 100;
}

function createPayload(size) {
  if (!Number.isInteger(size) || size <= 0) {
    throw new Error(`invalid payload size: ${size}`);
  }
  const payload = Buffer.alloc(Math.max(size, HEADER_SIZE));
  for (let i = HEADER_SIZE; i < payload.length; i += 1) {
    payload[i] = 0x61 + (i % 23);
  }
  return payload;
}

function applyMetricHeader(buffer, values) {
  buffer.writeUInt32BE(METRIC_MAGIC, 0);
  buffer.writeUInt32BE(values.msgSize >>> 0, 4);
  buffer.writeUInt32BE(values.runId >>> 0, 8);
  buffer.writeUInt8(values.phase & 0xff, 12);
  buffer.writeUInt8(0, 13);
  buffer.writeUInt16BE(HEADER_SIZE, 14);
  buffer.writeBigUInt64BE(values.sentAtNs, 16);
}

function stampPayload(buffer, values) {
  applyMetricHeader(buffer, {
    phase: values.phase,
    runId: values.runId,
    msgSize: values.msgSize,
    sentAtNs: process.hrtime.bigint()
  });
}

function decodeMetricHeader(buffer) {
  if (!Buffer.isBuffer(buffer) || buffer.length < HEADER_SIZE) {
    return null;
  }
  if (buffer.readUInt32BE(0) !== METRIC_MAGIC) {
    return null;
  }
  const headerSize = buffer.readUInt16BE(14);
  if (headerSize !== HEADER_SIZE) {
    return null;
  }
  return {
    magic: METRIC_MAGIC,
    msgSize: buffer.readUInt32BE(4),
    runId: buffer.readUInt32BE(8),
    phase: buffer.readUInt8(12),
    sentAtNs: buffer.readBigUInt64BE(16)
  };
}

function latencyUsFromHeader(header, receivedAtNs) {
  return Number(receivedAtNs - header.sentAtNs) / 1000;
}

function percentile(sortedValues, q) {
  if (sortedValues.length === 0) {
    return 0;
  }
  const index = Math.min(
    sortedValues.length - 1,
    Math.max(0, Math.ceil(sortedValues.length * q) - 1)
  );
  return sortedValues[index];
}

function computeMetrics(latenciesUs, durationSeconds, msgSize) {
  const count = latenciesUs.length;
  const throughput = durationSeconds > 0 ? count / durationSeconds : 0;
  const bandwidth = throughput * msgSize / 1_000_000;
  const sorted = latenciesUs.slice().sort((a, b) => a - b);
  const latency = count > 0 ? sorted.reduce((sum, value) => sum + value, 0) / count : 0;
  const latencyP95 = percentile(sorted, 0.95);
  const latencyP99 = percentile(sorted, 0.99);

  return {
    throughput,
    bandwidth,
    latency,
    latency_p95: latencyP95,
    latency_p99: latencyP99
  };
}

function summarizeMetrics(pattern, transport, msgSize, latenciesUs, durationSeconds) {
  const metrics = computeMetrics(latenciesUs, durationSeconds, msgSize);

  return Object.entries(metrics).map(([metric, value]) =>
    `RESULT,current,${pattern},${transport},${msgSize},${metric},${value.toFixed(2)}`
  );
}

function throughputUnit(pattern) {
  return pattern.includes('STREAM') ? 'Kops/s' : 'Kmsg/s';
}

function formatTableRows(rows) {
  return [
    '| Size | Throughput | Bandwidth | Lat.Mean(ms) | Lat.P95(ms) | Lat.P99(ms) |',
    '|------|------------|-----------|--------------|-------------|-------------|',
    ...rows.map((row) => {
      const throughput = `${(row.metrics.throughput / 1000).toFixed(2)} ${throughputUnit(row.pattern)}`;
      const bandwidth = `${row.metrics.bandwidth.toFixed(2)} MB/s`;
      return `| ${row.msgSize}B | ${throughput} | ${bandwidth} | ${(row.metrics.latency / 1000).toFixed(3)} | ${(row.metrics.latency_p95 / 1000).toFixed(3)} | ${(row.metrics.latency_p99 / 1000).toFixed(3)} |`;
    })
  ];
}

function buildEffectiveOptions(options, extraLines) {
  const lines = [
    `- pattern: ${options.pattern}`,
    `- recv_mode: ${options.recv}`,
    `- duration_seconds: ${options.duration}`,
    `- warmup_seconds: ${options.warmup}`,
    `- msg_sizes: ${options.msgSizes.join(',')}`,
    `- runs: ${options.runs}`,
    `- transports: ${(options.transports || []).join(',') || '-'}`,
    `- results_dir: ${options.resultsDir}`
  ];
  if (typeof options.clients !== 'undefined') {
    lines.push(`- clients: ${options.clients}`);
  }
  if (options.resultsTag) {
    lines.push(`- results_tag: ${options.resultsTag}`);
  }
  if (Array.isArray(extraLines)) {
    lines.push(...extraLines);
  }
  return lines;
}

function formatTimestamp(date) {
  const pad = (value) => String(value).padStart(2, '0');
  return `${date.getFullYear()}${pad(date.getMonth() + 1)}${pad(date.getDate())}_${pad(date.getHours())}${pad(date.getMinutes())}${pad(date.getSeconds())}`;
}

function retainLatestFiles(dir, maxFiles) {
  if (!fs.existsSync(dir)) {
    return;
  }
  const files = fs.readdirSync(dir).sort();
  while (files.length > maxFiles) {
    const oldest = files.shift();
    fs.rmSync(path.join(dir, oldest), { force: true });
  }
}

function writeReport(reportDir, recvMode, resultLines, options, extraSections) {
  fs.mkdirSync(reportDir, { recursive: true });
  retainLatestFiles(reportDir, 100);
  const stamp = formatTimestamp(new Date());
  const tag = options.resultsTag ? `_${options.resultsTag}` : '';
  const file = path.join(
    reportDir,
    `perf_linux_${recvMode}_${stamp}${tag}.txt`
  );
  const sections = Array.isArray(extraSections) ? extraSections : [];
  const content = [
    '## Effective Options (start)',
    ...buildEffectiveOptions({ ...options, recv: recvMode }),
    '',
    ...sections,
    ...(sections.length > 0 ? [''] : []),
    '## Result Data',
    ...resultLines,
    '',
    '## Effective Options (result)',
    ...buildEffectiveOptions({ ...options, recv: recvMode }),
    `- result_lines: ${resultLines.length}`,
    '## Status Summary',
    `- result_lines: ${resultLines.length}`,
    `- status: ${resultLines.length > 0 ? 'complete' : 'partial'}`,
    `- expected_result_lines: ${resultLines.length}`,
    `- actual_result_lines: ${resultLines.length}`
  ].join('\n');
  fs.writeFileSync(file, `${content}\n`, 'utf8');
  if (options.output) {
    fs.writeFileSync(options.output, `${content}\n`, 'utf8');
  }
  return file;
}

function sleepImmediate() {
  return new Promise((resolve) => setImmediate(resolve));
}

function createRunId() {
  return (Math.random() * 0xffffffff) >>> 0;
}

function createMetricCollector(config) {
  const worker = new Worker(path.join(__dirname, 'perf_metric_worker.js'), {
    workerData: config
  });
  let closed = false;

  const closeWorker = async () => {
    if (closed) {
      return;
    }
    closed = true;
    await worker.terminate();
  };

  return {
    record(header, receivedAtNs) {
      if (!header || closed) {
        return;
      }
      worker.postMessage({
        type: 'sample',
        msgSize: header.msgSize,
        runId: header.runId,
        phase: header.phase,
        sentAtNs: header.sentAtNs,
        receivedAtNs
      });
    },
    async finish() {
      return new Promise((resolve, reject) => {
        const cleanup = () => {
          worker.removeAllListeners('message');
          worker.removeAllListeners('error');
          worker.removeAllListeners('exit');
        };
        worker.once('message', (message) => {
          closed = true;
          cleanup();
          resolve(message);
        });
        worker.once('error', (error) => {
          closed = true;
          cleanup();
          reject(error);
        });
        worker.once('exit', (code) => {
          if (code !== 0) {
            closed = true;
            cleanup();
            reject(new Error(`metric worker exited with code ${code}`));
          }
        });
        worker.postMessage({ type: 'finish' });
      }).finally(() => {
        worker.unref();
      });
    },
    close() {
      return closeWorker();
    }
  };
}

module.exports = {
  DEFAULT_MULTI_TRANSPORTS,
  DEFAULT_SINGLE_TRANSPORTS,
  HEADER_SIZE,
  STANDARD_MSG_SIZES,
  STREAM_MSG_SIZES,
  buildEffectiveOptions,
  computeMetrics,
  createMetricCollector,
  createPayload,
  createRunId,
  defaultMultiClients,
  defaultMultiMsgSizes,
  defaultSingleMsgSizes,
  decodeMetricHeader,
  formatTableRows,
  latencyUsFromHeader,
  parseCommonArgs,
  resolveMultiPatternNames,
  resolveSinglePatternNames,
  sleepImmediate,
  stampPayload,
  summarizeMetrics,
  writeReport
};
