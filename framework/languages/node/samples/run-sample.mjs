import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const samplesRoot = path.dirname(fileURLToPath(import.meta.url));
const nodeRoot = path.dirname(samplesRoot);
const sampleName = process.argv[2];
if (!sampleName) {
  throw new Error('Usage: node samples/run-sample.mjs <Sample.Ts>');
}
const runnerOptions = parseRunnerOptions(process.argv.slice(3));

const sampleRoot = path.join(samplesRoot, sampleName);
if (!fs.existsSync(path.join(sampleRoot, 'package.json'))) {
  throw new Error(`Unknown Node sample '${sampleName}'.`);
}

const runDir = fs.mkdtempSync(path.join(os.tmpdir(), `zlink-${sampleName.toLowerCase()}-`));
const logDir = path.join(runDir, 'logs');
const workDir = path.join(runDir, 'work');
fs.mkdirSync(logDir, { recursive: true });
fs.mkdirSync(workDir, { recursive: true });

const children = [];
const reservedPorts = new Set();
let redisContainer;
let failed = false;
let cleaning = false;

async function main() {
  try {
    run('npm', ['run', 'build'], { cwd: sampleRoot });
    const redisEndpoint = await startRedis();
    const context = createContext(redisEndpoint);
    const runSample = sampleDefinitions[sampleName];
    if (!runSample) throw new Error(`Unknown Node sample '${sampleName}'.`);
    await runSample(context);
  } catch (error) {
    failed = true;
    printLogs();
    throw error;
  } finally {
    await cleanup();
    if (runnerOptions.keepRunDir || failed) {
      console.log(`runDir=${runDir}`);
    } else {
      fs.rmSync(runDir, { recursive: true, force: true });
    }
  }
}

function createContext(redisEndpoint) {
  const env = { ...process.env };
  return {
    env,
    logDir,
    nodeRoot,
    redisEndpoint,
    runDir,
    sampleRoot,
    workDir,
    port: reserveBrowserSafePort,
    writeConfig(name, sample) {
      const target = path.join(runDir, `${name}.json`);
      fs.writeFileSync(target, `${JSON.stringify({ sample }, null, 2)}\n`);
      return target;
    },
    async start(name, entry, args = [], extraEnv = {}) {
      const child = startNode(name, path.join(sampleRoot, entry), args, { ...env, ...extraEnv });
      children.push(child);
      return child;
    },
    waitTcp,
    waitHttp,
    waitLog,
    assertLogCount,
    runNode(entry, args = [], extraEnv = {}) {
      run(process.execPath, [entry, ...args], { cwd: sampleRoot, env: { ...env, ...extraEnv } });
    },
    runBrowser(definition, entryName) {
      const configPath = path.join(runDir, 'browser-runner.json');
      fs.writeFileSync(configPath, `${JSON.stringify(definition, null, 2)}\n`);
      const args = [
        path.join(nodeRoot, 'scripts/browser-e2e/run-sample.mjs'),
        sampleName,
        '--config',
        configPath
      ];
      if (entryName) args.push('--entry', entryName);
      run(process.execPath, args, { cwd: sampleRoot, env });
    }
  };
}

const sampleDefinitions = {
  'Bingo.Ts': runBingo,
  'TicTacToe.Ts': runTicTacToe,
  'SupportChat.Ts': runSupportChat,
  'DeliveryDispatch.Ts': runDeliveryDispatch,
  'GameQuest.Ts': runGameQuest,
  'ShoppingMall.Ts': runShoppingMall
};

async function runBingo(ctx) {
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

async function runTicTacToe(ctx) {
  const endpoints = {
    apiHttp: [`http://127.0.0.1:${await ctx.port()}`, `http://127.0.0.1:${await ctx.port()}`],
    api: [`tcp://127.0.0.1:${await ctx.port()}`, `tcp://127.0.0.1:${await ctx.port()}`],
    playChannel: [`tcp://127.0.0.1:${await ctx.port()}`, `tcp://127.0.0.1:${await ctx.port()}`],
    playStream: [`ws://127.0.0.1:${await ctx.port()}`, `ws://127.0.0.1:${await ctx.port()}`],
    playSpot: [`tcp://127.0.0.1:${await ctx.port()}`, `tcp://127.0.0.1:${await ctx.port()}`],
    playPub: [`tcp://127.0.0.1:${await ctx.port()}`, `tcp://127.0.0.1:${await ctx.port()}`]
  };
  const redisKeyPrefix = `tictactoe:node:${process.pid}:`;
  const config = (instanceName, apiIndex, playIndex, peerPlayIndex) => ({
    instanceName,
    apiIndex,
    playIndex,
    apiHttpEndpoint: endpoints.apiHttp[apiIndex],
    apiEndpoints: endpoints.api,
    apiHttpEndpoints: endpoints.apiHttp,
    playEndpoint: endpoints.playChannel[playIndex],
    playChannelEndpoints: endpoints.playChannel,
    playEndpoints: endpoints.playStream,
    playSpotEndpoint: endpoints.playSpot[playIndex],
    playSpotEndpoints: endpoints.playSpot,
    playSpotPubSubEndpoint: endpoints.playPub[playIndex],
    playSpotPubSubEndpoints: endpoints.playPub,
    playStreamEndpoint: endpoints.playStream[playIndex],
    redisEndpoint: ctx.redisEndpoint,
    redisKeyPrefix,
    logDir: path.join(ctx.logDir, 'flow'),
    playSpotNodeRid: `play-node-${playIndex + 1}`,
    peerPlaySpotNodeRid: `play-node-${peerPlayIndex + 1}`,
    peerPlaySpotEndpoint: endpoints.playSpot[peerPlayIndex],
    peerPlaySpotPubEndpoint: endpoints.playPub[peerPlayIndex]
  });
  const configs = {
    playA: ctx.writeConfig('play-a', config('play-a', 0, 0, 1)),
    playB: ctx.writeConfig('play-b', config('play-b', 0, 1, 0)),
    apiA: ctx.writeConfig('api-a', config('api-a', 0, 0, 1)),
    apiB: ctx.writeConfig('api-b', config('api-b', 1, 0, 1))
  };
  fs.mkdirSync(path.join(ctx.logDir, 'flow'), { recursive: true });
  await ctx.start('play-b', 'dist/Server/Play/main.js', ['--config', configs.playB]);
  await ctx.waitTcp(endpoints.playStream[1]);
  await ctx.start('play-a', 'dist/Server/Play/main.js', ['--config', configs.playA]);
  await ctx.waitTcp(endpoints.playStream[0]);
  await ctx.waitLog('play-a', 'spotPeerReady');
  await ctx.waitLog('play-b', 'spotPeerReady');
  await ctx.start('api-a', 'dist/Server/Api/main.js', ['--config', configs.apiA]);
  await ctx.waitTcp(endpoints.apiHttp[0]);
  await ctx.start('api-b', 'dist/Server/Api/main.js', ['--config', configs.apiB]);
  await ctx.waitTcp(endpoints.apiHttp[1]);
  ctx.runBrowser({
    timeoutMs: 90_000,
    config: { apiHttpEndpoint: '/api/tictactoe' },
    proxies: [{ prefix: '/api/tictactoe', target: endpoints.apiHttp[0] }]
  });
}

async function runSupportChat(ctx) {
  const logDir = path.join(ctx.logDir, 'flow');
  fs.mkdirSync(logDir, { recursive: true });
  const apiChannelEndpoint = `tcp://127.0.0.1:${await ctx.port()}`;
  const supportChannelEndpoint = `tcp://127.0.0.1:${await ctx.port()}`;
  const supportSpotEndpoint = `tcp://127.0.0.1:${await ctx.port()}`;
  const sessionSpotEndpoint = `tcp://127.0.0.1:${await ctx.port()}`;
  const sessionStreamEndpoint = `ws://127.0.0.1:${await ctx.port()}`;
  const redisKeyPrefix = `supportchat:node:${process.pid}:`;
  const common = { redisEndpoint: ctx.redisEndpoint, redisKeyPrefix, logDir };
  const supportConfig = ctx.writeConfig('support', {
    ...common, supportChannelEndpoint, supportSpotEndpoint
  });
  const apiConfig = ctx.writeConfig('api', { ...common, apiChannelEndpoint });
  const sessionConfig = ctx.writeConfig('session', {
    ...common, sessionSpotEndpoint, sessionStreamEndpoint
  });
  await ctx.start('support', 'dist/Server/Support/main.js', ['--config', supportConfig]);
  await ctx.waitTcp(supportSpotEndpoint);
  await ctx.start('api', 'dist/Server/Api/main.js', ['--config', apiConfig]);
  await ctx.waitTcp(apiChannelEndpoint);
  await ctx.start('session', 'dist/Server/Session/main.js', ['--config', sessionConfig]);
  await ctx.waitTcp(sessionStreamEndpoint);
  ctx.runNode(path.join(ctx.nodeRoot, 'e2e/location-readiness.js'), [
    '--redis-endpoint', ctx.redisEndpoint,
    '--key-prefix', `${redisKeyPrefix}location`,
    '--peer', 'client-server', 'supportchat.api', 'router', apiChannelEndpoint,
    '--peer', 'client-server', 'supportchat.support', 'router', supportChannelEndpoint,
    '--peer', 'spot-mesh', 'supportchat-conversations', 'spot', supportSpotEndpoint
  ]);
  ctx.runBrowser({
    timeoutMs: 90_000,
    config: { sessionStreamEndpoint },
    proxies: []
  });
}

async function runDeliveryDispatch(ctx) {
  const flowDir = path.join(ctx.logDir, 'flow');
  fs.mkdirSync(flowDir, { recursive: true });
  const sample = {
    dispatchApiHttpUrl: `http://127.0.0.1:${await ctx.port()}`,
    dispatchEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    dispatchSpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    courierStreamEndpoint: `ws://127.0.0.1:${await ctx.port()}`,
    courierActorNode1SpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    courierActorNode2SpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    trackingEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    trackingSpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    sessionStreamEndpoint: `ws://127.0.0.1:${await ctx.port()}`,
    sessionSpotRouterEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    courierSessionSpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    sessionSpotNodeRid: 'delivery-session-node',
    redisEndpoint: ctx.redisEndpoint,
    redisKeyPrefix: `deliverydispatch:node:${process.pid}:`,
    logDir: flowDir,
    workDir: ctx.workDir
  };
  const configPath = ctx.writeConfig('deliverydispatch', sample);
  for (const [role, entry, ready] of [
    ['tracking', 'dist/Server/Tracking/main.js', sample.trackingEndpoint],
    ['customer-gateway', 'dist/Server/Session/main.js', sample.sessionStreamEndpoint],
    ['courier-session', 'dist/Server/CourierSession/main.js', sample.courierStreamEndpoint],
    ['courier-spot-node1', 'dist/Server/Courier/node1-main.js', sample.courierActorNode1SpotEndpoint],
    ['courier-spot-node2', 'dist/Server/Courier/node2-main.js', sample.courierActorNode2SpotEndpoint],
    ['dispatch', 'dist/Server/Dispatch/main.js', sample.dispatchEndpoint]
  ]) {
    await ctx.start(role, entry, ['--config', configPath]);
    await ctx.waitTcp(ready);
  }
  await ctx.waitHttp(sample.dispatchApiHttpUrl);
  ctx.runNode(path.join(ctx.nodeRoot, 'e2e/location-readiness.js'), [
    '--redis-endpoint', ctx.redisEndpoint,
    '--key-prefix', `${sample.redisKeyPrefix}location`,
    '--peer', 'client-server', 'deliverydispatch.dispatch', 'router', sample.dispatchEndpoint,
    '--peer', 'client-server', 'deliverydispatch.tracking', 'router', sample.trackingEndpoint,
    '--peer', 'spot-mesh', 'delivery-couriers', 'spot',
      sample.dispatchSpotEndpoint, sample.courierSessionSpotEndpoint,
      sample.courierActorNode1SpotEndpoint, sample.courierActorNode2SpotEndpoint,
    '--peer', 'spot-mesh', 'delivery-customers', 'spot',
      sample.sessionSpotRouterEndpoint, sample.trackingSpotEndpoint
  ]);
  console.log('topology=ready');
  ctx.runBrowser({
    timeoutMs: 90_000,
    config: {
      dispatchApiHttpUrl: '/api/delivery',
      sessionStreamEndpoint: sample.sessionStreamEndpoint,
      courierStreamEndpoint: sample.courierStreamEndpoint
    },
    proxies: [{ prefix: '/api/delivery', target: sample.dispatchApiHttpUrl }]
  });
  await ctx.waitLog('dispatch', 'ignored stale decision delivery=delivery-reassign courier=courier-a attempt=1');
}

async function runGameQuest(ctx) {
  const sample = {
    apiAHttpUrl: `http://127.0.0.1:${await ctx.port()}`,
    apiBHttpUrl: `http://127.0.0.1:${await ctx.port()}`,
    apiAStreamEndpoint: `ws://127.0.0.1:${await ctx.port()}`,
    apiBStreamEndpoint: `ws://127.0.0.1:${await ctx.port()}`,
    apiAActorSpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    apiBActorSpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    missionAEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    missionBEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    missionASpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    missionBSpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    missionASpotRouterEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    missionBSpotRouterEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    missionAHttpUrl: `http://127.0.0.1:${await ctx.port()}`,
    missionBHttpUrl: `http://127.0.0.1:${await ctx.port()}`,
    redisEndpoint: ctx.redisEndpoint,
    redisKeyPrefix: `gamequest:node:${process.pid}:`,
    logDir: ctx.logDir,
    workDir: ctx.workDir
  };
  const configPath = ctx.writeConfig('gamequest', sample);
  for (const [role, entry, ready] of [
    ['mission-a', 'dist/Server/MissionA/main.js', sample.missionAHttpUrl],
    ['mission-b', 'dist/Server/MissionB/main.js', sample.missionBHttpUrl],
    ['api-a', 'dist/Server/ApiA/main.js', sample.apiAHttpUrl],
    ['api-b', 'dist/Server/ApiB/main.js', sample.apiBHttpUrl]
  ]) {
    await ctx.start(role, entry, ['--config', configPath]);
    await ctx.waitHttp(ready);
  }
  ctx.runBrowser({
    timeoutMs: 120_000,
    config: {
      apiAHttpUrl: '/api/gamequest/api-a',
      apiBHttpUrl: '/api/gamequest/api-b',
      apiAStreamEndpoint: sample.apiAStreamEndpoint,
      apiBStreamEndpoint: sample.apiBStreamEndpoint,
      missionAHttpUrl: '/api/gamequest/mission-a',
      missionBHttpUrl: '/api/gamequest/mission-b'
    },
    proxies: [
      { prefix: '/api/gamequest/api-a', target: sample.apiAHttpUrl },
      { prefix: '/api/gamequest/api-b', target: sample.apiBHttpUrl },
      { prefix: '/api/gamequest/mission-a', target: sample.missionAHttpUrl },
      { prefix: '/api/gamequest/mission-b', target: sample.missionBHttpUrl }
    ]
  });
}

async function runShoppingMall(ctx) {
  const sample = {
    apiAHttpUrl: `http://127.0.0.1:${await ctx.port()}`,
    apiBHttpUrl: `http://127.0.0.1:${await ctx.port()}`,
    workflowAHttpUrl: `http://127.0.0.1:${await ctx.port()}`,
    workflowBHttpUrl: `http://127.0.0.1:${await ctx.port()}`,
    workflowAChannelEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    workflowBChannelEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    workflowASpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    workflowBSpotEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    workflowASpotPubEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    workflowBSpotPubEndpoint: `tcp://127.0.0.1:${await ctx.port()}`,
    redisEndpoint: ctx.redisEndpoint,
    redisKeyPrefix: `shoppingmall:node:${process.pid}:`,
    logDir: ctx.logDir,
    workDir: ctx.workDir
  };
  const configPath = ctx.writeConfig('shoppingmall', sample);
  for (const [role, entry, ready] of [
    ['workflow-a', 'dist/Server/WorkflowA/main.js', sample.workflowAHttpUrl],
    ['workflow-b', 'dist/Server/WorkflowB/main.js', sample.workflowBHttpUrl],
    ['api-a', 'dist/Server/ApiA/main.js', sample.apiAHttpUrl],
    ['api-b', 'dist/Server/ApiB/main.js', sample.apiBHttpUrl]
  ]) {
    await ctx.start(role, entry, ['--config', configPath]);
    await ctx.waitHttp(ready);
  }
  ctx.runNode(path.join(ctx.sampleRoot, 'dist/Client/main.js'), [
    '--api-a-http', sample.apiAHttpUrl,
    '--api-b-http', sample.apiBHttpUrl
  ]);
}

async function reserveBrowserSafePort() {
  for (let attempt = 0; attempt < 200; attempt += 1) {
    const port = 41000 + Math.floor(Math.random() * 20000);
    if (reservedPorts.has(port)) continue;
    if (await canBind(port)) {
      reservedPorts.add(port);
      return port;
    }
  }
  throw new Error('Unable to reserve a browser-safe loopback port.');
}

function canBind(port) {
  return new Promise((resolve) => {
    const server = net.createServer();
    server.once('error', () => resolve(false));
    server.listen(port, '127.0.0.1', () => server.close(() => resolve(true)));
  });
}

async function startRedis() {
  const name = `zlink-redis-node-sample-${process.pid}-${Date.now()}`;
  const image = runnerOptions.redisImage;
  const created = command('docker', [
    'create', '--name', name, '--tmpfs', '/data', '-p', '127.0.0.1::6379', image
  ]).trim();
  redisContainer = created;
  command('docker', ['start', created]);
  const portLine = command('docker', ['port', created, '6379/tcp']).trim();
  const port = portLine.slice(portLine.lastIndexOf(':') + 1);
  const endpoint = `127.0.0.1:${port}`;
  await waitTcp(`tcp://${endpoint}`);
  return endpoint;
}

function parseRunnerOptions(args) {
  const options = { keepRunDir: false, redisImage: 'redis:7.2-alpine' };
  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index];
    if (argument === '--keep-run-dir') {
      options.keepRunDir = true;
      continue;
    }
    if (argument === '--redis-image') {
      const value = args[++index];
      if (!value || value.startsWith('--')) throw new Error('--redis-image <image> is required.');
      options.redisImage = value;
      continue;
    }
    throw new Error(`Unknown runner option '${argument}'.`);
  }
  return options;
}

function startNode(name, entry, args, env) {
  const logPath = path.join(logDir, `${name}.log`);
  const output = fs.openSync(logPath, 'a');
  const child = spawn(process.execPath, [entry, ...args], {
    cwd: sampleRoot,
    env,
    stdio: ['ignore', output, output]
  });
  fs.closeSync(output);
  const state = { child, logPath, name, status: undefined };
  child.once('exit', (code, signal) => {
    state.status = code ?? (signal ? 1 : 0);
  });
  return state;
}

async function waitTcp(endpoint) {
  const url = new URL(endpoint.replace(/^tcp:/, 'http:').replace(/^ws:/, 'http:'));
  await waitUntil(`endpoint ${endpoint}`, () => new Promise((resolve) => {
    const socket = net.connect(Number(url.port), url.hostname);
    socket.once('connect', () => { socket.destroy(); resolve(true); });
    socket.once('error', () => resolve(false));
    socket.setTimeout(500, () => { socket.destroy(); resolve(false); });
  }));
}

async function waitHttp(endpoint) {
  await waitUntil(`health ${endpoint}`, async () => {
    try {
      const response = await fetch(new URL('/health', endpoint), { signal: AbortSignal.timeout(1000) });
      return response.ok;
    } catch {
      return false;
    }
  });
}

async function waitLog(name, marker) {
  await waitUntil(`${name} log marker '${marker}'`, async () => {
    const target = path.join(logDir, `${name}.log`);
    return fs.existsSync(target) && fs.readFileSync(target, 'utf8').includes(marker);
  });
}

function assertLogCount(name, marker, expected) {
  const target = path.join(logDir, `${name}.log`);
  const content = fs.existsSync(target) ? fs.readFileSync(target, 'utf8') : '';
  const actual = content.split(marker).length - 1;
  if (actual !== expected) {
    throw new Error(`${name} log marker '${marker}' count was ${actual}; expected ${expected}.`);
  }
}

async function waitUntil(description, probe) {
  const deadline = Date.now() + 30_000;
  while (Date.now() < deadline) {
    ensureChildrenRunning();
    if (await probe()) return;
    await sleep(100);
  }
  throw new Error(`Timed out waiting for ${description}.`);
}

function ensureChildrenRunning() {
  const stopped = children.find((entry) => entry.status !== undefined);
  if (stopped) {
    throw new Error(`${stopped.name} exited before the sample client ran. See ${stopped.logPath}.`);
  }
}

function run(executable, args, options = {}) {
  const result = spawnSync(platformExecutable(executable), args, {
    cwd: options.cwd ?? sampleRoot,
    env: options.env ?? process.env,
    stdio: 'inherit'
  });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    throw new Error(`${executable} ${args.join(' ')} exited with ${result.status}.`);
  }
}

function command(executable, args) {
  const result = spawnSync(platformExecutable(executable), args, { encoding: 'utf8' });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    throw new Error(`${executable} ${args.join(' ')} failed: ${result.stderr.trim()}`);
  }
  return result.stdout;
}

function platformExecutable(executable) {
  return process.platform === 'win32' && executable === 'npm' ? 'npm.cmd' : executable;
}

async function cleanup() {
  if (cleaning) return;
  cleaning = true;
  for (const { child } of children.reverse()) {
    if (child.exitCode === null && child.signalCode === null) child.kill('SIGINT');
  }
  await sleep(500);
  for (const { child } of children) {
    if (child.exitCode === null && child.signalCode === null) child.kill('SIGKILL');
  }
  if (redisContainer) {
    spawnSync(platformExecutable('docker'), ['rm', '-fv', redisContainer], { stdio: 'ignore' });
  }
}

function printLogs() {
  for (const entry of children) {
    if (!fs.existsSync(entry.logPath)) continue;
    const lines = fs.readFileSync(entry.logPath, 'utf8').trimEnd().split(/\r?\n/).slice(-80);
    process.stderr.write(`===== ${entry.name} =====\n${lines.join('\n')}\n`);
  }
}

function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

for (const signal of ['SIGINT', 'SIGTERM']) {
  process.once(signal, () => {
    failed = true;
    void cleanup().finally(() => process.exit(signal === 'SIGINT' ? 130 : 143));
  });
}

await main();
