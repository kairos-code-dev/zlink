const assert = require('node:assert/strict');
const childProcess = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const workspaceRoot = path.resolve(__dirname, '..', '..');
const samplesRoot = path.join(workspaceRoot, 'samples');
const requiredSamples = [
  'StreamingClient',
  'Bingo.Ts'
];

test('node samples define the required sample directories and README files', () => {
  const missing = [];
  for (const sample of requiredSamples) {
    for (const relative of ['package.json', 'README.ko.md']) {
      const target = path.join(samplesRoot, sample, relative);
      if (!fs.existsSync(target)) {
        missing.push(`${sample}/${relative}`);
      }
    }
  }
  if (!fs.existsSync(path.join(samplesRoot, 'run_samples.sh'))) {
    missing.push('run_samples.sh');
  }

  assert.deepEqual(missing, []);
});

test('node topology samples mirror dotnet role layout', () => {
  const expected = {
    StreamingClient: ['Client/self-check.js', 'Server/main.js'],
    'Bingo.Ts': [
      'Client/bingo-client-app.ts',
      'Client/bingo-notification-inbox.ts',
      'Client/bingo-player-client.ts',
      'Client/self-check.ts',
      'Server/Api/Handlers/authenticate-player-handler.ts',
      'Server/Api/Handlers/match-bingo-handler.ts',
      'Server/Api/main.ts',
      'Server/Play/Actors/player-actor.ts',
      'Server/Play/Actors/player-actor-factory.ts',
      'Server/Play/BingoRoomSpots/Handlers/bingo-room-join-handler.ts',
      'Server/Play/BingoRoomSpots/Handlers/bingo-room-timer-handler.ts',
      'Server/Play/BingoRoomSpots/Handlers/start-bingo-game-handler.ts',
      'Server/Play/BingoRoomSpots/bingo-card.ts',
      'Server/Play/BingoRoomSpots/bingo-notification-publisher.ts',
      'Server/Play/BingoRoomSpots/bingo-room-models.ts',
      'Server/Play/BingoRoomSpots/bingo-room-spot.ts',
      'Server/Play/EntrySpot/Handlers/match-bingo-actor-handler.ts',
      'Server/Play/EntrySpot/bingo-entry-spot.ts',
      'Server/Play/Handlers/allocate-bingo-room-handler.ts',
      'Server/Play/Handlers/bingo-room-directory.ts',
      'Server/Play/Handlers/bingo-notifications-handler.ts',
      'Server/Play/Handlers/ensure-player-actor-handler.ts',
      'Server/Play/Handlers/match-bingo-channel-handler.ts',
      'Server/Play/Handlers/start-bingo-game-channel-handler.ts',
      'Server/Play/main.ts',
      'Server/Registry/main.ts',
      'Server/Session/Sessions/Handlers/authenticate-session-handler.ts',
      'Server/Session/Sessions/bingo-session.ts',
      'Server/Session/main.ts',
      'Shared/Configuration/sample-names.ts',
      'Shared/Contracts/messages.ts'
    ]
  };
  const missing = [];
  for (const [sample, relatives] of Object.entries(expected)) {
    for (const relative of relatives) {
      if (!fs.existsSync(path.join(samplesRoot, sample, relative))) {
        missing.push(`${sample}/${relative}`);
      }
    }
  }

  assert.deepEqual(missing, []);
});

test('node sample READMEs describe execution topology success condition and regression', () => {
  const missing = [];
  for (const sample of requiredSamples) {
    const readme = fs.readFileSync(path.join(samplesRoot, sample, 'README.ko.md'), 'utf8');
    for (const heading of ['## 실행', '## Topology', '## Success Condition', '## 회귀 테스트']) {
      if (!readme.includes(heading)) {
        missing.push(`${sample}:${heading}`);
      }
    }
  }

  assert.deepEqual(missing, []);
});

test('node samples use only framework and connector public APIs', () => {
  const violations = [];
  for (const file of sampleSourceFiles(samplesRoot)) {
    const content = fs.readFileSync(file, 'utf8');
    if (/bindings\/node|runtime\/native|src\/zlink\/runtime|packages\/[^/]+\/src/.test(content)) {
      violations.push(path.relative(workspaceRoot, file));
    }
  }

  assert.deepEqual(violations, []);
});

test('node framework samples exercise the real NestJS application context', () => {
  const missing = [];
  for (const sample of ['Bingo.Ts']) {
    const usesNestModule = sampleSourceFiles(path.join(samplesRoot, sample))
      .some((file) => fs.readFileSync(file, 'utf8').includes('packages/nestjs/dist'));
    if (!usesNestModule) {
      missing.push(sample);
    }
  }

  const nestRuntime = fs.readFileSync(path.join(samplesRoot, 'shared', 'nestjs-provider-runtime.js'), 'utf8');
  assert.equal(nestRuntime.includes("require('@nestjs/common')"), true);
  assert.equal(nestRuntime.includes("require('@nestjs/core')"), true);
  assert.equal(nestRuntime.includes('NestFactory.createApplicationContext'), true);
  assert.equal(nestRuntime.includes('resolveModuleProviders'), false);

  const hiddenServerRuntime = [];
  for (const file of sampleSourceFiles(samplesRoot)) {
    const relative = path.relative(samplesRoot, file);
    if (relative.startsWith('shared/') || relative.includes('/dist/')) {
      continue;
    }
    const content = fs.readFileSync(file, 'utf8');
    if (/startChannelServer|startRouteServer|createZLinkNestRuntime|nestjs-provider-runtime/.test(content)) {
      hiddenServerRuntime.push(relative);
    }
  }

  const serverRoles = [
    ['Bingo.Ts/Server/Api/main.ts', 'BingoApiModule'],
    ['Bingo.Ts/Server/Play/main.ts', 'BingoPlayModule'],
    ['Bingo.Ts/Server/Registry/main.ts', 'BingoRegistryModule'],
    ['Bingo.Ts/Server/Session/main.ts', 'BingoSessionModule']
  ];
  for (const [relative, moduleName] of serverRoles) {
    const content = fs.readFileSync(path.join(samplesRoot, relative), 'utf8');
    for (const text of [
      "require('@nestjs/common')",
      "require('@nestjs/core')",
      'ZLinkModule.forRoot',
      'NestFactory.createApplicationContext',
      `})(${moduleName});`
    ]) {
      if (!content.includes(text)) {
        missing.push(`${relative}:${text}`);
      }
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(hiddenServerRuntime, []);
});

test('Bingo TypeScript sample builds and exposes separated TypeScript roles', () => {
  const packageJson = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'package.json'), 'utf8');
  const tsconfig = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'tsconfig.json'), 'utf8');
  const client = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Client', 'self-check.ts'), 'utf8');
  const api = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Api', 'main.ts'), 'utf8');
  const session = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Session', 'main.ts'), 'utf8');
  const play = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Play', 'main.ts'), 'utf8');
  const readme = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'README.ko.md'), 'utf8');
  const runSamples = fs.readFileSync(path.join(samplesRoot, 'run_samples.sh'), 'utf8');
  const required = [
    [packageJson, '@zlink-systems/sample-bingo-ts'],
    [packageJson, 'tsc -p tsconfig.json'],
    [tsconfig, '"outDir": "dist"'],
    [tsconfig, '"Server/**/*.ts"'],
    [client, "from './bingo-client-app'"],
    [client, "../Server/Api/main.js"],
    [client, 'PASS Bingo.Ts'],
    [api, 'async function bootstrap'],
    [session, 'async function bootstrap'],
    [play, 'async function bootstrap'],
    [readme, 'TypeScript Client/Server/Shared 구조'],
    [runSamples, 'samples/Bingo.Ts'],
    [runSamples, 'npm run build'],
    [runSamples, 'npm run start']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);
  const violations = [];
  for (const file of sampleSourceFiles(path.join(samplesRoot, 'Bingo.Ts'))) {
    if (!file.endsWith('.ts')) {
      continue;
    }
    const content = fs.readFileSync(file, 'utf8');
    if (/require\(['"][^'"]*\/Bingo\/|from ['"][^'"]*\/Bingo\//.test(content)) {
      violations.push(`${path.relative(samplesRoot, file)} references the JavaScript Bingo sample`);
    }
    if (content.includes('@ts-nocheck')) {
      violations.push(`${path.relative(samplesRoot, file)} disables TypeScript checking`);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('node topology samples run server roles as separate processes over TCP route endpoints', () => {
  const cases = [
    ['StreamingClient', 'Server/main.js', 'STREAMING_CLIENT_ENDPOINT'],
    ['Bingo.Ts', 'Server/Api/main.ts', 'BINGO_API_ENDPOINT'],
    ['Bingo.Ts', 'Server/Play/main.ts', 'BINGO_PLAY_ENDPOINT'],
    ['Bingo.Ts', 'Server/Session/main.ts', 'BINGO_SESSION_ENDPOINT'],
    ['Bingo.Ts', 'Server/Registry/main.ts', 'BINGO_REGISTRY_ENDPOINT']
  ];

  for (const [sample, serverRelative, endpointEnv] of cases) {
    const serverEntry = path.join(samplesRoot, sample, serverRelative);
    const clientEntry = fs.existsSync(path.join(samplesRoot, sample, 'Client', 'self-check.js'))
      ? path.join(samplesRoot, sample, 'Client', 'self-check.js')
      : path.join(samplesRoot, sample, 'Client', 'self-check.ts');
    const serverContent = fs.readFileSync(serverEntry, 'utf8');
    const clientContent = fs.readFileSync(clientEntry, 'utf8');

    assert.equal(fs.existsSync(serverEntry), true);
    assert.match(serverContent, new RegExp(escapeRegExp(endpointEnv)));
    assert.match(clientContent, /withServers/);
    assert.match(clientContent, /reserveTcpEndpoint/);
  }
});

test('node topology samples do not use stdin command protocol as messaging', () => {
  const violations = [];
  for (const file of sampleSourceFiles(samplesRoot)) {
    const content = fs.readFileSync(file, 'utf8');
    if (/runRoleServer|startRoleProcess|withRoleProcess|command ===|stdin\.write/.test(content)) {
      violations.push(path.relative(samplesRoot, file));
    }
  }

  assert.deepEqual(violations, []);
});

test('node samples do not hide readiness with sleeps or pre-ready pings', () => {
  const violations = [];
  for (const file of sampleSourceFiles(samplesRoot)) {
    const content = fs.readFileSync(file, 'utf8');
    if (/\bsleep\s*\(|setTimeout\s*\(|beforeReady/.test(content)) {
      violations.push(path.relative(workspaceRoot, file));
    }
  }

  assert.deepEqual(violations, []);
});

test('node sample process host fails instead of hanging when a ready role exits', () => {
  const processHost = fs.readFileSync(path.join(samplesRoot, 'shared', 'process-host.js'), 'utf8');
  const required = [
    'exitedUnexpectedly',
    'Promise.race',
    'exited while sample was running',
    "child.kill('SIGKILL')",
    'const closed = await waitForClose'
  ];
  const missing = required.filter((text) => !processHost.includes(text));

  assert.deepEqual(missing, []);
});

test('node session samples do not implement sample-only actor session stores', () => {
  const bannedPatterns = [
    /SessionBindingTable/,
    /BoundNotificationHub/,
    /bindings\s*=\s*new Map\(/,
    /notificationHub/,
    /sessionFor\(actorId\)/,
    /staleSend\(actorId/
  ];
  const violations = [];

  for (const sample of ['Bingo.Ts']) {
    for (const file of sampleSourceFiles(path.join(samplesRoot, sample))) {
      const content = fs.readFileSync(file, 'utf8');
      for (const pattern of bannedPatterns) {
        if (pattern.test(content)) {
          violations.push(`${path.relative(samplesRoot, file)} matches ${pattern}`);
        }
      }
    }
  }

  assert.deepEqual(violations, []);
});

test('node run_samples.sh executes every sample self-check', () => {
  const output = childProcess.execFileSync(path.join(samplesRoot, 'run_samples.sh'), {
    cwd: workspaceRoot,
    encoding: 'utf8'
  });

  for (const sample of requiredSamples) {
    assert.match(output, new RegExp(`PASS ${escapeRegExp(sample)}`));
  }
});

test('StreamingClient sample covers reconnect request reply and manual notification dispatch', () => {
  const sample = fs.readFileSync(path.join(samplesRoot, 'StreamingClient', 'Client', 'self-check.js'), 'utf8');
  const server = fs.readFileSync(path.join(samplesRoot, 'StreamingClient', 'Server', 'main.js'), 'utf8');
  const required = [
    'dispatchMode: connector.ZlinkStreamDispatchMode.Manual',
    ".request(json.toJson({ playerId: 'p1' }))",
    "client.on('ServerNotice'",
    'endpoint'
  ];
  const missing = required.filter((text) => !sample.includes(text));
  if (!server.includes('net.createServer') || !server.includes('ZlinkStreamFrameCodec.decode')) {
    missing.push('tcp stream server');
  }

  assert.deepEqual(missing, []);
});

test('node cross-language smoke covers channel send publish and stream connector paths', () => {
  const smoke = fs.readFileSync(path.join(workspaceRoot, 'cross-language', 'node_dotnet_smoke.js'), 'utf8');
  const required = [
    'requestToChannel',
    'sendToChannel',
    'publishToChannel',
    'nodePublisherToDotnetFanoutSubscriber',
    'nodeConnectorToDotnetStreamServer',
    'dotnetConnectorToNodeStreamServer'
  ];
  const missing = required.filter((text) => !smoke.includes(text));

  assert.deepEqual(missing, []);
});

function sampleSourceFiles(root) {
  const files = [];
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const fullPath = path.join(root, entry.name);
    if (entry.isDirectory()) {
      files.push(...sampleSourceFiles(fullPath));
    } else if (entry.isFile() && /\.(?:js|ts|mjs|cjs|md|sh)$/.test(entry.name)) {
      files.push(fullPath);
    }
  }
  return files;
}

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}
