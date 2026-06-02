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
  'Bingo'
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
      'Server/Api/Handlers/create-game-http-handler.js',
      'Server/Api/main.js',
      'Server/Play/EntrySpot/Handlers/play-actor-join-game-handler.js',
      'Server/Play/GameSpots/Handlers/play-actor-place-mark-handler.js',
      'Server/Play/GameSpots/Handlers/tictactoe-game-join-handler.js',
      'Server/Play/GameSpots/Handlers/tictactoe-game-timer-handler.js',
      'Server/Play/Handlers/create-game-handler.js',
      'Server/Play/main.js',
      'Shared/Contracts/messages.js'
    ],
    'TicTacToe.SessionGateway': [
      'Client/self-check.js',
      'Server/Api/main.js',
      'Server/Play/main.js',
      'Server/Registry/main.js',
      'Server/Session/main.js',
      'Shared/Actors/player-actor.js',
      'Shared/Contracts/round.js'
    ],
    Bingo: [
      'Client/self-check.js',
      'Server/Api/Handlers/authenticate-player-handler.js',
      'Server/Api/Handlers/match-bingo-handler.js',
      'Server/Api/api-server-host-factory.js',
      'Server/Api/main.js',
      'Server/Play/Handlers/allocate-bingo-room-handler.js',
      'Server/Play/play-server-host-factory.js',
      'Server/Play/main.js',
      'Server/Registry/registry-host-factory.js',
      'Server/Registry/main.js',
      'Server/Session/session-server-host-factory.js',
      'Server/Session/main.js',
      'Shared/Contracts/bingo-card.js'
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

test('node framework samples exercise the NestJS module surface', () => {
  const missing = [];
  for (const sample of ['TicTacToe', 'TicTacToe.SessionGateway', 'Bingo']) {
    const usesNestModule = sampleSourceFiles(path.join(samplesRoot, sample))
      .some((file) => fs.readFileSync(file, 'utf8').includes('packages/nestjs/dist'));
    if (!usesNestModule) {
      missing.push(sample);
    }
  }

  assert.deepEqual(missing, []);
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
    ['Bingo', 'Server/Registry/main.js', 'BINGO_REGISTRY_ENDPOINT']
  ];

  for (const [sample, serverRelative, endpointEnv] of cases) {
    const serverEntry = path.join(samplesRoot, sample, serverRelative);
    const clientEntry = path.join(samplesRoot, sample, 'Client', 'self-check.js');
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

test('node samples do not hide readiness with sleep calls', () => {
  const violations = [];
  for (const file of sampleSourceFiles(samplesRoot)) {
    const content = fs.readFileSync(file, 'utf8');
    if (/\bsleep\s*\(|setTimeout\s*\(/.test(content)) {
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
  const apiHandler = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Api', 'Handlers', 'create-game-http-handler.js'), 'utf8');
  const createGame = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Play', 'Handlers', 'create-game-handler.js'), 'utf8');
  const entryJoin = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Play', 'EntrySpot', 'Handlers', 'play-actor-join-game-handler.js'), 'utf8');
  const placeMark = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'Server', 'Play', 'GameSpots', 'Handlers', 'play-actor-place-mark-handler.js'), 'utf8');
  const required = [
    [client, '../Server/Api/main.js'],
    [client, '../Server/Play/main.js'],
    [client, "createChannelClient"],
    [client, "client.request('RunTicTacToe'"],
    [client, 'PlayerJoinedNotify'],
    [client, 'GameStateNotify'],
    [client, 'timerRegistered'],
    [api, "channelName: 'api'"],
    [api, "channelName: 'play'"],
    [api, "packetName: 'RunTicTacToe'"],
    [apiHandler, "client.request('CreateGame'"],
    [play, "channelName: 'play'"],
    [play, "packetName: 'CreateGame'"],
    [createGame, 'timerRegistered'],
    [entryJoin, 'PlayerJoinedNotify'],
    [placeMark, 'winner']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);

  assert.deepEqual(missing, []);
});

test('TicTacToe SessionGateway sample covers reconnect two-actor round and bound push', () => {
  const client = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Client', 'self-check.js'), 'utf8');
  const session = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Server', 'Session', 'main.js'), 'utf8');
  const round = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Shared', 'Contracts', 'round.js'), 'utf8');
  const actor = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'Shared', 'Actors', 'player-actor.js'), 'utf8');
  const readme = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'README.ko.md'), 'utf8');
  const required = [
    [session, "bind(gateway, 'p2', 'session-o', 1)"],
    [session, "new SessionGatewayRound('match-1', second, opponent)"],
    [session, "await round.place('p1', 0)"],
    [session, "await round.place('p2', 4)"],
    [client, "assert.equal(result.finalState.board, 'XXXOO....')"],
    [client, "assert.equal(result.finalState.winnerActorId, 'p1')"],
    [client, 'pushedByPacket.OpponentJoinedNotify'],
    [client, 'pushedByPacket.TurnChangedNotify'],
    [client, 'pushedByPacket.GameEndedNotify'],
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

  assert.deepEqual(missing, []);
});

test('Bingo sample covers api to play channel room allocation flow', () => {
  const client = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Client', 'self-check.js'), 'utf8');
  const apiMain = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Api', 'main.js'), 'utf8');
  const apiFactory = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Api', 'api-server-host-factory.js'), 'utf8');
  const authenticate = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Api', 'Handlers', 'authenticate-player-handler.js'), 'utf8');
  const match = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Api', 'Handlers', 'match-bingo-handler.js'), 'utf8');
  const roomMain = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Play', 'main.js'), 'utf8');
  const playFactory = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Play', 'play-server-host-factory.js'), 'utf8');
  const room = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'Server', 'Play', 'Handlers', 'allocate-bingo-room-handler.js'), 'utf8');
  const readme = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'README.ko.md'), 'utf8');
  const required = [
    [apiMain, 'buildApiServerHost'],
    [apiFactory, 'AuthenticatePlayerHandler'],
    [apiFactory, 'MatchBingoHandler'],
    [apiFactory, 'createChannelClient'],
    [apiFactory, 'startChannelServer'],
    [apiFactory, "channelName: 'bingo.play'"],
    [apiFactory, "channelName: 'bingo.api'"],
    [apiFactory, "handlerGroups: ['api']"],
    [apiFactory, "packetName: 'AuthenticatePlayerReq'"],
    [apiFactory, "packetName: 'MatchBingoApiReq'"],
    [authenticate, 'Access token must be a sample player id.'],
    [authenticate, "ZLinkHandlerGroup('api')"],
    [authenticate, "ZLinkRequest('AuthenticatePlayerReq')"],
    [authenticate, 'accepted: true'],
    [match, "this.playClient"],
    [match, "ZLinkHandlerGroup('api')"],
    [match, "ZLinkRequest('MatchBingoApiReq')"],
    [match, ".requestToChannel('bingo.play'"],
    [match, "mode: request.mode ?? 'four-player'"],
    [match, ".packetName('AllocateBingoRoom')"],
    [match, '.timeout(10000)'],
    [match, '.submit()'],
    [roomMain, 'buildPlayServerHost'],
    [playFactory, 'AllocateBingoRoomHandler'],
    [playFactory, "channelName: 'bingo.play'"],
    [playFactory, "handlerGroups: ['play']"],
    [playFactory, "packetName: 'AllocateBingoRoom'"],
    [room, "ZLinkHandlerGroup('play')"],
    [room, "ZLinkRequest('AllocateBingoRoom')"],
    [room, 'const requiredPlayers = 4'],
    [room, 'this.reservedSeats >= requiredPlayers'],
    [room, 'return { roomId: this.currentRoomId }'],
    [client, "createChannelClient"],
    [client, "channelName: 'bingo.api'"],
    [client, "client.request('AuthenticatePlayerReq'"],
    [client, "client.request('MatchBingoApiReq'"],
    [client, "assert.equal(first.roomId, 'bingo-room-001')"],
    [client, "assert.equal(second.roomId, 'bingo-room-001')"],
    [readme, 'request payload 는 `mode` 만 포함한다'],
    [readme, 'Play channel 로 `AllocateBingoRoom` request 를 보낸다']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);

  assert.deepEqual(missing, []);
  assert.equal(apiFactory.includes('createRouteClient'), false);
  assert.equal(client.includes('createRouteClient'), false);
  assert.equal(client.includes("'RunBingo'"), false);
  assert.equal(match.includes('this.playClient.request('), false);
  assert.equal(match.includes("'RunBingoRoom'"), false);
  assert.equal(match.includes('players:'), false);
  assert.equal(match.includes('draws:'), false);
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
