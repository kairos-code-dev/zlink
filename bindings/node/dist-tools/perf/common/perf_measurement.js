// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const path = require('node:path');
const { Worker } = require('node:worker_threads');
const { performance } = require('node:perf_hooks');
const METRIC_MAGIC = 0x5a4c4e4b;
const HEADER_SIZE = 29;
function currentEpochUs() {
    return BigInt(Math.round((performance.timeOrigin + performance.now()) * 1000));
}
function createPayload(size) {
    if (!Number.isInteger(size) || size < HEADER_SIZE) {
        throw new Error(`invalid payload size: ${size}`);
    }
    const payload = Buffer.alloc(size);
    for (let i = HEADER_SIZE; i < payload.length; i += 1) {
        payload[i] = 0x61 + (i % 23);
    }
    return payload;
}
function applyMetricHeader(buffer, values) {
    buffer.writeUInt32LE(METRIC_MAGIC, 0);
    buffer.writeUInt32LE(values.runId >>> 0, 4);
    buffer.writeUInt8(values.phase & 0xff, 8);
    buffer.writeUInt32LE(values.msgSize >>> 0, 9);
    buffer.writeBigUInt64LE(BigInt(values.seq ?? 0), 13);
    buffer.writeBigInt64LE(BigInt(values.sentTsUs ?? currentEpochUs()), 21);
}
function stampPayload(buffer, values) {
    applyMetricHeader(buffer, {
        phase: values.phase,
        runId: values.runId,
        msgSize: values.msgSize,
        seq: values.seq,
        sentTsUs: values.sentTsUs
    });
}
function decodeMetricHeader(buffer) {
    if (!Buffer.isBuffer(buffer) || buffer.length < HEADER_SIZE) {
        return null;
    }
    if (buffer.readUInt32LE(0) !== METRIC_MAGIC) {
        return null;
    }
    return {
        magic: METRIC_MAGIC,
        runId: buffer.readUInt32LE(4),
        phase: buffer.readUInt8(8),
        msgSize: buffer.readUInt32LE(9),
        seq: buffer.readBigUInt64LE(13),
        sentTsUs: buffer.readBigInt64LE(21)
    };
}
function latencyUsFromPayload(buffer, receivedAtUs = currentEpochUs()) {
    const header = decodeMetricHeader(buffer);
    if (!header) {
        return 0;
    }
    return Number(receivedAtUs - header.sentTsUs);
}
function percentile(sortedValues, q) {
    if (sortedValues.length === 0) {
        return 0;
    }
    const index = Math.min(sortedValues.length - 1, Math.max(0, Math.ceil(sortedValues.length * q) - 1));
    return sortedValues[index];
}
function computeMetrics(latenciesUs, durationSeconds, msgSize, bandwidthMultiplier = 1) {
    const count = latenciesUs.length;
    const throughput = durationSeconds > 0 ? count / durationSeconds : 0;
    const bandwidth = throughput * msgSize * bandwidthMultiplier / 1_000_000;
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
function isEchoPattern(pattern) {
    return pattern === 'MULTI_DEALER_ROUTER'
        || pattern === 'MULTI_ROUTER_ROUTER'
        || pattern === 'MULTI_STREAM';
}
function summarizeMetrics(pattern, transport, msgSize, latenciesUs, durationSeconds) {
    const metrics = computeMetrics(latenciesUs, durationSeconds, msgSize, isEchoPattern(pattern) ? 2 : 1);
    return Object.entries(metrics).map(([metric, value]) => `RESULT,current,${pattern},${transport},${msgSize},${metric},${value.toFixed(2)}`);
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
                sentTsUs: header.sentTsUs,
                receivedAtUs: receivedAtNs
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
    HEADER_SIZE,
    METRIC_MAGIC,
    computeMetrics,
    createMetricCollector,
    createPayload,
    createRunId,
    decodeMetricHeader,
    currentEpochUs,
    latencyUsFromPayload,
    primaryMetricsFromResultLines,
    sleepImmediate,
    stampPayload,
    summarizeMetrics
};
