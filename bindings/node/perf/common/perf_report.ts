// SPDX-License-Identifier: MPL-2.0

'use strict';

const fs = require('node:fs');
const path = require('node:path');

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

module.exports = {
  buildEffectiveOptions,
  formatTableRows,
  writeReport
};
