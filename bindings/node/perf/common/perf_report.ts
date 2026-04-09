// SPDX-License-Identifier: MPL-2.0

'use strict';

const fs = require('node:fs');
const path = require('node:path');

function buildEffectiveOptions(options, extraLines) {
  const patterns = options.patterns || options.pattern || '-';
  const lines = [
    `- lang: ${options.lang}`,
    `- suite: ${options.suite}`,
    `- runs: ${options.runs}`,
    `- patterns: ${patterns}`,
    `- transports: ${(options.transports || []).join(',') || '-'}`,
    `- msg_sizes: ${options.msgSizes.join(',')}`,
    `- duration_seconds: ${options.duration}`,
    `- warmup_seconds: ${options.warmup}`,
    `- results_dir: ${options.resultsDir}`,
    `- pin_cpu: ${options.pinCpu ? 'on' : 'off'}`
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
  return pattern === 'MULTI_DEALER_ROUTER'
    || pattern === 'MULTI_ROUTER_ROUTER'
    || pattern === 'MULTI_STREAM'
    ? 'Kops/s'
    : 'Kmsg/s';
}

function formatTableRows(rows) {
  const latencyUnit = 'ms';
  return [
    `| Size | Throughput | Bandwidth | Lat.Mean(${latencyUnit}) | Lat.P95(${latencyUnit}) | Lat.P99(${latencyUnit}) |`,
    '|------|------------|-----------|----------------|---------------|---------------|',
    ...rows.map((row) => {
      const throughput = `${(row.metrics.throughput / 1000).toFixed(2)} ${throughputUnit(row.pattern)}`;
      const bandwidth = `${row.metrics.bandwidth.toFixed(2)} MB/s`;
      return `| ${row.msgSize}B | ${throughput} | ${bandwidth} | ${row.metrics.latency.toFixed(6)} | ${row.metrics.latency_p95.toFixed(6)} | ${row.metrics.latency_p99.toFixed(6)} |`;
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

function platformName() {
  if (process.platform === 'win32') {
    return 'windows';
  }
  if (process.platform === 'darwin') {
    return 'macos';
  }
  return 'linux';
}

function writeReport(reportDir, lang, suite, resultLines, options, extraSections) {
  fs.mkdirSync(reportDir, { recursive: true });
  const maxFiles = suite === 'multi'
    ? Number(process.env.PERF_RESULTS_MAX_FILES || 100)
    : 100;
  retainLatestFiles(reportDir, Number.isFinite(maxFiles) && maxFiles > 0 ? maxFiles : 100);
  const stamp = formatTimestamp(new Date());
  const tag = options.resultsTag ? `_${options.resultsTag}` : '';
  const file = path.join(
    reportDir,
    `perf_${lang}_${suite}_${platformName()}_${stamp}${tag}.txt`
  );
  const sections = Array.isArray(extraSections) ? extraSections : [];
  const content = [
    '## Effective Options (start)',
    ...buildEffectiveOptions({ ...options, lang, suite }),
    '',
    ...sections,
    ...(sections.length > 0 ? [''] : []),
    '## Result Data',
    ...resultLines,
    '',
    '## Effective Options (result)',
    ...buildEffectiveOptions({ ...options, lang, suite }),
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
