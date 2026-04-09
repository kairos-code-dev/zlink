// SPDX-License-Identifier: MPL-2.0

'use strict';

const STANDARD_MSG_SIZES = [64, 256, 1024, 65536, 131072, 262144];
const STREAM_MSG_SIZES = [64, 256, 1024, 65536];
const DEFAULT_SINGLE_TRANSPORTS = ['inproc'];
const DEFAULT_MULTI_TRANSPORTS = ['tcp'];

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
    warmup: defaults.warmup,
    msgSizes: defaults.msgSizes,
    resultsDir: defaults.resultsDir,
    resultsTag: '',
    output: '',
    runs: 1,
    transports: defaults.transports || [],
    clients: defaults.clients,
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
    } else if (arg === '--duration') {
      options.duration = Number(argv[i + 1]);
      i += 1;
    } else if (arg === '--warmup') {
      options.warmup = Number(argv[i + 1]);
      i += 1;
    } else if (arg === '--msg-sizes') {
      options.msgSizes = parseSizeList(argv[i + 1], defaults.msgSizes);
      options.msgSizesExplicit = true;
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
    } else if (arg === '--runs') {
      options.runs = Number(argv[i + 1]);
      i += 1;
    } else if (arg === '--transports') {
      options.transports = parseStringList(argv[i + 1], defaults.transports || []);
      options.transportsExplicit = true;
      i += 1;
    } else if (arg === '--clients') {
      if (typeof defaults.clients === 'undefined') {
        throw new Error('--clients is not supported for this runner');
      }
      options.clients = Number(argv[i + 1]);
      options.clientsExplicit = true;
      i += 1;
    } else if (
      arg === '--build-dir'
      || arg === '--reuse-build'
      || arg === '--clean-build'
    ) {
      if (arg.startsWith('--') && argv[i + 1] && !argv[i + 1].startsWith('--')) {
        i += 1;
      }
    } else if (arg === '--help' || arg === '-h') {
      options.helpRequested = true;
    } else {
      throw new Error(`unsupported argument: ${arg}`);
    }
  }

  return options;
}

function resolveSinglePatternNames(pattern) {
  return pattern === 'ALL'
    ? ['PAIR', 'PUBSUB', 'DEALER_DEALER', 'DEALER_ROUTER', 'ROUTER_ROUTER', 'SPOT']
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
  return 8;
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
