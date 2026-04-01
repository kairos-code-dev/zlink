// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const path = require('node:path');
const { buildEffectiveOptions, defaultMultiClients, defaultMultiMsgSizes, DEFAULT_MULTI_TRANSPORTS, formatTableRows, parseCommonArgs, resolveMultiPatternNames, writeReport } = require('../common/perf_metrics');
const { spawnMultiPair } = require('./perf_multi_common');
function usage() {
    console.log(`Usage: bindings/node/perf/run_benchmarks_multi.sh [options]

Measure current zlink Node multi-pattern performance.

Options:
  -h, --help            Show this help.
  --pattern NAME        Pattern list (comma-separated) or ALL.
  --results-dir PATH    Override result root directory.
  --results-tag NAME    Optional tag in saved result filename.
  --runs N              Iterations per configuration (default: 1).
  --recv MODE           Receive model: recv|callback (default: recv).
  --callback            Alias of --recv callback.
  --duration N          Override multi duration seconds (default: 5).
  --warmup N            Override multi warmup seconds (default: 2).
  --msg-sizes LIST      Comma-separated sizes.
  --transports LIST     Comma-separated transports (default: tcp).
  --clients N           Override number of client sockets per pattern (default: 100, stream=10000).

Notes:
  - result is saved under perf/results/multi/report/ as
    perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt.`);
}
function primaryMetricsFromResultLines(pattern, msgSize, lines) {
    const metrics = {};
    for (const line of lines) {
        const parts = line.split(',');
        if (parts.length !== 7) {
            continue;
        }
        if (parts[2] !== pattern || Number(parts[4]) !== msgSize) {
            continue;
        }
        if (parts[5] === 'throughput'
            || parts[5] === 'bandwidth'
            || parts[5] === 'latency'
            || parts[5] === 'latency_p95'
            || parts[5] === 'latency_p99') {
            metrics[parts[5]] = Number(parts[6]);
        }
    }
    if (typeof metrics.throughput !== 'number'
        || typeof metrics.bandwidth !== 'number'
        || typeof metrics.latency !== 'number'
        || typeof metrics.latency_p95 !== 'number'
        || typeof metrics.latency_p99 !== 'number') {
        throw new Error(`missing primary metrics for ${pattern} size ${msgSize}`);
    }
    return metrics;
}
async function main() {
    const options = parseCommonArgs(process.argv.slice(2), {
        pattern: 'ALL',
        recv: 'recv',
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
    if (options.recv !== 'recv' && options.recv !== 'callback') {
        throw new Error('multi perf supports --recv recv|callback');
    }
    if (options.transports.some((transport) => transport !== 'tcp')) {
        throw new Error('multi perf currently supports only --transports tcp');
    }
    const resultLines = [];
    const reportSections = [];
    console.log('## Effective Options (start)');
    for (const line of buildEffectiveOptions(options, [
        '- default_clients: 100',
        '- default_stream_clients: 10000'
    ])) {
        console.log(line);
    }
    console.log('');
    for (const patternName of patternNames) {
        const patternRows = [];
        for (const msgSize of options.msgSizes) {
            if (patternName === 'MULTI_DEALER_DEALER') {
                if (options.recv !== 'recv') {
                    throw new Error('MULTI_DEALER_DEALER supports only --recv recv');
                }
                const lines = await spawnMultiPair('perf_multi_dealer_dealer_server.js', 'perf_multi_dealer_dealer_client.js', { ...options, pattern: patternName, msgSize, clients: options.clients });
                patternRows.push({
                    pattern: patternName,
                    msgSize,
                    metrics: primaryMetricsFromResultLines(patternName, msgSize, lines)
                });
                resultLines.push(...lines);
                continue;
            }
            if (patternName === 'MULTI_PUBSUB') {
                if (options.recv !== 'recv') {
                    throw new Error('MULTI_PUBSUB supports only --recv recv');
                }
                const lines = await spawnMultiPair('perf_multi_pubsub_server.js', 'perf_multi_pubsub_client.js', { ...options, pattern: patternName, msgSize, clients: options.clients });
                patternRows.push({
                    pattern: patternName,
                    msgSize,
                    metrics: primaryMetricsFromResultLines(patternName, msgSize, lines)
                });
                resultLines.push(...lines);
                continue;
            }
            if (patternName === 'MULTI_DEALER_ROUTER') {
                if (options.recv !== 'recv') {
                    throw new Error('MULTI_DEALER_ROUTER supports only --recv recv');
                }
                const lines = await spawnMultiPair('perf_multi_dealer_router_server.js', 'perf_multi_dealer_router_client.js', { ...options, pattern: patternName, msgSize, clients: options.clients });
                patternRows.push({
                    pattern: patternName,
                    msgSize,
                    metrics: primaryMetricsFromResultLines(patternName, msgSize, lines)
                });
                resultLines.push(...lines);
                continue;
            }
            if (patternName === 'MULTI_ROUTER_ROUTER') {
                if (options.recv !== 'recv') {
                    throw new Error('MULTI_ROUTER_ROUTER supports only --recv recv');
                }
                const lines = await spawnMultiPair('perf_multi_router_router_server.js', 'perf_multi_router_router_client.js', { ...options, pattern: patternName, msgSize, clients: options.clients });
                patternRows.push({
                    pattern: patternName,
                    msgSize,
                    metrics: primaryMetricsFromResultLines(patternName, msgSize, lines)
                });
                resultLines.push(...lines);
                continue;
            }
            if (patternName === 'MULTI_STREAM') {
                const lines = await spawnMultiPair('perf_multi_stream_server.js', 'perf_multi_stream_client.js', { ...options, pattern: patternName, msgSize, clients: options.clients });
                patternRows.push({
                    pattern: patternName,
                    msgSize,
                    metrics: primaryMetricsFromResultLines(patternName, msgSize, lines)
                });
                resultLines.push(...lines);
                continue;
            }
            if (patternName === 'MULTI_SPOT') {
                const lines = await spawnMultiPair('perf_multi_spot_server.js', 'perf_multi_spot_client.js', { ...options, pattern: patternName, msgSize, clients: options.clients });
                patternRows.push({
                    pattern: patternName,
                    msgSize,
                    metrics: primaryMetricsFromResultLines(patternName, msgSize, lines)
                });
                resultLines.push(...lines);
                continue;
            }
            throw new Error(`unsupported multi pattern: ${patternName}`);
        }
        if (patternRows.length > 0) {
            const tableLines = formatTableRows(patternRows);
            reportSections.push(`## PATTERN: ${patternName}`);
            reportSections.push(...tableLines);
            reportSections.push('');
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
    const report = writeReport(path.join(options.resultsDir, 'multi', 'report'), options.recv, resultLines, options, reportSections);
    console.log(`report=${report}`);
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
