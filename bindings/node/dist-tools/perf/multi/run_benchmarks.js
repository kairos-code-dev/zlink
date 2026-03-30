// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const path = require('node:path');
const { parseCommonArgs, summarizeMetrics, writeReport } = require('../common/perf_metrics');
const { spawnMultiPair } = require('./perf_multi_common');
const { runStreamBenchmark } = require('./perf_stream');
async function main() {
    const options = parseCommonArgs(process.argv.slice(2), {
        pattern: 'STREAM',
        recv: 'recv',
        duration: 2,
        warmup: 1,
        msgSizes: [256],
        resultsDir: path.join(process.cwd(), 'perf', 'results')
    });
    const patternNames = options.pattern === 'ALL'
        ? ['MULTI_DEALER_DEALER', 'MULTI_PUBSUB', 'STREAM']
        : options.pattern.split(',').map((value) => value.trim().toUpperCase());
    if (options.recv !== 'recv' && options.recv !== 'callback') {
        throw new Error('multi perf supports --recv recv|callback');
    }
    const resultLines = [];
    for (const patternName of patternNames) {
        for (const msgSize of options.msgSizes) {
            if (patternName === 'STREAM') {
                const latenciesUs = await runStreamBenchmark(msgSize, options);
                const lines = summarizeMetrics('STREAM', 'tcp', msgSize, latenciesUs, options.duration);
                for (const line of lines) {
                    console.log(line);
                    resultLines.push(line);
                }
                continue;
            }
            if (patternName === 'MULTI_DEALER_DEALER') {
                if (options.recv !== 'recv') {
                    throw new Error('MULTI_DEALER_DEALER supports only --recv recv');
                }
                const lines = await spawnMultiPair('perf_multi_dealer_dealer_server.js', 'perf_multi_dealer_dealer_client.js', { ...options, msgSize, clients: 2 });
                for (const line of lines) {
                    console.log(line);
                    resultLines.push(line);
                }
                continue;
            }
            if (patternName === 'MULTI_PUBSUB') {
                if (options.recv !== 'recv') {
                    throw new Error('MULTI_PUBSUB supports only --recv recv');
                }
                const lines = await spawnMultiPair('perf_multi_pubsub_server.js', 'perf_multi_pubsub_client.js', { ...options, msgSize, clients: 2 });
                for (const line of lines) {
                    console.log(line);
                    resultLines.push(line);
                }
                continue;
            }
            throw new Error(`unsupported multi pattern: ${patternName}`);
        }
    }
    const report = writeReport(path.join(options.resultsDir, 'multi', 'report'), options.recv, resultLines, options);
    console.log(`report=${report}`);
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
