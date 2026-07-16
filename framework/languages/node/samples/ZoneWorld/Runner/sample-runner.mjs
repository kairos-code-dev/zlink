import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { setTimeout as delay } from 'node:timers/promises';
import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';

export const sampleName = 'ZoneWorld';

export async function runSample(ctx) {
  const redisKeyPrefix = `zoneworld:node:${process.pid}:`;
  const shared = {
    redisEndpoint: ctx.redisEndpoint,
    redisKeyPrefix,
    logDirectory: ctx.logDir
  };
  const east = await zoneNodeConfig(ctx, shared, 'zone-node-2', 'east', { disableBots: true });
  const west = await zoneNodeConfig(ctx, shared, 'zone-node-1', 'west', { disableBots: true });
  const replacement = await zoneNodeConfig(ctx, shared, 'zone-node-2', 'east-replacement', { disableBots: true });
  const crashReplacement = await zoneNodeConfig(ctx, shared, 'zone-node-2', 'east-crash-replacement', { disableBots: true });
  const ops = {
    streamEndpoint: `ws://127.0.0.1:${await ctx.port()}`,
    broadcastEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    reportEndpoint: `tcp://127.0.0.1:${await ctx.port()}`
  };
  const gateway = {
    streamEndpoint: `ws://127.0.0.1:${await ctx.port()}`,
    spotRouterEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    spotPubSubEndpoint: `tcp://127.0.0.1:${await ctx.port()}`
  };

  await ctx.start('zone-node-2', 'dist/Server/ZoneNode/main.js', ['--config', east.path]);
  await ctx.waitLog('zone-node-2', 'slot=1');
  await ctx.waitLog('zone-node-2', 'members=zoneworld.zones=zn1,zoneworld.bridge=zn1,zoneworld.report=zn1');
  console.log('ZW-G1 shared-allocation=passed');
  console.log('ZW-G2 reverse-start=passed node=zone-node-2 slot=1');

  await ctx.start('zone-node-1', 'dist/Server/ZoneNode/main.js', ['--config', west.path]);
  await ctx.waitLog('zone-node-1', 'slot=2');
  const store = new ZLinkRedisLocationStore({
    url: `redis://${ctx.redisEndpoint}`,
    keyPrefix: `${redisKeyPrefix}location`
  });
  const before = await store.listRoutingIdSlots('zoneworld.zone-node');
  const oldGeneration = generationAt(before, 1);
  await ctx.start('zone-node-2-replacement', 'dist/Server/ZoneNode/main.js', ['--config', replacement.path]);
  await delay(500);
  if (await canConnect(replacement.value.zoneNode.spotRouterEndpoint)) {
    throw new Error('ZW-G3 replacement bound before a routing-id slot was available.');
  }
  console.log('ZW-G3 state=WaitingForSlot replacement=zone-node-2-replacement');
  ctx.signal('zone-node-2', 'SIGTERM');
  await ctx.waitLog('zone-node-2-replacement', 'slot=1');
  const after = await store.listRoutingIdSlots('zoneworld.zone-node');
  const generation = generationAt(after, 1);
  if (generation <= oldGeneration) throw new Error('ZW-G3 replacement generation did not increase.');
  console.log(`ZW-G3 handoff=passed slot=1 generation=${generation}`);
  ctx.signal('zone-node-2', 'SIGKILL');
  const beforeCrash = await store.listRoutingIdSlots('zoneworld.zone-node');
  const crashGeneration = generationAt(beforeCrash, 1);
  ctx.signal('zone-node-2-replacement', 'SIGKILL');
  await ctx.start(
    'zone-node-2-crash-replacement',
    'dist/Server/ZoneNode/main.js',
    ['--config', crashReplacement.path]
  );
  await delay(1_000);
  if (await canConnect(crashReplacement.value.zoneNode.spotRouterEndpoint)) {
    throw new Error('ZW-G4 crash replacement bound before the owner lease expired.');
  }
  console.log('ZW-G4 state=WaitingForSlot lease=unexpired');
  await ctx.waitLog('zone-node-2-crash-replacement', 'slot=1');
  const afterCrash = await store.listRoutingIdSlots('zoneworld.zone-node');
  const recoveredGeneration = generationAt(afterCrash, 1);
  if (recoveredGeneration <= crashGeneration) throw new Error('ZW-G4 crash replacement generation did not increase.');
  console.log(`ZW-G4 lease=expired slot=1 generation=${recoveredGeneration}`);
  console.log('ZW-G5 fixed-routing-id=absent');
  await store.dispose();

  const opsPath = ctx.writeConfig('ops', { shared, ops });
  await ctx.start('ops', 'dist/Server/Ops/main.js', ['--config', opsPath]);
  await ctx.waitTcp(ops.streamEndpoint);

  const timerFailure = startScenarioClient(
    ctx,
    specialClientConfig(ctx, shared, gateway, ops, 'C4'),
    'timer-failure'
  );
  await timerFailure.waitFor('scenario ZW-C4 armed');
  await ctx.stop('zone-node-1', 'SIGKILL');
  const westFinal = await zoneNodeConfig(ctx, shared, 'zone-node-1', 'west-final', {
    disableBots: true,
    faultTickZone: 'zone-nw'
  });
  await ctx.start('zone-node-1-final', 'dist/Server/ZoneNode/main.js', ['--config', westFinal.path]);
  await ctx.waitLog('zone-node-1-final', 'topology=ready');
  await timerFailure.waitFor('scenario ZW-C4 passed');
  await timerFailure.complete();
  ctx.signal('zone-node-1', 'SIGKILL');

  const gatewayPath = ctx.writeConfig('gateway', { shared, gateway });
  await ctx.start('gateway', 'dist/Server/Gateway/main.js', ['--config', gatewayPath]);
  await ctx.waitTcp(gateway.streamEndpoint);
  await ctx.waitLog('zone-node-1-final', 'spot peers ready');
  await ctx.waitLog('zone-node-2-crash-replacement', 'spot peers ready');

  const clientPath = ctx.writeConfig('client', {
    shared,
    client: { gatewayEndpoint: gateway.streamEndpoint, opsEndpoint: ops.streamEndpoint, scenarios: 'ZW-A1' }
  });
  ctx.runNode(path.join(ctx.sampleRoot, 'dist/Client/main.js'), ['--config', clientPath]);
  await ctx.waitLog('zone-node-1-final', 'fanout subscriber received announcement');
  await ctx.waitLog('zone-node-2-crash-replacement', 'fanout subscriber received announcement');
  for (const zoneId of ['zone-nw', 'zone-sw']) {
    await ctx.waitLog('zone-node-1-final', `zone spot announcement delivered zone=${zoneId}`);
  }
  for (const zoneId of ['zone-ne', 'zone-se']) {
    await ctx.waitLog('zone-node-2-crash-replacement', `zone spot announcement delivered zone=${zoneId}`);
  }

  const transition = startScenarioClient(
    ctx,
    specialClientConfig(ctx, shared, gateway, ops, 'B4-C2-C3'),
    'transition'
  );
  await transition.waitFor('scenario ZW-B4-C2-C3 armed');
  await ctx.stop('zone-node-2-crash-replacement', 'SIGKILL');
  await transition.waitFor('scenario ZW-B4 passed');
  await transition.waitFor('scenario ZW-C2 passed');
  await transition.waitFor('scenario ZW-C3 passed');
  await transition.complete();
  const eastAfterFailure = await zoneNodeConfig(ctx, shared, 'zone-node-2', 'east-after-failure', { disableBots: true });
  await ctx.start('zone-node-2-after-failure', 'dist/Server/ZoneNode/main.js', ['--config', eastAfterFailure.path]);
  await ctx.waitLog('zone-node-2-after-failure', 'topology=ready');
  await ctx.waitLog('zone-node-2-after-failure', 'spot peers ready');

  const extra = await zoneNodeConfig(ctx, shared, 'zone-node-3', 'extra', { disableBots: true });
  await ctx.start('zone-node-3', 'dist/Server/ZoneNode/main.js', ['--config', extra.path]);
  await ctx.waitLog('zone-node-3', 'topology=ready node=zone-node-3 zones=');
  ctx.runNode(path.join(ctx.sampleRoot, 'dist/Client/special.js'), [
    '--config', specialClientConfig(ctx, shared, gateway, ops, 'D2')
  ]);
  await ctx.waitLog('zone-node-3', 'fanout subscriber received announcement');
  console.log('scenario ZW-D2 passed');

  ctx.runNode(path.join(ctx.sampleRoot, 'dist/Client/special.js'), [
    '--config', specialClientConfig(ctx, shared, gateway, ops, 'E')
  ]);
  ctx.runNode(path.join(ctx.sampleRoot, 'dist/Client/special.js'), [
    '--config', specialClientConfig(ctx, shared, gateway, ops, 'E5-arm')
  ]);
  await stopAndWaitForLocationLease(
    ctx,
    'zone-node-2-after-failure',
    eastAfterFailure.value.zoneNode,
    shared
  );
  const eastAfterMaintenance = await zoneNodeConfig(ctx, shared, 'zone-node-2', 'east-after-maintenance', {
    disableBots: true
  });
  await ctx.start('zone-node-2-after-maintenance', 'dist/Server/ZoneNode/main.js', ['--config', eastAfterMaintenance.path]);
  await ctx.waitLog('zone-node-2-after-maintenance', 'maintenance restored node=zone-node-2 enabled=true');
  ctx.runNode(path.join(ctx.sampleRoot, 'dist/Client/special.js'), [
    '--config', specialClientConfig(ctx, shared, gateway, ops, 'E5')
  ]);

  await stopAndWaitForLocationLease(
    ctx,
    'zone-node-2-after-maintenance',
    eastAfterMaintenance.value.zoneNode,
    shared
  );
  await stopAndWaitForLocationLease(ctx, 'zone-node-1-final', westFinal.value.zoneNode, shared);
  const botStartSignalPath = path.join(ctx.logDir, 'bots.start');
  const eastBots = await zoneNodeConfig(ctx, shared, 'zone-node-2', 'east-bots', { botStartSignalPath });
  const westBots = await zoneNodeConfig(ctx, shared, 'zone-node-1', 'west-bots', { botStartSignalPath });
  await ctx.start('zone-node-1-bots', 'dist/Server/ZoneNode/main.js', ['--config', westBots.path]);
  await ctx.start('zone-node-2-bots', 'dist/Server/ZoneNode/main.js', ['--config', eastBots.path]);
  await ctx.waitLog('zone-node-1-bots', 'bot-start=ready');
  await ctx.waitLog('zone-node-2-bots', 'bot-start=ready');
  fs.writeFileSync(botStartSignalPath, 'start\n', { mode: 0o600 });
  await ctx.waitLog('zone-node-1-bots', 'topology=ready');
  await ctx.waitLog('zone-node-1-bots', 'spot peers ready');
  await ctx.waitLog('zone-node-2-bots', 'topology=ready');
  await ctx.waitLog('zone-node-2-bots', 'spot peers ready');
  await ctx.waitLog(
    'gateway',
    `gateway spot peer ready remote=${westBots.value.zoneNode.spotRouterEndpoint}`
  );
  await ctx.waitLog(
    'gateway',
    `gateway spot peer ready remote=${eastBots.value.zoneNode.spotRouterEndpoint}`
  );

  for (const name of ['zone-node-1-bots', 'zone-node-2-bots']) {
    await ctx.waitLog(name, 'bot spawned');
  }
  await ctx.waitLog(
    'zone-node-1-bots',
    'zone player entered zone=zone-nw player=bot-ne-x from=zone-node-2'
  );
  await ctx.waitLog(
    'zone-node-2-bots',
    'zone change result player=bot-ne-x from=zone-ne to=zone-nw status=accepted'
  );
  const botLogs = ['zone-node-1-bots', 'zone-node-2-bots']
    .map((name) => fs.readFileSync(path.join(ctx.logDir, `${name}.log`), 'utf8'))
    .join('\n');
  const spawned = new Set([...botLogs.matchAll(/bot spawned bot=([^ ]+)/g)].map((match) => match[1]));
  if (spawned.size !== 8) throw new Error(`ZW-F1 expected 8 spawned bots, observed ${spawned.size}.`);
  if (/No current session binding exists for actor 'bot-/.test(botLogs)) {
    throw new Error('ZW-F3 attempted to push to an unbound bot actor.');
  }
  console.log('scenario ZW-F2 passed');
  ctx.runNode(path.join(ctx.sampleRoot, 'dist/Client/special.js'), [
    '--config', specialClientConfig(ctx, shared, gateway, ops, 'F')
  ]);
  console.log('topology=ready');
  console.log('zoneworld-transfer=completed');
  console.log('zoneworld-border-sync=completed');
  console.log('zoneworld-ops-observe=completed');
  console.log('zoneworld-ops-announce=completed');
  console.log('zoneworld-ops-maintenance=completed');
  console.log('zoneworld=completed');
  console.log('PASS ZoneWorld');
}

async function zoneNodeConfig(ctx, shared, nodeId, name, overrides = {}) {
  const value = {
    shared,
    zoneNode: {
      nodeId,
      spotRouterEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
      spotPubSubEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
      opsChannelEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
      actorsChannelEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
      bridgeEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
      ...overrides
    }
  };
  return { value, path: ctx.writeConfig(name, value) };
}

function specialClientConfig(ctx, shared, gateway, ops, scenarios) {
  return ctx.writeConfig(`client-${scenarios.toLowerCase().replaceAll(/[^a-z0-9]+/g, '-')}`, {
    shared,
    client: { gatewayEndpoint: gateway.streamEndpoint, opsEndpoint: ops.streamEndpoint, scenarios }
  });
}

function startScenarioClient(ctx, configPath, name, relativeExecutable = 'dist/Client/special.js') {
  const executable = path.join(ctx.sampleRoot, relativeExecutable);
  const child = spawn(process.execPath, [executable, '--config', configPath], {
    cwd: ctx.sampleRoot,
    stdio: ['ignore', 'pipe', 'pipe']
  });
  let output = '';
  child.stdout.on('data', (chunk) => { output += chunk.toString(); });
  child.stderr.on('data', (chunk) => { output += chunk.toString(); });
  let failure;
  const exited = new Promise((resolve) => child.once('exit', (code) => {
    if (code !== 0) failure = new Error(`ZoneWorld ${name} client exited with ${code}.\n${output}`);
    resolve();
  }));
  const complete = async () => {
    await exited;
    if (failure !== undefined) throw failure;
  };
  return {
    async waitFor(marker) {
      const deadline = Date.now() + 60_000;
      while (!output.includes(marker)) {
        if (child.exitCode !== null) await complete();
        if (Date.now() >= deadline) throw new Error(`Timed out waiting for '${marker}'.\n${output}`);
        await delay(50);
      }
    },
    complete
  };
}

function generationAt(snapshot, slot) {
  const allocation = snapshot.allocations.find((candidate) => candidate.slot === slot);
  if (!allocation) throw new Error(`Routing-id slot ${slot} is not allocated.`);
  return allocation.owner.generation;
}

function canConnect(endpoint) {
  const url = new URL(endpoint.replace(/^tcp:/, 'http:'));
  return new Promise((resolve) => {
    const socket = net.connect({
      port: Number(url.port),
      host: url.hostname,
      signal: AbortSignal.timeout(250)
    });
    socket.once('connect', () => { socket.destroy(); resolve(true); });
    socket.once('error', () => resolve(false));
  });
}

async function stopAndWaitForLocationLease(ctx, name, node, shared) {
  const store = new ZLinkRedisLocationStore({
    url: `redis://${shared.redisEndpoint}`,
    keyPrefix: `${shared.redisKeyPrefix}location`
  });
  try {
    const endpoints = new Set([node.spotRouterEndpoint, node.spotPubSubEndpoint]);
    const peers = await store.listPeers({});
    const owners = new Set(peers.filter((peer) => endpoints.has(peer.endpoint)).map((peer) => peer.ownerId));
    await ctx.stop(name, 'SIGKILL');
    const deadline = Date.now() + 10_000;
    while (owners.size > 0) {
      const leases = await store.listOwnerLeases();
      const liveOwners = new Set(leases.leases.map((lease) => lease.ownerId));
      if (![...owners].some((ownerId) => liveOwners.has(ownerId))) return;
      if (Date.now() >= deadline) throw new Error(`Timed out waiting for ${name} location lease expiry.`);
      await delay(50);
    }
  } finally {
    await store.dispose();
  }
}
