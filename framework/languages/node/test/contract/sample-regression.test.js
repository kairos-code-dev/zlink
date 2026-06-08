const assert = require('node:assert/strict');
const childProcess = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const workspaceRoot = path.resolve(__dirname, '..', '..');
const samplesRoot = path.join(workspaceRoot, 'samples');
const requiredSamples = [
  'TicTacToe.Ts',
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
  if (fs.existsSync(path.join(samplesRoot, 'shared'))) {
    missing.push('samples/shared must not hide sample logic');
  }

  assert.deepEqual(missing, []);
});

test('node topology samples mirror dotnet role layout', () => {
  const expected = {
    'TicTacToe.Ts': [
      'Client/self-check.ts',
      'Server/Api/Handlers/authenticate-player-handler.ts',
      'Server/Api/Handlers/create-game-http-handler.ts',
      'Server/Api/main.ts',
      'Server/Play/Domain/TicTacToe/tictactoe-board.ts',
      'Server/Play/Domain/TicTacToe/tictactoe-match.ts',
      'Server/Play/Application/GameCreation/tictactoe-game.ts',
      'Server/Play/Adapters/ZLink/Actors/play-actor.ts',
      'Server/Play/Adapters/ZLink/Actors/play-actor-factory.ts',
      'Server/Play/Adapters/ZLink/Handlers/create-game-handler.ts',
      'Server/Play/Adapters/ZLink/Sessions/play-session.ts',
      'Server/Play/Adapters/ZLink/Sessions/play-session-factory.ts',
      'Server/Play/Adapters/ZLink/Spots/Handlers/play-actor-join-game-handler.ts',
      'Server/Play/Adapters/ZLink/Spots/Handlers/play-actor-place-mark-handler.ts',
      'Server/Play/Adapters/ZLink/Spots/Handlers/tictactoe-game-timer-handler.ts',
      'Server/Play/Adapters/ZLink/Spots/play-entry-spot.ts',
      'Server/Play/main.ts',
      'Shared/Contracts/messages.ts'
    ],
    'Bingo.Ts': [
      'Client/bingo-client-app.ts',
      'Client/bingo-notification-inbox.ts',
      'Client/bingo-player-client.ts',
      'Client/self-check.ts',
      'Server/Api/Handlers/authenticate-player-handler.ts',
      'Server/Api/Handlers/match-bingo-handler.ts',
      'Server/Api/main.ts',
      'Server/Play/Domain/Bingo/bingo-card.ts',
      'Server/Play/Domain/Bingo/bingo-room-game.ts',
      'Server/Play/Domain/Bingo/bingo-room-models.ts',
      'Server/Play/Application/RoomAllocation/bingo-room-directory.ts',
      'Server/Play/Adapters/ZLink/Actors/player-actor.ts',
      'Server/Play/Adapters/ZLink/Actors/player-actor-factory.ts',
      'Server/Play/Adapters/ZLink/Handlers/allocate-bingo-room-handler.ts',
      'Server/Play/Adapters/ZLink/Handlers/bingo-notifications-handler.ts',
      'Server/Play/Adapters/ZLink/Handlers/ensure-player-actor-handler.ts',
      'Server/Play/Adapters/ZLink/Handlers/match-bingo-channel-handler.ts',
      'Server/Play/Adapters/ZLink/Handlers/submit-bingo-card-channel-handler.ts',
      'Server/Play/Adapters/ZLink/Notifications/bingo-notification-publisher.ts',
      'Server/Play/Adapters/ZLink/Spots/Handlers/bingo-room-timer-handler.ts',
      'Server/Play/Adapters/ZLink/Spots/Handlers/match-bingo-actor-handler.ts',
      'Server/Play/Adapters/ZLink/Spots/Handlers/submit-bingo-card-handler.ts',
      'Server/Play/Adapters/ZLink/Spots/bingo-entry-spot.ts',
      'Server/Play/Adapters/ZLink/Spots/bingo-room-spot.ts',
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

test('node samples keep only the common TicTacToe and Bingo variants', () => {
  const entries = fs.readdirSync(samplesRoot, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => entry.name)
    .sort();

  assert.deepEqual(entries, ['Bingo.Ts', 'TicTacToe.Ts']);
  assert.equal(entries.some((entry) => /SessionGateway|Gateway|StreamingClient/.test(entry)), false);
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
  for (const sample of ['TicTacToe.Ts', 'Bingo.Ts']) {
    const usesNestModule = sampleSourceFiles(path.join(samplesRoot, sample))
      .some((file) => fs.readFileSync(file, 'utf8').includes('packages/nestjs/dist'));
    if (!usesNestModule) {
      missing.push(sample);
    }
  }

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
    ['TicTacToe.Ts/Server/Api/main.ts', 'TicTacToeApiModule'],
    ['TicTacToe.Ts/Server/Play/main.ts', 'TicTacToePlayModule'],
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

test('TicTacToe TypeScript sample builds and exposes basic TypeScript roles', () => {
  const packageJson = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'package.json'), 'utf8');
  const tsconfig = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'tsconfig.json'), 'utf8');
  const client = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Client', 'self-check.ts'), 'utf8');
  const api = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Api', 'main.ts'), 'utf8');
  const play = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'main.ts'), 'utf8');
  const readme = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'README.ko.md'), 'utf8');
  const runSamples = fs.readFileSync(path.join(samplesRoot, 'run_samples.sh'), 'utf8');
  const required = [
    [packageJson, '@zlink-systems/sample-tictactoe-ts'],
    [packageJson, 'tsc -p tsconfig.json'],
    [tsconfig, '"outDir": "dist"'],
    [tsconfig, '"Server/**/*.ts"'],
    [client, "../Server/Api/main.js"],
    [client, "../Server/Play/main.js"],
    [client, 'PASS TicTacToe.Ts'],
    [api, 'TicTacToeApiModule'],
    [play, 'TicTacToePlayModule'],
    [readme, 'TicTacToe TypeScript Sample'],
    [runSamples, 'samples/TicTacToe.Ts'],
    [runSamples, 'npm run build'],
    [runSamples, 'npm run start']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);
  const violations = [];
  for (const file of sampleSourceFiles(path.join(samplesRoot, 'TicTacToe.Ts'))) {
    if (!file.endsWith('.ts')) {
      continue;
    }
    const content = fs.readFileSync(file, 'utf8');
    if (/require\(['"][^'"]*samples\/TicTacToe\/|from ['"][^'"]*samples\/TicTacToe\//.test(content)) {
      violations.push(`${path.relative(samplesRoot, file)} references the JavaScript TicTacToe sample`);
    }
    if (content.includes('@ts-nocheck')) {
      violations.push(`${path.relative(samplesRoot, file)} disables TypeScript checking`);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('TicTacToe TypeScript sample mirrors dotnet game state contract', () => {
  const readme = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'README.ko.md'), 'utf8');
  const client = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Client', 'self-check.ts'), 'utf8');
  const board = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Domain', 'TicTacToe', 'tictactoe-board.ts'), 'utf8');
  const match = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Domain', 'TicTacToe', 'tictactoe-match.ts'), 'utf8');
  const joinHandler = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Adapters', 'ZLink', 'Spots', 'Handlers', 'play-actor-join-game-handler.ts'), 'utf8');
  const moveHandler = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Adapters', 'ZLink', 'Spots', 'Handlers', 'play-actor-place-mark-handler.ts'), 'utf8');
  const required = [
    [board, 'class TicTacToeBoard'],
    [match, 'class TicTacToeMatch'],
    [match, "this.status = 'InProgress'"],
    [match, "this.status = 'Won'"],
    [match, "this.status = 'TurnTimedOut'"],
    [joinHandler, 'gameStateNotify(state)'],
    [moveHandler, 'gameStateNotify(state)'],
    [client, "payload.state.status === 'InProgress'"],
    [client, "moves.at(-1).state.status, 'Won'"],
    [readme, '`Won`']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);
  const violations = [];
  for (const [content, text] of [
    [match, "this.status = 'Running'"],
    [match, "this.status = 'Finished'"],
    [client, "status === 'Running'"],
    [client, "status, 'Finished'"]
  ]) {
    if (content.includes(text)) {
      violations.push(text);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
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
    if (/require\(['"][^'"]*samples\/Bingo\/|from ['"][^'"]*samples\/Bingo\//.test(content)) {
      violations.push(`${path.relative(samplesRoot, file)} references the JavaScript Bingo sample`);
    }
    if (content.includes('@ts-nocheck')) {
      violations.push(`${path.relative(samplesRoot, file)} disables TypeScript checking`);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('Bingo TypeScript sample uses registry discovery instead of direct server peer endpoints', () => {
  const client = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Client', 'self-check.ts'), 'utf8');
  const api = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Api', 'main.ts'), 'utf8');
  const play = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Play', 'main.ts'), 'utf8');
  const session = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Session', 'main.ts'), 'utf8');
  const registry = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Registry', 'main.ts'), 'utf8');
  const discovery = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'discovery-support.ts'), 'utf8');
  const required = [
    [registry, 'RegisterServiceHandler'],
    [registry, 'ResolveServiceHandler'],
    [discovery, 'createRegistryClient'],
    [play, 'registry.register(SampleNames.playService'],
    [api, 'registry.resolve(SampleNames.playService'],
    [api, 'registry.register(SampleNames.apiService'],
    [session, 'registry.resolve(SampleNames.apiService'],
    [session, 'registry.resolve(SampleNames.playService'],
    [session, 'registry.resolve(SampleNames.notificationService'],
    [play, 'registry.register(SampleNames.notificationService']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);
  const violations = [];
  for (const [content, text] of [
    [session, 'process.env.BINGO_API_ENDPOINT'],
    [session, 'process.env.BINGO_PLAY_ENDPOINT'],
    [api, 'process.env.BINGO_PLAY_ENDPOINT']
  ]) {
    if (content.includes(text)) {
      violations.push(text);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('Bingo TypeScript sample publishes drawn number before finished notify', () => {
  const roomSpot = fs.readFileSync(path.join(
    samplesRoot,
    'Bingo.Ts',
    'Server',
    'Play',
    'Adapters',
    'ZLink',
    'Spots',
    'bingo-room-spot.ts'
  ), 'utf8');
  const drawIndex = roomSpot.indexOf('this.notifications.numberDrawn');
  const finishedBranchIndex = roomSpot.indexOf('if (drawn.finished)');
  const endedIndex = roomSpot.indexOf('this.notifications.gameEnded');

  assert.equal(drawIndex > 0, true);
  assert.equal(finishedBranchIndex > drawIndex, true);
  assert.equal(endedIndex > finishedBranchIndex, true);
});

test('node topology samples run server roles as separate processes over TCP route endpoints', () => {
  const cases = [
    ['TicTacToe.Ts', 'Server/Api/main.ts', 'TICTACTOE_API_ENDPOINT'],
    ['TicTacToe.Ts', 'Server/Play/main.ts', 'TICTACTOE_PLAY_ENDPOINT'],
    ['Bingo.Ts', 'Server/Api/main.ts', 'BINGO_API_ENDPOINT'],
    ['Bingo.Ts', 'Server/Play/main.ts', 'BINGO_PLAY_ENDPOINT'],
    ['Bingo.Ts', 'Server/Play/main.ts', 'BINGO_NOTIFICATION_ENDPOINT'],
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
  const missing = [];
  for (const sample of requiredSamples) {
    const processHost = fs.readFileSync(path.join(samplesRoot, sample, 'Client', 'sample-process-host.ts'), 'utf8');
    for (const text of [
      'exitedUnexpectedly',
      'Promise.race',
      'exited while sample was running',
      "child.kill('SIGKILL')",
      'const closed = await waitForClose'
    ]) {
      if (!processHost.includes(text)) {
        missing.push(`${sample}:${text}`);
      }
    }
  }

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

  for (const sample of ['TicTacToe.Ts', 'Bingo.Ts']) {
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
