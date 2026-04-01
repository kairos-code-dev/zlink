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
const PATTERNS = {
    PAIR: runPairBenchmark,
    PUBSUB: runPubSubBenchmark,
    DEALER_DEALER: runDealerDealerBenchmark,
    DEALER_ROUTER: runDealerRouterBenchmark,
    ROUTER_ROUTER: runRouterRouterBenchmark,
    SPOT: runSpotBenchmark
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
  --recv MODE           Receive model: callback (default: callback).
  --duration N          Override single duration seconds (default: 5).
  --warmup N            Override single warmup seconds (default: 2).
  --msg-sizes LIST      Comma-separated sizes (default: 64,256,1024,65536,131072,262144).
  --transports LIST     Comma-separated transports (default: inproc).

Notes:
  - result is saved under perf/results/single/report/ as
    perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt.
  - single suite supports callback mode only.`);
}
async function main() {
    const options = parseCommonArgs(process.argv.slice(2), {
        pattern: 'ALL',
        recv: 'callback',
        duration: 5,
        warmup: 2,
        msgSizes: defaultSingleMsgSizes(),
        resultsDir: path.join(process.cwd(), 'perf', 'results'),
        transports: DEFAULT_SINGLE_TRANSPORTS
    });
    if (options.helpRequested) {
        usage();
        return;
    }
    if (options.recv !== 'callback') {
        throw new Error('single perf supports only --recv callback');
    }
    if (options.transports.some((transport) => transport !== 'inproc')) {
        throw new Error('single perf currently supports only --transports inproc');
    }
    const names = resolveSinglePatternNames(options.pattern);
    const resultLines = [];
    const reportSections = [];
    console.log('## Effective Options (start)');
    for (const line of buildEffectiveOptions(options)) {
        console.log(line);
    }
    console.log('');
    for (const name of names) {
        const runner = PATTERNS[name];
        if (!runner) {
            throw new Error(`unsupported single pattern: ${name}`);
        }
        const patternRows = [];
        for (const msgSize of options.msgSizes) {
            const latenciesUs = await runner(msgSize, options);
            const metrics = computeMetrics(latenciesUs, options.duration, msgSize);
            const lines = summarizeMetrics(name, 'inproc', msgSize, latenciesUs, options.duration);
            for (const line of lines) {
                console.log(line);
                resultLines.push(line);
            }
            const row = { pattern: name, msgSize, metrics };
            patternRows.push(row);
        }
        reportSections.push(`## PATTERN: ${name}`);
        reportSections.push(...formatTableRows(patternRows));
        reportSections.push('');
        console.log(`## PATTERN: ${name}`);
        for (const line of formatTableRows(patternRows)) {
            console.log(line);
        }
        console.log('');
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
    const report = writeReport(path.join(options.resultsDir, 'single', 'report'), options.recv, resultLines, options, reportSections);
    console.log(`report=${report}`);
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
