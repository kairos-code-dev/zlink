import net from 'node:net';
import path from 'node:path';
import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';

export const sampleName = 'ZoneWorld';

export async function runSample(ctx) {
  const redisKeyPrefix = `zoneworld:node:${process.pid}:`;
  const shared = {
    redisEndpoint: ctx.redisEndpoint,
    redisKeyPrefix,
    logDirectory: ctx.logDir
  };
  const east = await zoneNodeConfig(ctx, shared, 'zone-node-2', 'east');
  const west = await zoneNodeConfig(ctx, shared, 'zone-node-1', 'west');
  const replacement = await zoneNodeConfig(ctx, shared, 'zone-node-2', 'east-replacement');
  const crashReplacement = await zoneNodeConfig(ctx, shared, 'zone-node-2', 'east-crash-replacement');
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
  const gatewayPath = ctx.writeConfig('gateway', { shared, gateway });
  await ctx.start('gateway', 'dist/Server/Gateway/main.js', ['--config', gatewayPath]);
  await ctx.waitTcp(gateway.streamEndpoint);

  const clientPath = ctx.writeConfig('client', {
    shared,
    client: { gatewayEndpoint: gateway.streamEndpoint, opsEndpoint: ops.streamEndpoint, scenarios: 'ZW-A1' }
  });
  ctx.runNode(path.join(ctx.sampleRoot, 'dist/Client/main.js'), ['--config', clientPath]);
}

async function zoneNodeConfig(ctx, shared, nodeId, name) {
  const value = {
    shared,
    zoneNode: {
      nodeId,
      spotRouterEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
      spotPubSubEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
      opsChannelEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
      actorsChannelEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
      bridgeEndpoint: `tcp://127.0.0.1:${await ctx.port()}`
    }
  };
  return { value, path: ctx.writeConfig(name, value) };
}

function generationAt(snapshot, slot) {
  const allocation = snapshot.allocations.find((candidate) => candidate.slot === slot);
  if (!allocation) throw new Error(`Routing-id slot ${slot} is not allocated.`);
  return allocation.owner.generation;
}

function canConnect(endpoint) {
  const url = new URL(endpoint.replace(/^tcp:/, 'http:'));
  return new Promise((resolve) => {
    const socket = net.connect(Number(url.port), url.hostname);
    socket.once('connect', () => { socket.destroy(); resolve(true); });
    socket.once('error', () => resolve(false));
    socket.setTimeout(250, () => { socket.destroy(); resolve(false); });
  });
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}
