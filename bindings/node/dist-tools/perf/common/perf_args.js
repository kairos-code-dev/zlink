// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const STANDARD_MSG_SIZES = [64, 256, 1024, 65536, 131072, 262144];
const STREAM_MSG_SIZES = [64, 256, 1024, 65536];
const DEFAULT_SINGLE_TRANSPORTS = ['tcp', 'tls', 'ws', 'wss', 'inproc', 'ipc'];
const DEFAULT_MULTI_TRANSPORTS = ['tcp', 'tls', 'ws', 'wss'];
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
        duration: defaults.duration,
        msgSizes: defaults.msgSizes,
        resultsDir: defaults.resultsDir,
        resultsTag: '',
        output: '',
        runs: 1,
        transports: defaults.transports || [],
        clients: defaults.clients,
        pinCpu: false,
        ioThreads: undefined,
        hwm: undefined,
        sendHwm: undefined,
        recvHwm: undefined,
        serverIoThreads: undefined,
        clientIoThreads: undefined,
        sendTimeoutMs: undefined,
        recvTimeoutMs: undefined,
        connectConcurrency: undefined,
        transportTransitionMs: undefined,
        patternTransitionMs: undefined,
        serverReadyTimeoutMs: undefined,
        connectReadyTimeoutMs: undefined,
        monitorHwm: undefined,
        serverShutdownTimeoutMs: undefined,
        serverBindPort: undefined,
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
        }
        else if (arg === '--duration') {
            options.duration = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--msg-size') {
            options.msgSizes = parseSizeList(argv[i + 1], defaults.msgSizes).slice(0, 1);
            options.msgSizesExplicit = true;
            i += 1;
        }
        else if (arg === '--msg-sizes') {
            options.msgSizes = parseSizeList(argv[i + 1], defaults.msgSizes);
            options.msgSizesExplicit = true;
            i += 1;
        }
        else if (arg === '--results-dir') {
            options.resultsDir = argv[i + 1];
            i += 1;
        }
        else if (arg === '--results-tag') {
            options.resultsTag = argv[i + 1];
            i += 1;
        }
        else if (arg === '--output') {
            options.output = argv[i + 1];
            i += 1;
        }
        else if (arg === '--runs') {
            options.runs = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--transports') {
            options.transports = parseStringList(argv[i + 1], defaults.transports || []);
            options.transportsExplicit = true;
            i += 1;
        }
        else if (arg === '--pin-cpu') {
            options.pinCpu = true;
        }
        else if (arg === '--io-threads') {
            options.ioThreads = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--hwm') {
            options.hwm = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--send-hwm') {
            options.sendHwm = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--recv-hwm') {
            options.recvHwm = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--server-io-threads') {
            options.serverIoThreads = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--client-io-threads') {
            options.clientIoThreads = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--sndtimeo' || arg === '--send-timeout-ms') {
            options.sendTimeoutMs = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--rcvtimeo' || arg === '--recv-timeout-ms') {
            options.recvTimeoutMs = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--connect-concurrency') {
            options.connectConcurrency = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--transport-transition-ms') {
            options.transportTransitionMs = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--pattern-transition-ms') {
            options.patternTransitionMs = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--server-ready-timeout-ms') {
            options.serverReadyTimeoutMs = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--connect-ready-timeout-ms') {
            options.connectReadyTimeoutMs = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--monitor-hwm') {
            options.monitorHwm = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--server-shutdown-timeout-ms') {
            options.serverShutdownTimeoutMs = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--server-bind-port') {
            options.serverBindPort = Number(argv[i + 1]);
            i += 1;
        }
        else if (arg === '--clients') {
            if (typeof defaults.clients === 'undefined') {
                throw new Error('--clients is not supported for this runner');
            }
            options.clients = Number(argv[i + 1]);
            options.clientsExplicit = true;
            i += 1;
        }
        else if (arg === '--build-dir'
            || arg === '--reuse-build'
            || arg === '--clean-build') {
            if (arg === '--build-dir' && argv[i + 1] && !argv[i + 1].startsWith('--')) {
                i += 1;
            }
        }
        else if (arg === '--help' || arg === '-h') {
            options.helpRequested = true;
        }
        else {
            throw new Error(`unsupported argument: ${arg}`);
        }
    }
    return options;
}
function resolveSinglePatternNames(pattern) {
    return pattern === 'ALL'
        ? ['PAIR', 'PUBSUB', 'DEALER_DEALER', 'DEALER_ROUTER', 'ROUTER_ROUTER', 'SPOT', 'SPOT_REQREP']
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
            'MULTI_SPOT_REQREP',
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
module.exports = {
    DEFAULT_MULTI_TRANSPORTS,
    DEFAULT_SINGLE_TRANSPORTS,
    STANDARD_MSG_SIZES,
    STREAM_MSG_SIZES,
    defaultMultiClients,
    defaultMultiMsgSizes,
    defaultSingleMsgSizes,
    parseCommonArgs,
    resolveMultiPatternNames,
    resolveSinglePatternNames
};
