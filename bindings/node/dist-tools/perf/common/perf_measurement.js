// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const METRIC_MAGIC = 0x5a4c4e4b;
const HEADER_SIZE = 29;
const BASE_EPOCH_NS = BigInt(Date.now()) * 1000000n;
const BASE_HR_NS = process.hrtime.bigint();
const PRIMARY_METRICS = ['throughput', 'bandwidth', 'latency', 'latency_p95', 'latency_p99'];
function currentEpochNs() {
    return BASE_EPOCH_NS + (process.hrtime.bigint() - BASE_HR_NS);
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
    const sentTsNs = values.sentTsNs ?? currentEpochNs();
    buffer.writeUInt32LE(METRIC_MAGIC, 0);
    buffer.writeUInt32LE(values.runId >>> 0, 4);
    buffer.writeUInt8(values.phase & 0xff, 8);
    buffer.writeUInt32LE(values.msgSize >>> 0, 9);
    buffer.writeBigUInt64LE(BigInt(values.seq ?? 0), 13);
    buffer.writeBigInt64LE(BigInt(sentTsNs), 21);
}
function stampPayload(buffer, values) {
    applyMetricHeader(buffer, {
        phase: values.phase,
        runId: values.runId,
        msgSize: values.msgSize,
        seq: values.seq,
        sentTsNs: values.sentTsNs
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
        sentTsNs: buffer.readBigInt64LE(21)
    };
}
function decodeMetricHeaderFromParts(parts) {
    for (const part of parts || []) {
        if (!part || typeof part.data !== 'function') {
            continue;
        }
        const data = part.data();
        let header = decodeMetricHeader(data);
        if (header) {
            return header;
        }
        const magicBytes = Buffer.allocUnsafe(4);
        magicBytes.writeUInt32LE(METRIC_MAGIC, 0);
        const offset = data.indexOf(magicBytes);
        if (offset >= 0 && (data.length - offset) >= HEADER_SIZE) {
            header = decodeMetricHeader(data.subarray(offset));
            if (header) {
                return header;
            }
        }
    }
    return null;
}
function latencyNsFromPayload(buffer, receivedAtNs = currentEpochNs()) {
    const header = decodeMetricHeader(buffer);
    if (!header) {
        return 0;
    }
    return Number(receivedAtNs - header.sentTsNs);
}
function percentile(sortedValues, q) {
    if (sortedValues.length === 0) {
        return 0;
    }
    const index = Math.min(sortedValues.length - 1, Math.max(0, Math.ceil(sortedValues.length * q) - 1));
    return sortedValues[index];
}
function computeMetrics(latenciesNs, durationSeconds, msgSize, bandwidthMultiplier = 1, throughputCount = latenciesNs.length) {
    const count = latenciesNs.length;
    const effectiveCount = Number.isFinite(throughputCount) && throughputCount >= 0
        ? throughputCount
        : count;
    const throughput = durationSeconds > 0 ? effectiveCount / durationSeconds : 0;
    const bandwidth = throughput * msgSize * bandwidthMultiplier / 1_000_000;
    const sorted = latenciesNs.slice().sort((a, b) => a - b);
    const latency = count > 0 ? sorted.reduce((sum, value) => sum + value, 0) / count : 0;
    const latencyP95 = percentile(sorted, 0.95);
    const latencyP99 = percentile(sorted, 0.99);
    return {
        throughput,
        bandwidth,
        latency: latency / 1_000_000,
        latency_p95: latencyP95 / 1_000_000,
        latency_p99: latencyP99 / 1_000_000
    };
}
function isEchoPattern(pattern) {
    return pattern === 'SPOT_REQREP'
        || pattern === 'MULTI_DEALER_ROUTER'
        || pattern === 'MULTI_ROUTER_ROUTER'
        || pattern === 'MULTI_STREAM'
        || pattern === 'MULTI_SPOT_REQREP';
}
function summarizeMetrics(pattern, transport, msgSize, latenciesNs, durationSeconds, libName = 'current', throughputCount = latenciesNs.length) {
    const metrics = computeMetrics(latenciesNs, durationSeconds, msgSize, isEchoPattern(pattern) ? 2 : 1, throughputCount);
    return Object.entries(metrics).map(([metric, value]) => {
        const formatted = metric.startsWith('latency')
            ? value.toFixed(6)
            : value.toFixed(2);
        return `RESULT,${libName},${pattern},${transport},${msgSize},${metric},${formatted}`;
    });
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
        if (PRIMARY_METRICS.includes(parts[5])) {
            metrics[parts[5]] = Number(parts[6]);
        }
    }
    if (PRIMARY_METRICS.some((metric) => typeof metrics[metric] !== 'number')) {
        throw new Error(`missing primary metrics for ${pattern} size ${msgSize}`);
    }
    return metrics;
}
function sleepImmediate() {
    return new Promise((resolve) => setImmediate(resolve));
}
function createRunId(caseOrdinal = 1) {
    if (!Number.isInteger(caseOrdinal) || caseOrdinal <= 0) {
        throw new Error(`invalid run id ordinal: ${caseOrdinal}`);
    }
    return caseOrdinal >>> 0;
}
function createMetricCollector(config) {
    const latenciesNs = [];
    const runId = config.runId >>> 0;
    const msgSize = config.msgSize >>> 0;
    const activeStartNs = config.activeStartNs === undefined
        ? 0n
        : BigInt(config.activeStartNs);
    const activeStopNs = config.activeStopNs === undefined
        ? BigInt('0xffffffffffffffff')
        : BigInt(config.activeStopNs);
    const rttDivisor = config.roundTrip ? 2n : 1n;
    const sampleCap = Number.isFinite(config.sampleCap) && config.sampleCap > 0
        ? Math.trunc(config.sampleCap)
        : Number.POSITIVE_INFINITY;
    let accepted = 0;
    let rejected = 0;
    let closed = false;
    return {
        record(header, receivedAtNs) {
            if (!header || closed) {
                return;
            }
            if ((header.runId >>> 0) !== runId || (header.msgSize >>> 0) !== msgSize) {
                rejected += 1;
                return;
            }
            if (header.phase !== 1) {
                return;
            }
            const sentTsNs = BigInt(header.sentTsNs);
            const recvTsNs = BigInt(receivedAtNs);
            if (recvTsNs < activeStartNs || recvTsNs > activeStopNs) {
                return;
            }
            if (recvTsNs < sentTsNs) {
                rejected += 1;
                return;
            }
            accepted += 1;
            if (latenciesNs.length < sampleCap) {
                latenciesNs.push(Number((recvTsNs - sentTsNs) / rttDivisor));
            }
        },
        async finish() {
            closed = true;
            return {
                latenciesNs,
                accepted,
                rejected
            };
        },
        close() {
            closed = true;
            return Promise.resolve();
        }
    };
}
module.exports = {
    HEADER_SIZE,
    METRIC_MAGIC,
    PRIMARY_METRICS,
    computeMetrics,
    createMetricCollector,
    createPayload,
    createRunId,
    decodeMetricHeader,
    decodeMetricHeaderFromParts,
    currentEpochNs,
    latencyNsFromPayload,
    primaryMetricsFromResultLines,
    sleepImmediate,
    stampPayload,
    summarizeMetrics
};
