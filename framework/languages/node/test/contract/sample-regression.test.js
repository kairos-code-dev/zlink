const assert = require('node:assert/strict');
const childProcess = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const workspaceRoot = path.resolve(__dirname, '..', '..');
const samplesRoot = path.join(workspaceRoot, 'samples');
const requiredSamples = [
  'StreamingClient',
  'TicTacToe',
  'TicTacToe.SessionGateway',
  'Bingo',
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
    TicTacToe: [
      'Client/self-check.js',
      'Server/Api/Handlers/authenticate-player-handler.js',
      'Server/Api/Handlers/create-game-http-handler.js',
      'Server/Api/main.js',
      'Server/Play/Actors/play-actor.js',
      'Server/Play/Actors/play-actor-factory.js',
      'Server/Play/EntrySpot/play-entry-spot.js',
      'Server/Play/EntrySpot/Handlers/play-actor-join-game-handler.js',
      'Server/Play/GameSpots/Handlers/play-actor-place-mark-handler.js',
      'Server/Play/GameSpots/Handlers/tictactoe-game-created-handler.js',
      'Server/Play/GameSpots/Handlers/tictactoe-game-join-handler.js',
      'Server/Play/GameSpots/Handlers/tictactoe-game-timer-handler.js',
      'Server/Play/GameSpots/tictactoe-game.js',
      'Server/Play/GameSpots/tictactoe-game-models.js',
      'Server/Play/Handlers/create-game-handler.js',
      'Server/Play/Sessions/play-session.js',
      'Server/Play/main.js',
      'Shared/Contracts/messages.js'
    ],
    'TicTacToe.SessionGateway': [
      'Client/self-check.js',
      'Server/Api/Handlers/authenticate-actor-handler.js',
      'Server/Api/Handlers/create-match-handler.js',
      'Server/Api/main.js',
      'Server/Play/EntrySpot/Handlers/join-match-handler.js',
      'Server/Play/EntrySpot/tictactoe-entry-spot.js',
      'Server/Play/GameSpots/Handlers/place-mark-handler.js',
      'Server/Play/GameSpots/tictactoe-match-directory.js',
      'Server/Play/GameSpots/tictactoe-match-room.js',
      'Server/Play/Handlers/create-match-room-handler.js',
      'Server/Play/Handlers/ensure-player-actor-handler.js',
      'Server/Play/main.js',
      'Server/Registry/main.js',
      'Server/Session/Sessions/Handlers/authenticate-session-packet-handler.js',
      'Server/Session/Sessions/Handlers/create-match-session-packet-handler.js',
      'Server/Session/Sessions/Handlers/place-mark-session-packet-handler.js',
      'Server/Session/Sessions/session-relay-session.js',
      'Server/Session/main.js',
      'Shared/Configuration/sample-names.js',
      'Shared/Actors/player-actor.js',
      'Shared/Contracts/messages.js',
      'Shared/Contracts/round.js'
    ],
    Bingo: [
      'Client/bingo-client-app.js',
      'Client/bingo-notification-inbox.js',
      'Client/bingo-player-client.js',
      'Client/self-check.js',
      'Server/Api/Handlers/authenticate-player-handler.js',
      'Server/Api/Handlers/match-bingo-handler.js',
      'Server/Api/main.js',
      'Server/Play/Actors/player-actor.js',
      'Server/Play/Actors/player-actor-factory.js',
      'Server/Play/BingoRoomSpots/Handlers/bingo-room-join-handler.js',
      'Server/Play/BingoRoomSpots/Handlers/bingo-room-timer-handler.js',
      'Server/Play/BingoRoomSpots/Handlers/start-bingo-game-handler.js',
      'Server/Play/BingoRoomSpots/bingo-card.js',
      'Server/Play/BingoRoomSpots/bingo-notification-publisher.js',
      'Server/Play/BingoRoomSpots/bingo-room-models.js',
      'Server/Play/BingoRoomSpots/bingo-room-spot.js',
      'Server/Play/EntrySpot/Handlers/match-bingo-actor-handler.js',
      'Server/Play/EntrySpot/bingo-entry-spot.js',
      'Server/Play/Handlers/allocate-bingo-room-handler.js',
      'Server/Play/Handlers/bingo-room-directory.js',
      'Server/Play/Handlers/bingo-notifications-handler.js',
      'Server/Play/Handlers/ensure-player-actor-handler.js',
      'Server/Play/Handlers/match-bingo-channel-handler.js',
      'Server/Play/Handlers/start-bingo-game-channel-handler.js',
      'Server/Play/main.js',
      'Server/Registry/main.js',
      'Server/Session/Sessions/Handlers/authenticate-session-handler.js',
      'Server/Session/Sessions/bingo-session.js',
      'Server/Session/main.js',
      'Shared/Configuration/sample-names.js',
      'Shared/Contracts/messages.js'
    ],
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
  for (const sample of ['TicTacToe', 'TicTacToe.SessionGateway', 'Bingo', 'Bingo.Ts']) {
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
    ['TicTacToe/Server/Api/main.js', 'TicTacToeApiModule'],
    ['TicTacToe/Server/Play/main.js', 'TicTacToePlayModule'],
    ['TicTacToe.SessionGateway/Server/Api/main.js', 'TicTacToeSessionGatewayApiModule'],
    ['TicTacToe.SessionGateway/Server/Play/main.js', 'TicTacToeSessionGatewayPlayModule'],
    ['TicTacToe.SessionGateway/Server/Registry/main.js', 'TicTacToeSessionGatewayRegistryModule'],
    ['TicTacToe.SessionGateway/Server/Session/main.js', 'TicTacToeSessionGatewaySessionModule'],
    ['Bingo/Server/Api/main.js', 'BingoApiModule'],
    ['Bingo/Server/Play/main.js', 'BingoPlayModule'],
    ['Bingo/Server/Registry/main.js', 'BingoRegistryModule'],
    ['Bingo/Server/Session/main.js', 'BingoSessionModule'],
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
    ['TicTacToe', 'Server/Api/main.js', 'TICTACTOE_API_ENDPOINT'],
    ['TicTacToe', 'Server/Play/main.js', 'TICTACTOE_PLAY_ENDPOINT'],
    ['TicTacToe.SessionGateway', 'Server/Session/main.js', 'TICTACTOE_SG_SESSION_ENDPOINT'],
    ['TicTacToe.SessionGateway', 'Server/Api/main.js', 'TICTACTOE_SG_API_ENDPOINT'],
    ['TicTacToe.SessionGateway', 'Server/Play/main.js', 'TICTACTOE_SG_PLAY_ENDPOINT'],
    ['TicTacToe.SessionGateway', 'Server/Registry/main.js', 'TICTACTOE_SG_REGISTRY_ENDPOINT'],
    ['Bingo', 'Server/Api/main.js', 'BINGO_API_ENDPOINT'],
    ['Bingo', 'Server/Play/main.js', 'BINGO_PLAY_ENDPOINT'],
    ['Bingo', 'Server/Session/main.js', 'BINGO_SESSION_ENDPOINT'],
    ['Bingo', 'Server/Registry/main.js', 'BINGO_REGISTRY_ENDPOINT'],
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

  for (const sample of ['TicTacToe.SessionGateway', 'Bingo']) {
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

test('TicTacToe sample covers separated api play roles timer and push notifications', () => {
  const client = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Client', 'self-check.js'), 'utf8');
  const api = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Api', 'main.js'), 'utf8');
  const play = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Play', 'main.js'), 'utf8');
  const authenticate = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Api', 'Handlers', 'authenticate-player-handler.js'), 'utf8');
  const apiHandler = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Api', 'Handlers', 'create-game-http-handler.js'), 'utf8');
  const createGame = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Play', 'Handlers', 'create-game-handler.js'), 'utf8');
  const playSession = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Play', 'Sessions', 'play-session.js'), 'utf8');
  const actorFactory = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Play', 'Actors', 'play-actor-factory.js'), 'utf8');
  const entrySpot = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Play', 'EntrySpot', 'play-entry-spot.js'), 'utf8');
  const entryJoin = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Play', 'EntrySpot', 'Handlers', 'play-actor-join-game-handler.js'), 'utf8');
  const placeMark = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Play', 'GameSpots', 'Handlers', 'play-actor-place-mark-handler.js'), 'utf8');
  const contracts = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Shared', 'Contracts', 'messages.js'), 'utf8');
  const required = [
    [client, '../Server/Api/main.js'],
    [client, '../Server/Play/main.js'],
    [client, 'createGame(apiHttpEndpoint'],
    [client, 'zlinkStreamConnectorFactory.create'],
    [client, 'PacketNames.authenticateReq'],
    [client, 'PacketNames.joinGameReq'],
    [client, 'PacketNames.placeMarkReq'],
    [client, 'PacketNames.playerJoinedNotify'],
    [client, 'PacketNames.gameStateNotify'],
    [client, "assert.equal(moves.at(-1).state.board, 'XXXOO....')"],
    [client, "assert.equal(moves.at(-1).state.winner, 'p1')"],
    [api, "request.url !== '/games'"],
    [api, 'PacketNames.authenticatePlayerReq'],
    [api, 'TICTACTOE_API_HTTP_ENDPOINT'],
    [apiHandler, 'PacketNames.createGame'],
    [authenticate, 'accessToken is required'],
    [play, 'net.createServer'],
    [play, 'ZlinkStreamFrameCodec.decode'],
    [play, 'SampleNames.playChannel'],
    [play, 'PacketNames.createGame'],
    [play, 'TICTACTOE_PLAY_STREAM_ENDPOINT'],
    [createGame, 'timerHandler.register(room)'],
    [playSession, 'PacketNames.authenticateReq'],
    [playSession, 'PacketNames.joinGameReq'],
    [playSession, 'PacketNames.placeMarkReq'],
    [playSession, 'flushAllNotifications'],
    [actorFactory, 'new PlayActor'],
    [entrySpot, 'class PlayEntrySpot'],
    [entryJoin, 'PacketNames.playerJoinedNotify'],
    [placeMark, 'gameStateNotify'],
    [contracts, 'authenticatePlayerReq'],
    [contracts, 'createGameHttpReq'],
    [contracts, 'placeMarkRes']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);
  const banned = [
    [client, 'RunTicTacToe'],
    [api, 'RunTicTacToe'],
    [play, 'RunTicTacToe']
  ].filter(([content, text]) => content.includes(text)).map(([, text]) => text);

  assert.deepEqual(missing, []);
  assert.deepEqual(banned, []);
});

test('TicTacToe SessionGateway sample covers reconnect two-actor round and bound push', () => {
  const client = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Client', 'self-check.js'), 'utf8');
  const api = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Api', 'main.js'), 'utf8');
  const apiAuth = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Api', 'Handlers', 'authenticate-actor-handler.js'), 'utf8');
  const apiCreate = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Api', 'Handlers', 'create-match-handler.js'), 'utf8');
  const play = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Play', 'main.js'), 'utf8');
  const actorManager = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Play', 'session-gateway-actor-manager.js'), 'utf8');
  const ensureActor = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Play', 'Handlers', 'ensure-player-actor-handler.js'), 'utf8');
  const createRoom = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Play', 'Handlers', 'create-match-room-handler.js'), 'utf8');
  const joinMatch = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Play', 'EntrySpot', 'Handlers', 'join-match-handler.js'), 'utf8');
  const placeMark = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Play', 'GameSpots', 'Handlers', 'place-mark-handler.js'), 'utf8');
  const session = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Session', 'main.js'), 'utf8');
  const authenticateSession = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Session', 'Sessions', 'Handlers', 'authenticate-session-packet-handler.js'), 'utf8');
  const createSession = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Session', 'Sessions', 'Handlers', 'create-match-session-packet-handler.js'), 'utf8');
  const placeSession = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Session', 'Sessions', 'Handlers', 'place-mark-session-packet-handler.js'), 'utf8');
  const round = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Shared', 'Contracts', 'round.js'), 'utf8');
  const messages = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Shared', 'Contracts', 'messages.js'), 'utf8');
  const actor = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Shared', 'Actors', 'player-actor.js'), 'utf8');
  const readme = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'README.ko.md'), 'utf8');
  const required = [
    [client, 'PacketNames.authenticateSessionReq'],
    [client, 'PacketNames.createMatchSessionReq'],
    [client, 'PacketNames.joinMatchReq'],
    [client, 'PacketNames.placeMarkSessionReq'],
    [client, "assert.equal(moves.at(-1).state.board, 'XXXOO....')"],
    [client, "assert.equal(moves.at(-1).state.winnerActorId, 'p1')"],
    [client, 'pushedByPacket.OpponentJoinedNotify'],
    [client, 'pushedByPacket.TurnChangedNotify'],
    [client, 'pushedByPacket.GameEndedNotify'],
    [api, 'AuthenticateActorHandler'],
    [api, 'CreateMatchHandler'],
    [api, 'ZLinkModule.forRoot'],
    [api, 'ZLINK_ROUTE_CLIENT'],
    [api, 'manualConnections: [process.env.TICTACTOE_SG_PLAY_ENDPOINT]'],
    [apiAuth, 'accessToken is required'],
    [apiCreate, 'PacketNames.createMatchReq'],
    [play, 'SampleBoundSessionRuntime'],
    [play, 'SessionGatewayActorManager'],
    [play, 'ZLinkModule.forRoot'],
    [actorManager, 'DefaultZLinkActorManager'],
    [actorManager, 'Inject(SampleBoundSessionRuntime)'],
    [play, 'PacketNames.ensurePlayerActorReq'],
    [play, 'PacketNames.createMatchReq'],
    [play, 'PacketNames.joinMatchReq'],
    [play, 'PacketNames.placeMarkReq'],
    [play, 'PacketNames.notificationsReq'],
    [ensureActor, 'this.boundSessions.bind'],
    [createRoom, 'this.matches.create'],
    [joinMatch, 'this.actorManager.getOrCreate'],
    [placeMark, 'room.place'],
    [session, 'AuthenticateSessionPacketHandler'],
    [session, 'CreateMatchSessionPacketHandler'],
    [session, 'PlaceMarkSessionPacketHandler'],
    [session, 'ZLinkModule.forRoot'],
    [session, 'ZLINK_ROUTE_CLIENT'],
    [session, 'process.env.TICTACTOE_SG_API_ENDPOINT'],
    [session, 'process.env.TICTACTOE_SG_PLAY_ENDPOINT'],
    [authenticateSession, 'PacketNames.authenticateActorReq'],
    [authenticateSession, 'PacketNames.ensurePlayerActorReq'],
    [createSession, 'PacketNames.createMatchReq'],
    [createSession, 'PacketNames.joinMatchReq'],
    [placeSession, 'PacketNames.placeMarkReq'],
    [messages, 'authenticateSessionReq'],
    [messages, 'createMatchSessionReq'],
    [messages, 'placeMarkSessionReq'],
    [round, 'class SessionGatewayRound'],
    [actor, 'notifyOpponentJoined'],
    [actor, 'notifyTurnChanged'],
    [actor, 'notifyGameEnded'],
    [actor, "packetName('TurnChangedNotify')"],
    [actor, "packetName('GameEndedNotify')"],
    [readme, '`p1`, `p2` 두 actor'],
    [readme, '`XXXOO....`'],
    [readme, 'winner 는 `p1`']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);
  const banned = [
    [client, 'RunSessionGateway'],
    [session, 'RunSessionGateway'],
    [session, 'runScenario']
  ].filter(([content, text]) => content.includes(text)).map(([, text]) => text);

  assert.deepEqual(missing, []);
  assert.deepEqual(banned, []);
});

test('Bingo sample covers four-player host start guards timer draws and bound push fanout', () => {
  const client = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Client', 'self-check.js'), 'utf8');
  const clientApp = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Client', 'bingo-client-app.js'), 'utf8');
  const playerClient = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Client', 'bingo-player-client.js'), 'utf8');
  const inbox = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Client', 'bingo-notification-inbox.js'), 'utf8');
  const apiMain = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Api', 'main.js'), 'utf8');
  const authenticate = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Api', 'Handlers', 'authenticate-player-handler.js'), 'utf8');
  const match = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Api', 'Handlers', 'match-bingo-handler.js'), 'utf8');
  const sessionMain = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Session', 'main.js'), 'utf8');
  const session = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Session', 'Sessions', 'bingo-session.js'), 'utf8');
  const authenticateSession = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Session', 'Sessions', 'Handlers', 'authenticate-session-handler.js'), 'utf8');
  const roomMain = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Play', 'main.js'), 'utf8');
  const room = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Play', 'Handlers', 'allocate-bingo-room-handler.js'), 'utf8');
  const ensureActor = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Play', 'Handlers', 'ensure-player-actor-handler.js'), 'utf8');
  const notificationHandler = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Play', 'Handlers', 'bingo-notifications-handler.js'), 'utf8');
  const actorFactory = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Play', 'Actors', 'player-actor-factory.js'), 'utf8');
  const roomSpot = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Play', 'BingoRoomSpots', 'bingo-room-spot.js'), 'utf8');
  const notifications = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Play', 'BingoRoomSpots', 'bingo-notification-publisher.js'), 'utf8');
  const timer = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Play', 'BingoRoomSpots', 'Handlers', 'bingo-room-timer-handler.js'), 'utf8');
  const readme = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'README.ko.md'), 'utf8');
  const required = [
    [client, 'new BingoClientApp().run'],
    [client, "assert.equal(result.ended.status, 'Finished')"],
    [client, "assert.deepEqual(result.ended.winners, ['player-1', 'player-3'])"],
    [clientApp, 'earlyHostStartRejected'],
    [clientApp, 'nonHostStartRejected'],
    [clientApp, 'waitForEnded'],
    [clientApp, 'startedPushCounts.every'],
    [clientApp, 'drawnPushCounts.every'],
    [clientApp, 'endedPushCounts.every'],
    [clientApp, 'createRouteClient'],
    [clientApp, 'sessionEndpoint'],
    [playerClient, "sessionClient.request('session-server', 'AuthenticateReq'"],
    [playerClient, "sessionClient.request('session-server', 'MatchBingoReq'"],
    [playerClient, "sessionClient.request('session-server', 'StartBingoGameReq'"],
    [playerClient, "sessionClient.request('session-server', 'BingoNotificationsReq'"],
    [inbox, 'PlayerJoinedNotify'],
    [inbox, 'BingoGameStartedNotify'],
    [inbox, 'BingoNumberDrawnNotify'],
    [inbox, 'BingoGameEndedNotify'],
    [apiMain, 'async function bootstrap'],
    [apiMain, 'AuthenticatePlayerHandler'],
    [apiMain, 'MatchBingoHandler'],
    [apiMain, "require('@nestjs/common')"],
    [apiMain, "require('@nestjs/core')"],
    [apiMain, 'ZLinkModule.forRoot'],
    [apiMain, 'clientServerChannels'],
    [apiMain, 'NestFactory.createApplicationContext'],
    [apiMain, "client: { manualConnections: [process.env.BINGO_PLAY_ENDPOINT] }"],
    [apiMain, "'bingo.play'"],
    [apiMain, "'bingo.api'"],
    [apiMain, "handlerGroups: ['api']"],
    [apiMain, 'zlinkHandlerGroup'],
    [apiMain, "'AuthenticatePlayerReq'"],
    [apiMain, "'MatchBingoApiReq'"],
    [apiMain, 'waitForShutdown'],
    [apiMain, 'closeNestRuntime'],
    [authenticate, 'Access token must be a sample player id.'],
    [authenticate, 'accepted: true'],
    [match, "this.zlinkClient"],
    [match, ".requestToChannel('bingo.play'"],
    [match, "mode: request.mode ?? 'four-player'"],
    [match, ".packetName('AllocateBingoRoom')"],
    [match, '.timeout(10000)'],
    [match, '.submit()'],
    [sessionMain, 'AuthenticateSessionHandler'],
    [sessionMain, 'clientServerChannels'],
    [sessionMain, 'routerMeshes'],
    [sessionMain, 'SESSION_CONTEXTS'],
    [sessionMain, "'bingo.api'"],
    [sessionMain, "'bingo.play'"],
    [sessionMain, 'ZLINK_CHANNEL_CLIENT'],
    [sessionMain, "'MatchBingoReq'"],
    [sessionMain, "'StartBingoGameReq'"],
    [sessionMain, "'BingoNotificationsReq'"],
    [sessionMain, 'relayToPlay'],
    [sessionMain, 'providers: ['],
    [authenticateSession, 'Inject(ZLINK_CHANNEL_CLIENT)'],
    [session, 'class BingoSession'],
    [session, 'requireSingleBoundActor'],
    [authenticateSession, ".packetName('AuthenticatePlayerReq')"],
    [authenticateSession, ".packetName('EnsurePlayerActorReq')"],
    [roomMain, 'async function bootstrap'],
    [roomMain, 'clientServerChannels'],
    [roomMain, 'AllocateBingoRoomHandler'],
    [roomMain, 'EnsurePlayerActorHandler'],
    [roomMain, 'MatchBingoChannelHandler'],
    [roomMain, 'StartBingoGameChannelHandler'],
    [roomMain, 'SampleBoundSessionRuntime'],
    [roomMain, 'BingoNotificationsHandler'],
    [roomMain, 'providers: ['],
    [roomMain, 'zlinkHandlerGroup'],
    [roomMain, "'AllocateBingoRoom'"],
    [roomMain, "'EnsurePlayerActorReq'"],
    [roomMain, "'MatchBingoReq'"],
    [roomMain, "'StartBingoGameReq'"],
    [roomMain, "'BingoNotificationsReq'"],
    [actorFactory, 'Inject(SampleBoundSessionRuntime)'],
    [notificationHandler, 'Inject(SampleBoundSessionRuntime)'],
    [ensureActor, 'Inject(PlayerActorFactory)'],
    [roomMain, "channelName: 'bingo.play'"],
    [roomMain, "handlerGroups: ['play']"],
    [notificationHandler, 'this.boundSessions.deliveredFor'],
    [actorFactory, 'boundSessions.bind'],
    [roomSpot, 'Bingo requires four players before start.'],
    [roomSpot, 'Only the room host can start Bingo.'],
    [roomSpot, 'runTimerDraws'],
    [roomSpot, 'this.winners.push'],
    [notifications, 'boundSession'],
    [timer, 'runTimerDraws'],
    [readme, '네 player 가 서로 다른 actor 로 bind 되고 같은 room 에 match 된다'],
    [readme, 'host start 요청과 non-host start 요청은 거부된다'],
    [readme, 'timer draw 뒤 같은 draw sequence 에서 `player-1`, `player-3` 이 winner 가 된다']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);

  assert.deepEqual(missing, []);
  assert.equal(clientApp.includes('createChannelClient'), false);
  assert.equal(clientApp.includes("playClient.request('"), false);
  assert.equal(clientApp.includes('RunBingoRoomTimerReq'), false);
  assert.equal(clientApp.includes('BingoDeliveredNotificationsReq'), false);
  assert.equal(apiMain.includes('createRouteClient'), false);
  assert.equal(apiMain.includes('createChannelClient'), false);
  assert.equal(apiMain.includes('beforeReady'), false);
  assert.equal(apiMain.includes("playClient.request('Ping'"), false);
  assert.equal(/const\s+\w+\s*=\s*new\s+\w+Handler/.test(apiMain), false);
  assert.equal(/const\s+\w+\s*=\s*new\s+\w+Handler/.test(sessionMain), false);
  assert.equal(client.includes('createRouteClient'), false);
  assert.equal(playerClient.includes('apiClient.request('), false);
  assert.equal(playerClient.includes('playClient.request('), false);
  assert.equal(client.includes("'RunBingo'"), false);
  assert.equal(match.includes('this.playClient'), false);
  assert.equal(match.includes("'RunBingoRoom'"), false);
  assert.equal(match.includes('players:'), false);
  assert.equal(match.includes('draws:'), false);
  assert.equal(roomMain.includes('RunBingoRoomTimerReq'), false);
  assert.equal(roomMain.includes("packetName: 'Ping'"), false);
  assert.equal(authenticate.includes('function descriptor()'), false);
  assert.equal(match.includes('function descriptor()'), false);
  assert.equal(/const\s+\w+\s*=\s*new\s+\w+(Handler|Directory|Publisher|Factory|Spot)/.test(roomMain), false);
  assert.equal(apiMain.includes("packetName: 'RunBingo'"), false);
  assert.equal(roomMain.includes('const requiredPlayers'), false);
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
