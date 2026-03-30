// SPDX-License-Identifier: MPL-2.0

'use strict';

const fs = require('node:fs');
const path = require('node:path');

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

function parseCommonArgs(argv, defaults) {
  const options = {
    pattern: defaults.pattern,
    recv: defaults.recv,
    duration: defaults.duration,
    warmup: defaults.warmup,
    msgSizes: defaults.msgSizes,
    resultsDir: defaults.resultsDir,
    resultsTag: '',
    output: ''
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
    } else if (
      arg === '--runs'
      || arg === '--transports'
      || arg === '--build-dir'
      || arg === '--reuse-build'
      || arg === '--clean-build'
      || arg === '--help'
    ) {
      if (arg.startsWith('--') && argv[i + 1] && !argv[i + 1].startsWith('--')) {
        i += 1;
      }
    } else {
      throw new Error(`unsupported argument: ${arg}`);
    }
  }

  return options;
}

function createPayload(size) {
  const payload = Buffer.alloc(size);
  for (let i = 8; i < payload.length; i += 1) {
    payload[i] = 0x61 + (i % 23);
  }
  return payload;
}

function stampPayload(buffer) {
  buffer.writeBigUInt64BE(process.hrtime.bigint(), 0);
}

function setPayloadPhase(buffer, phase) {
  if (buffer.length > 8) {
    buffer[8] = phase & 0xff;
  }
}

function payloadPhase(buffer) {
  if (buffer.length > 8) {
    return buffer[8];
  }
  return 0;
}

function latencyUsFromPayload(buffer) {
  const sentAt = buffer.readBigUInt64BE(0);
  return Number(process.hrtime.bigint() - sentAt) / 1000;
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

function summarizeMetrics(pattern, transport, msgSize, latenciesUs, durationSeconds) {
  const count = latenciesUs.length;
  const throughput = durationSeconds > 0 ? count / durationSeconds : 0;
  const bandwidth = throughput * msgSize / 1_000_000;
  const sorted = latenciesUs.slice().sort((a, b) => a - b);
  const latency = count > 0 ? sorted.reduce((sum, value) => sum + value, 0) / count : 0;
  const latencyP95 = percentile(sorted, 0.95);
  const latencyP99 = percentile(sorted, 0.99);

  const metrics = {
    throughput,
    bandwidth,
    latency,
    latency_p95: latencyP95,
    latency_p99: latencyP99
  };

  return Object.entries(metrics).map(([metric, value]) =>
    `RESULT,current,${pattern},${transport},${msgSize},${metric},${value.toFixed(2)}`
  );
}

function formatTimestamp(date) {
  const pad = (value) => String(value).padStart(2, '0');
  return `${date.getFullYear()}${pad(date.getMonth() + 1)}${pad(date.getDate())}_${pad(date.getHours())}${pad(date.getMinutes())}${pad(date.getSeconds())}`;
}

function writeReport(reportDir, recvMode, resultLines, options) {
  fs.mkdirSync(reportDir, { recursive: true });
  const stamp = formatTimestamp(new Date());
  const tag = options.resultsTag ? `_${options.resultsTag}` : '';
  const file = path.join(
    reportDir,
    `perf_linux_${recvMode}_${stamp}${tag}.txt`
  );
  const content = [
    '## Effective Options (start)',
    `- pattern: ${options.pattern}`,
    `- recv_mode: ${recvMode}`,
    `- duration: ${options.duration}`,
    `- warmup: ${options.warmup}`,
    `- msg_sizes: ${options.msgSizes.join(',')}`,
    '',
    ...resultLines,
    '',
    '## Effective Options (result)',
    `- recv_mode: ${recvMode}`,
    `- result_lines: ${resultLines.length}`
  ].join('\n');
  fs.writeFileSync(file, `${content}\n`, 'utf8');
  if (options.output) {
    fs.writeFileSync(options.output, `${content}\n`, 'utf8');
  }
  return file;
}

module.exports = {
  createPayload,
  latencyUsFromPayload,
  parseCommonArgs,
  payloadPhase,
  setPayloadPhase,
  stampPayload,
  summarizeMetrics,
  writeReport
};
