import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { ZLinkLocationAutoConnectType, ZLinkLocationRole } from '@zlink-systems/framework';
import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';

export const sampleName = 'Bingo.Ts';

export async function runSample(ctx) {
  const redisKeyPrefix = `bingo:node:${process.pid}:`;
  const apiA = `tcp://127.0.0.1:${await ctx.port()}`;
  const apiB = `tcp://127.0.0.1:${await ctx.port()}`;
  const playA = await bingoPlayConfig(ctx, 'a', redisKeyPrefix);
  const playB = await bingoPlayConfig(ctx, 'b', redisKeyPrefix);
  const sessionA = await bingoSessionConfig(ctx, 'a', redisKeyPrefix);
  const sessionB = await bingoSessionConfig(ctx, 'b', redisKeyPrefix);
  const common = { redisEndpoint: ctx.redisEndpoint, redisKeyPrefix };
  const flowDir = path.join(ctx.logDir, 'flow');
  fs.mkdirSync(flowDir, { recursive: true });
  const apiAConfig = ctx.writeConfig('api-a', { ...common, apiEndpoint: apiA, logDir: flowDir });
  const apiBConfig = ctx.writeConfig('api-b', { ...common, apiEndpoint: apiB, logDir: flowDir });

  await ctx.start('api-b', 'dist/Server/Api/main.js', ['--config', apiBConfig]);
  await ctx.waitTcp(apiB);
  await ctx.start('api-a', 'dist/Server/Api/main.js', ['--config', apiAConfig]);
  await ctx.waitTcp(apiA);
  await ctx.start('play-b', 'dist/Server/Play/main.js', ['--config', playB.path]);
  await ctx.waitTcp(playB.sample.playSpotEndpoint);
  await ctx.start('play-a', 'dist/Server/Play/main.js', ['--config', playA.path]);
  await ctx.waitTcp(playA.sample.playSpotEndpoint);
  const replacement = await verifyPlaySlotHandoff(
    ctx,
    redisKeyPrefix,
    playA.sample.playSpotEndpoint
  );
  await ctx.start('session-b', 'dist/Server/Session/main.js', ['--config', sessionB.path]);
  await ctx.waitTcp(sessionB.sample.sessionEndpoint);
  await ctx.start('session-a', 'dist/Server/Session/main.js', ['--config', sessionA.path]);
  await ctx.waitTcp(sessionA.sample.sessionEndpoint);
  await ctx.waitLog('api-b', 'bingo routing allocation ready role=api group=bingo.api slot=1');
  await ctx.waitLog('api-a', 'bingo routing allocation ready role=api group=bingo.api slot=2');
  await ctx.waitLog('play-b', 'bingo routing allocation ready role=play group=bingo.play slot=1');
  await ctx.waitLog('play-replacement', 'bingo routing allocation ready role=play group=bingo.play slot=2');
  await ctx.waitLog('session-b', 'bingo routing allocation ready role=session group=bingo.session slot=1');
  await ctx.waitLog('session-a', 'bingo routing allocation ready role=session group=bingo.session slot=2');
  await ctx.waitLog(
    'play-replacement',
    `bingo-room-peer ConnectionReady remote=${sessionB.sample.sessionSpotEndpoint}`
  );
  await ctx.waitLog(
    'play-b',
    `bingo-room-peer ConnectionReady remote=${sessionA.sample.sessionSpotEndpoint}`
  );
  ctx.runNode(path.join(ctx.nodeRoot, 'e2e/location-readiness.js'), [
    '--redis-endpoint', ctx.redisEndpoint,
    '--key-prefix', `${redisKeyPrefix}location`,
    '--peer', 'client-server', 'bingo.api', 'router', apiA, apiB,
    '--peer', 'spot-mesh', 'bingo.room', 'spot', playB.sample.playSpotEndpoint, replacement.sample.playSpotEndpoint
  ]);
  ctx.runBrowser({
    timeoutMs: 90_000,
    config: {
      sessionAEndpoint: sessionB.sample.sessionEndpoint,
      sessionBEndpoint: sessionA.sample.sessionEndpoint
    },
    proxies: []
  });
  await ctx.waitLog('play-b', 'bingo-record fetched actor=player-1 wins=0 losses=0');
  await ctx.waitLog('play-b', 'bingo-record fetched actor=player-2 wins=0 losses=0');
  await ctx.waitLog('play-b', 'bingo-record reported actor=player-1 wins=1 losses=0');
  await ctx.waitLog('play-b', 'bingo-record reported actor=player-2 wins=0 losses=1');
  await ctx.waitLog('play-b', 'bingo-lifecycle room-leave actor=player-1');
  await ctx.waitLog('play-b', 'bingo-lifecycle room-leave actor=player-2');
  await ctx.waitLog('play-b', 'bingo-lifecycle entry-destroy-complete actor=player-1');
  await ctx.waitLog('play-replacement', 'bingo-lifecycle entry-destroy-complete actor=player-2');
  await ctx.waitLog('session-b', 'bingo-lifecycle session-disconnect actor=player-1 destroy=false');
  await ctx.waitLog('session-a', 'bingo-lifecycle session-disconnect actor=player-2 destroy=false');
  ctx.assertLogCount('play-b', 'bingo-lifecycle room-leave actor=player-1', 1);
  ctx.assertLogCount('play-b', 'bingo-lifecycle room-leave actor=player-2', 1);
  ctx.assertLogCount('play-b', 'bingo-lifecycle entry-destroy-complete actor=player-1', 1);
  ctx.assertLogCount('play-replacement', 'bingo-lifecycle entry-destroy-complete actor=player-2', 1);
  ctx.assertLogCount('play-b', 'bingo-lifecycle entry-leave actor=player-1', 1);
  ctx.assertLogCount('play-replacement', 'bingo-lifecycle entry-leave actor=player-2', 1);
  ctx.assertLogCount('play-replacement', 'bingo-lifecycle entry-leave actor=observer', 1);
  ctx.assertLogCount('play-replacement', 'bingo-record reported actor=observer', 0);

}

async function bingoPlayConfig(ctx, suffix, redisKeyPrefix) {
  const sample = {
    redisEndpoint: ctx.redisEndpoint,
    redisKeyPrefix,
    logDir: path.join(ctx.logDir, 'flow'),
    playEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    playSpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    playSpotPubSubEndpoint: `tcp://127.0.0.1:${await ctx.port()}`
  };
  return { sample, path: ctx.writeConfig(`play-${suffix}`, sample) };
}

async function bingoSessionConfig(ctx, suffix, redisKeyPrefix) {
  const sample = {
    redisEndpoint: ctx.redisEndpoint,
    redisKeyPrefix,
    sessionEndpoint: `ws://127.0.0.1:${await ctx.port()}`,
    sessionSpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    sessionSpotPubSubEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    logDir: path.join(ctx.logDir, 'flow')
  };
  return { sample, path: ctx.writeConfig(`session-${suffix}`, sample) };
}

async function verifyPlaySlotHandoff(ctx, redisKeyPrefix, oldRoomEndpoint) {
  const beforeHandoff = await allocationSnapshot(ctx, redisKeyPrefix, 'bingo.play');
  await waitForPlayPeer(ctx, redisKeyPrefix, oldRoomEndpoint, true);
  const replacement = await bingoPlayConfig(ctx, 'replacement', redisKeyPrefix);
  await ctx.start('play-replacement', 'dist/Server/Play/main.js', ['--config', replacement.path]);
  await delay(500);
  if (await canConnect(replacement.sample.playSpotEndpoint)) {
    throw new Error('Bingo replacement bound a socket before a routing-id slot was available.');
  }
  console.log('BINGO-RID-5 state=WaitingForSlot replacement=play-replacement');
  ctx.signal('play-a', 'SIGUSR2');
  await ctx.waitLog('play-a', 'bingo-drain result=drained');
  await waitForPlayPeer(ctx, redisKeyPrefix, oldRoomEndpoint, false);
  await ctx.stop('play-a', 'SIGTERM');
  await ctx.waitTcp(replacement.sample.playSpotEndpoint);
  await ctx.waitLog('play-replacement', 'bingo routing allocation ready role=play group=bingo.play slot=2');
  await ctx.waitLog(
    'play-b',
    `bingo-room-peer ConnectionReady remote=${replacement.sample.playSpotEndpoint}`
  );
  await ctx.waitLog('play-replacement', 'bingo-play-router ConnectionReady clients=2');

  const afterHandoff = await allocationSnapshot(ctx, redisKeyPrefix, 'bingo.play');
  const oldGeneration = generationAt(beforeHandoff, 2);
  const replacementGeneration = generationAt(afterHandoff, 2);
  if (replacementGeneration <= oldGeneration) {
    throw new Error(`Bingo replacement generation ${replacementGeneration} did not exceed ${oldGeneration}.`);
  }
  console.log(`BINGO-RID-5 replacement=play-replacement slot=2 generation=${replacementGeneration}`);
  return replacement;
}

async function waitForPlayPeer(ctx, redisKeyPrefix, endpoint, present) {
  const store = new ZLinkRedisLocationStore({
    url: `redis://${ctx.redisEndpoint}`,
    keyPrefix: `${redisKeyPrefix}location`
  });
  try {
    const deadline = Date.now() + 15_000;
    while (Date.now() < deadline) {
      const peers = await store.listPeers({
        autoConnectType: ZLinkLocationAutoConnectType.SpotMesh,
        meshName: 'bingo.room',
        role: ZLinkLocationRole.Spot
      });
      if (peers.some((peer) => peer.endpoint === endpoint) === present) return;
      await delay(100);
    }
  } finally {
    await store.dispose();
  }
  throw new Error(`Bingo room peer ${endpoint} did not become present=${present}.`);
}

async function allocationSnapshot(ctx, redisKeyPrefix, groupName) {
  const store = new ZLinkRedisLocationStore({
    url: `redis://${ctx.redisEndpoint}`,
    keyPrefix: `${redisKeyPrefix}location`
  });
  try {
    return await store.listRoutingIdSlots(groupName);
  } finally {
    await store.dispose();
  }
}

function generationAt(snapshot, slot) {
  const allocation = snapshot.allocations.find((item) => item.slot === slot);
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
