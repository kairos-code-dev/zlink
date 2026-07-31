import fs from 'node:fs';
import path from 'node:path';

export const sampleName = 'Bingo.Ts';

export async function runSample(ctx) {
  const redisKeyPrefix = `bingo:node:${process.pid}:`;
  const apiA = `tcp://127.0.0.1:${await ctx.port()}`;
  const apiB = `tcp://127.0.0.1:${await ctx.port()}`;
  const apiMatchmakingA = `tcp://127.0.0.1:${await ctx.port()}`;
  const apiMatchmakingB = `tcp://127.0.0.1:${await ctx.port()}`;
  const matchmaking = `tcp://127.0.0.1:${await ctx.port()}`;
  const playA = await bingoPlayConfig(ctx, 'a', redisKeyPrefix);
  const playB = await bingoPlayConfig(ctx, 'b', redisKeyPrefix);
  const sessionA = await bingoSessionConfig(ctx, 'a', redisKeyPrefix);
  const sessionB = await bingoSessionConfig(ctx, 'b', redisKeyPrefix);
  const common = { redisEndpoint: ctx.redisEndpoint, redisKeyPrefix };
  const flowDir = path.join(ctx.logDir, 'flow');
  fs.mkdirSync(flowDir, { recursive: true });
  const apiAConfig = ctx.writeConfig('api-a', {
    ...common, apiEndpoint: apiA, apiMatchmakingEndpoint: apiMatchmakingA, logDir: flowDir
  });
  const apiBConfig = ctx.writeConfig('api-b', {
    ...common, apiEndpoint: apiB, apiMatchmakingEndpoint: apiMatchmakingB, logDir: flowDir
  });
  const matchmakingConfig = ctx.writeConfig('matchmaking', {
    ...common, matchmakingEndpoint: matchmaking, logDir: flowDir
  });

  await ctx.start('matchmaking', 'dist/Server/Matchmaking/main.js', ['--config', matchmakingConfig]);
  await ctx.waitTcp(matchmaking);
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
    playA.sample.playSpotEndpoint,
    playB.sample.playSpotEndpoint
  );
  await ctx.start('session-b', 'dist/Server/Session/main.js', ['--config', sessionB.path]);
  await ctx.waitTcp(sessionB.sample.sessionEndpoint);
  await ctx.start('session-a', 'dist/Server/Session/main.js', ['--config', sessionA.path]);
  await ctx.waitTcp(sessionA.sample.sessionEndpoint);
  await ctx.waitLog(
    'play-replacement',
    `bingo-room-peer ConnectionReady remote=${sessionB.sample.sessionSpotEndpoint}`
  );
  await ctx.waitLog(
    'play-b',
    `bingo-room-peer ConnectionReady remote=${sessionA.sample.sessionSpotEndpoint}`
  );
  await ctx.waitLog('session-b', `bingo-room-peer ConnectionReady remote=${apiA}`);
  await ctx.waitLog('session-a', `bingo-room-peer ConnectionReady remote=${apiA}`);
  await ctx.waitLog('api-b', `bingo-room-peer ConnectionReady remote=${playB.sample.playSpotEndpoint}`);
  await ctx.waitLog('api-a', `bingo-room-peer ConnectionReady remote=${replacement.sample.playSpotEndpoint}`);
  await ctx.waitLog('play-b', `bingo-room-peer ConnectionReady remote=${apiA}`);
  await ctx.waitLog('play-replacement', `bingo-room-peer ConnectionReady remote=${apiA}`);
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

async function verifyPlaySlotHandoff(ctx, redisKeyPrefix, oldRoomEndpoint, survivingRoomEndpoint) {
  const replacement = await bingoPlayConfig(ctx, 'replacement', redisKeyPrefix);
  await ctx.start('play-replacement', 'dist/Server/Play/main.js', ['--config', replacement.path]);
  await ctx.waitTcp(replacement.sample.playSpotEndpoint);
  ctx.signal('play-a', 'SIGUSR2');
  await ctx.waitLog('play-a', 'bingo-drain result=drained');
  await ctx.stop('play-a', 'SIGTERM');
  await ctx.waitAnyLog([
    {
      name: 'play-replacement',
      marker: `bingo-room-peer ConnectionReady remote=${survivingRoomEndpoint}`
    },
    {
      name: 'play-b',
      marker: `bingo-room-peer ConnectionReady remote=${replacement.sample.playSpotEndpoint}`
    }
  ]);
  console.log(`BINGO-ROLLING replacement=play-replacement retired=${oldRoomEndpoint}`);
  return replacement;
}
