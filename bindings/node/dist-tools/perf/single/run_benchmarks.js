// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const path = require('node:path');
const { buildEffectiveOptions, computeMetrics, defaultSingleMsgSizes, DEFAULT_SINGLE_TRANSPORTS, formatTableRows, parseCommonArgs, resolveSinglePatternNames, summarizeMetrics, writeReport } = require('../common/perf_metrics');
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
const POLICY_TRANSPORTS = {
    PAIR: ['tcp', 'tls', 'ws', 'wss', 'inproc', 'ipc'],
    PUBSUB: ['tcp', 'tls', 'ws', 'wss', 'inproc', 'ipc'],
    DEALER_DEALER: ['tcp', 'tls', 'ws', 'wss', 'inproc', 'ipc'],
    DEALER_ROUTER: ['tcp', 'tls', 'ws', 'wss', 'inproc', 'ipc'],
    ROUTER_ROUTER: ['tcp', 'tls', 'ws', 'wss', 'inproc', 'ipc'],
    SPOT: ['tcp', 'tls', 'ws', 'wss'],
    SPOT_REQREP: ['tcp', 'tls', 'ws', 'wss']
};
const RUNNABLE_TRANSPORTS = {
    PAIR: [],
    PUBSUB: [],
    DEALER_DEALER: [],
    DEALER_ROUTER: [],
    ROUTER_ROUTER: [],
    SPOT: [],
    SPOT_REQREP: []
};
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
  --warmup N            Accepted for compatibility but ignored.
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
function isUnsupported(error) {
    const text = String(error && error.message ? error.message : error).toLowerCase();
    return text.includes('protocol not supported')
        || text.includes('unsupported single transport')
        || text.includes('listen eperm')
        || text.includes('operation not permitted');
}
async function main() {
    const options = parseCommonArgs(process.argv.slice(2), {
        pattern: 'ALL',
        duration: 5,
        warmup: 0,
        msgSizes: defaultSingleMsgSizes(),
        resultsDir: path.join(process.cwd(), 'perf', 'results'),
        transports: DEFAULT_SINGLE_TRANSPORTS
    });
    if (options.helpRequested) {
        usage();
        return;
    }
    options.warmup = 0;
    const names = resolveSinglePatternNames(options.pattern);
    const resultLines = [];
    const reportSections = [];
    let unsupportedCount = 0;
    let failCount = 0;
    let caseOrdinal = 1;
    console.log('## Effective Options (start)');
    for (const line of buildEffectiveOptions({ ...options, lang: 'node', suite: 'single', patterns: names.join(',') })) {
        console.log(line);
    }
    console.log('');
    for (const name of names) {
        const runner = PATTERNS[name];
        if (!runner) {
            throw new Error(`unsupported single pattern: ${name}`);
        }
        if (reportSections.length > 0) {
            reportSections.push('===============================================================================');
            reportSections.push('');
            console.log('===============================================================================');
            console.log('');
        }
        console.log(`## PATTERN: ${name}`);
        reportSections.push(`## PATTERN: ${name}`);
        for (const transport of options.transports) {
            if (!POLICY_TRANSPORTS[name].includes(transport)) {
                const line = `UNSUPPORTED,current,${name},${transport}`;
                console.log(line);
                resultLines.push(line);
                unsupportedCount += 1;
                continue;
            }
            if (!RUNNABLE_TRANSPORTS[name].includes(transport)) {
                const line = `UNSUPPORTED,current,${name},${transport}`;
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
                    try {
                        const latenciesNs = await runner(msgSize, {
                            ...options,
                            transport,
                            runId: caseOrdinal
                        });
                        caseOrdinal += 1;
                        const metrics = computeMetrics(latenciesNs, options.duration, msgSize, name === 'SPOT_REQREP' ? 2 : 1);
                        runMetricsList.push(metrics);
                        resultLines.push(...summarizeMetrics(name, transport, msgSize, latenciesNs, options.duration));
                    }
                    catch (error) {
                        if (isUnsupported(error)) {
                            const line = `UNSUPPORTED,current,${name},${transport}`;
                            console.log(line);
                            resultLines.push(line);
                            unsupportedCount += 1;
                            unsupported = true;
                            break;
                        }
                        throw error;
                    }
                }
                if (unsupported) {
                    break;
                }
                if (runMetricsList.length !== options.runs) {
                    failCount += 1;
                    continue;
                }
                rows.push({
                    pattern: name,
                    msgSize,
                    metrics: options.runs > 1 ? medianMetrics(runMetricsList) : runMetricsList[0]
                });
            }
            if (rows.length > 0) {
                const tableLines = formatTableRows(rows);
                reportSections.push(`### Transport: ${transport}`);
                reportSections.push(...tableLines);
                reportSections.push('');
                console.log(`### Transport: ${transport}`);
                for (const line of tableLines) {
                    console.log(line);
                }
                console.log('');
            }
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
    const report = writeReport(path.join(options.resultsDir, 'single', 'report'), 'node', 'single', resultLines, { ...options, patterns: names.join(',') }, reportSections);
    console.log(`report=${report}`);
    if (failCount > 0) {
        process.exitCode = 1;
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
