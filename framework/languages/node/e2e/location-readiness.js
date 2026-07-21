#!/usr/bin/env node
'use strict';

const framework = require('@zlink-systems/framework');
const { ZLinkRedisLocationStore } = require('@zlink-systems/framework-locations-redis');

const pubEndpointMetadataKey = 'pub-endpoint';

const autoConnectTypes = new Map([
  ['route-mesh', framework.ZLinkLocationAutoConnectType.RouteMesh],
  ['fanout', framework.ZLinkLocationAutoConnectType.Fanout]
]);

const roles = new Map([
  ['router', framework.ZLinkLocationRole.Router],
  ['pub', framework.ZLinkLocationRole.Pub],
  ['sub', framework.ZLinkLocationRole.Sub]
]);

function parseArgs(argv) {
  const options = {
    timeoutMs: 3000,
    intervalMs: 100,
    peers: []
  };
  for (let i = 0; i < argv.length;) {
    const arg = argv[i++];
    switch (arg) {
      case '--redis-endpoint':
        options.redisEndpoint = requireValue(argv, i++, arg);
        break;
      case '--key-prefix':
        options.keyPrefix = requireValue(argv, i++, arg);
        break;
      case '--timeout-ms':
        options.timeoutMs = Number(requireValue(argv, i++, arg));
        break;
      case '--interval-ms':
        options.intervalMs = Number(requireValue(argv, i++, arg));
        break;
      case '--peer': {
        const autoConnectType = parseMapValue(autoConnectTypes, requireValue(argv, i++, arg), 'auto-connect type');
        const meshName = requireValue(argv, i++, arg);
        const role = parseMapValue(roles, requireValue(argv, i++, arg), 'location role');
        const endpoints = [];
        while (i < argv.length && !argv[i].startsWith('--')) {
          endpoints.push(argv[i++]);
        }
        if (endpoints.length === 0) {
          throw new Error('--peer requires at least one endpoint.');
        }
        options.peers.push({ autoConnectType, meshName, role, endpoints });
        break;
      }
      default:
        throw new Error(`Unknown argument '${arg}'.`);
    }
  }
  if (options.redisEndpoint === undefined) {
    throw new Error('--redis-endpoint is required.');
  }
  if (options.keyPrefix === undefined) {
    throw new Error('--key-prefix is required.');
  }
  if (options.peers.length === 0) {
    throw new Error('At least one --peer entry is required.');
  }
  return options;
}

function requireValue(argv, index, flag) {
  const value = argv[index];
  if (value === undefined || value.startsWith('--')) {
    throw new Error(`${flag} requires a value.`);
  }
  return value;
}

function parseMapValue(map, text, label) {
  const value = map.get(text);
  if (value === undefined) {
    throw new Error(`Unknown ${label} '${text}'.`);
  }
  return value;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const store = new ZLinkRedisLocationStore({
    url: `redis://${options.redisEndpoint}`,
    keyPrefix: options.keyPrefix
  });
  try {
    await waitReady(store, options);
  } finally {
    await store.dispose();
  }
}

async function waitReady(store, options) {
  const deadline = Date.now() + options.timeoutMs;
  let lastState = { missing: [], rows: [], leases: [] };
  while (Date.now() < deadline) {
    lastState = await readinessState(store, options.peers);
    if (lastState.missing.length === 0) {
      return;
    }
    await delay(options.intervalMs);
  }
  console.error('Timed out waiting for Redis location topology readiness.');
  console.error(JSON.stringify(lastState, (_key, value) =>
    typeof value === 'bigint' ? value.toString() : value, 2));
  process.exitCode = 1;
}

async function readinessState(store, expectedPeers) {
  const leaseSnapshot = await store.listOwnerLeases();
  const liveOwnerIds = new Set(
    leaseSnapshot.leases
      .filter((lease) => lease.leaseExpiresAt.getTime() > leaseSnapshot.storeNow.getTime())
      .map((lease) => lease.ownerId)
  );
  const missing = [];
  const rows = [];
  for (const expected of expectedPeers) {
    const currentRows = await store.listPeers({
      autoConnectType: expected.autoConnectType,
      meshName: expected.meshName,
      role: expected.role
    });
    rows.push(...currentRows);
    const liveRows = currentRows.filter((row) => liveOwnerIds.has(row.ownerId));
    for (const endpoint of expected.endpoints) {
      if (!liveRows.some((row) => row.endpoint === endpoint || row.metadata?.[pubEndpointMetadataKey] === endpoint)) {
        missing.push(`${expected.meshName}@${endpoint}`);
      }
    }
  }
  return { missing, rows, leases: leaseSnapshot.leases };
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
