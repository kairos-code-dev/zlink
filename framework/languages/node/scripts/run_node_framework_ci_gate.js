#!/usr/bin/env node
const path = require('node:path');

const e2eTestFiles = [
  'test/contract/channel-client.test.js',
  'test/contract/http-client.test.js',
  'test/contract/message-packet-name.test.js',
  'test/contract/stream-connector.test.js',
  'test/contract/stream-session-runtime.test.js',
  'test/browser/stream-connector-chromium.test.js'
];

process.env.ZLINK_NODE_RUNTIME_GATE_SKIP_TESTS = [
  ...(process.env.ZLINK_NODE_RUNTIME_GATE_SKIP_TESTS ?? '')
    .split(',')
    .map((value) => value.trim())
    .filter((value) => value.length > 0),
  ...e2eTestFiles
].join(',');

require(path.resolve(__dirname, 'run_node_runtime_gate.js'));
