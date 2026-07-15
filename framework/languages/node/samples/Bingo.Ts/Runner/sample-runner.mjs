import fs from 'node:fs';
import path from 'node:path';

export const sampleName = 'Bingo.Ts';

export async function runSample(ctx) {
  const redisKeyPrefix = `bingo:node:${process.pid}:`;
  const apiA = `tcp://127.0.0.1:${await ctx.port()}`;
  const apiB = `tcp://127.0.0.1:${await ctx.port()}`;
  const playA = await bingoPlayConfig(ctx, 'a', redisKeyPrefix);
  const playB = await bingoPlayConfig(ctx, 'b', redisKeyPrefix);
  const sessionA = await bingoSessionConfig(ctx, 'a', playA.sample.playSpotNodeRid, redisKeyPrefix);
  const sessionB = await bingoSessionConfig(ctx, 'b', playB.sample.playSpotNodeRid, redisKeyPrefix);
  const common = { redisEndpoint: ctx.redisEndpoint, redisKeyPrefix };
  const flowDir = path.join(ctx.logDir, 'flow');
  fs.mkdirSync(flowDir, { recursive: true });
  const apiAConfig = ctx.writeConfig('api-a', { ...common, apiEndpoint: apiA, logDir: flowDir });
  const apiBConfig = ctx.writeConfig('api-b', { ...common, apiEndpoint: apiB, logDir: flowDir });

  await ctx.start('api-a', 'dist/Server/Api/main.js', ['--config', apiAConfig]);
  await ctx.waitTcp(apiA);
  await ctx.start('api-b', 'dist/Server/Api/main.js', ['--config', apiBConfig]);
  await ctx.waitTcp(apiB);
  await ctx.start('play-a', 'dist/Server/Play/main.js', ['--config', playA.path]);
  await ctx.waitTcp(playA.sample.playSpotEndpoint);
  await ctx.start('play-b', 'dist/Server/Play/main.js', ['--config', playB.path]);
  await ctx.waitTcp(playB.sample.playSpotEndpoint);
  await ctx.start('session-a', 'dist/Server/Session/main.js', ['--config', sessionA.path]);
  await ctx.waitTcp(sessionA.sample.sessionEndpoint);
  await ctx.start('session-b', 'dist/Server/Session/main.js', ['--config', sessionB.path]);
  await ctx.waitTcp(sessionB.sample.sessionEndpoint);
  ctx.runNode(path.join(ctx.nodeRoot, 'e2e/location-readiness.js'), [
    '--redis-endpoint', ctx.redisEndpoint,
    '--key-prefix', `${redisKeyPrefix}location`,
    '--peer', 'client-server', 'bingo.api', 'router', apiA, apiB,
    '--peer', 'spot-mesh', 'bingo.room', 'spot', playA.sample.playSpotEndpoint, playB.sample.playSpotEndpoint
  ]);
  ctx.runBrowser({
    timeoutMs: 90_000,
    config: {
      sessionAEndpoint: sessionA.sample.sessionEndpoint,
      sessionBEndpoint: sessionB.sample.sessionEndpoint
    },
    proxies: []
  });
  await ctx.waitLog('play-a', 'bingo-record fetched actor=player-1 wins=0 losses=0');
  await ctx.waitLog('play-a', 'bingo-record fetched actor=player-2 wins=0 losses=0');
  await ctx.waitLog('play-a', 'bingo-record reported actor=player-1 wins=1 losses=0');
  await ctx.waitLog('play-a', 'bingo-record reported actor=player-2 wins=0 losses=1');
  await ctx.waitLog('play-a', 'bingo-lifecycle room-leave actor=player-1');
  await ctx.waitLog('play-a', 'bingo-lifecycle room-leave actor=player-2');
  await ctx.waitLog('play-a', 'bingo-lifecycle entry-destroy-complete actor=player-1');
  await ctx.waitLog('play-b', 'bingo-lifecycle entry-destroy-complete actor=player-2');
  await ctx.waitLog('session-a', 'bingo-lifecycle session-disconnect actor=player-1 destroy=false');
  await ctx.waitLog('session-b', 'bingo-lifecycle session-disconnect actor=player-2 destroy=false');
  ctx.assertLogCount('play-a', 'bingo-lifecycle room-leave actor=player-1', 1);
  ctx.assertLogCount('play-a', 'bingo-lifecycle room-leave actor=player-2', 1);
  ctx.assertLogCount('play-a', 'bingo-lifecycle entry-destroy-complete actor=player-1', 1);
  ctx.assertLogCount('play-b', 'bingo-lifecycle entry-destroy-complete actor=player-2', 1);
  ctx.assertLogCount('play-a', 'bingo-lifecycle entry-leave actor=player-1', 1);
  ctx.assertLogCount('play-b', 'bingo-lifecycle entry-leave actor=player-2', 1);
  ctx.assertLogCount('play-b', 'bingo-lifecycle entry-leave actor=observer', 1);
  ctx.assertLogCount('play-b', 'bingo-record reported actor=observer', 0);
}

async function bingoPlayConfig(ctx, suffix, redisKeyPrefix) {
  const sample = {
    redisEndpoint: ctx.redisEndpoint,
    redisKeyPrefix,
    logDir: path.join(ctx.logDir, 'flow'),
    playEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    playRouteEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    playSpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    playSpotPubSubEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    playSpotNodeRid: `bingo-play-node-${suffix}`
  };
  return { sample, path: ctx.writeConfig(`play-${suffix}`, sample) };
}

async function bingoSessionConfig(ctx, suffix, preferredPlayNodeRid, redisKeyPrefix) {
  const sample = {
    redisEndpoint: ctx.redisEndpoint,
    redisKeyPrefix,
    sessionEndpoint: `ws://127.0.0.1:${await ctx.port()}`,
    sessionRouteEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    sessionSpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    sessionSpotNodeRid: `bingo-session-node-${suffix}`,
    preferredPlayNodeRid,
    logDir: path.join(ctx.logDir, 'flow')
  };
  return { sample, path: ctx.writeConfig(`session-${suffix}`, sample) };
}
