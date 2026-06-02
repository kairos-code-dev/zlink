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

test('node topology samples mirror dotnet role layout using Node naming style', () => {
  const expected = {
    StreamingClient: ['client/self-check.js', 'server/main.js'],
    TicTacToe: [
      'client/self-check.js',
      'server/api/handlers/create-game-http-handler.js',
      'server/api/main.js',
      'server/play/entry-spot/handlers/play-actor-join-game-handler.js',
      'server/play/game-spots/handlers/play-actor-place-mark-handler.js',
      'server/play/game-spots/handlers/tictactoe-game-join-handler.js',
      'server/play/game-spots/handlers/tictactoe-game-timer-handler.js',
      'server/play/handlers/create-game-handler.js',
      'server/play/main.js',
      'shared/contracts/messages.js'
    ],
    'TicTacToe.SessionGateway': [
      'client/self-check.js',
      'server/api/main.js',
      'server/play/main.js',
      'server/registry/main.js',
      'server/session/main.js',
      'shared/actors/player-actor.js',
      'shared/contracts/round.js'
    ],
    Bingo: [
      'client/self-check.js',
      'server/api/main.js',
      'server/play/main.js',
      'server/registry/main.js',
      'server/session/main.js',
      'shared/contracts/bingo-card.js'
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
    ['StreamingClient', 'server/main.js', 'STREAMING_CLIENT_ENDPOINT'],
    ['TicTacToe', 'server/api/main.js', 'TICTACTOE_API_ENDPOINT'],
    ['TicTacToe', 'server/play/main.js', 'TICTACTOE_PLAY_ENDPOINT'],
    ['TicTacToe.SessionGateway', 'server/session/main.js', 'TICTACTOE_SG_SESSION_ENDPOINT'],
    ['TicTacToe.SessionGateway', 'server/api/main.js', 'TICTACTOE_SG_API_ENDPOINT'],
    ['TicTacToe.SessionGateway', 'server/play/main.js', 'TICTACTOE_SG_PLAY_ENDPOINT'],
    ['TicTacToe.SessionGateway', 'server/registry/main.js', 'TICTACTOE_SG_REGISTRY_ENDPOINT'],
    ['Bingo', 'server/api/main.js', 'BINGO_API_ENDPOINT'],
    ['Bingo', 'server/play/main.js', 'BINGO_PLAY_ENDPOINT'],
    ['Bingo', 'server/session/main.js', 'BINGO_SESSION_ENDPOINT'],
    ['Bingo', 'server/registry/main.js', 'BINGO_REGISTRY_ENDPOINT']
  ];

  for (const [sample, serverRelative, endpointEnv] of cases) {
    const serverEntry = path.join(samplesRoot, sample, serverRelative);
    const clientEntry = path.join(samplesRoot, sample, 'client', 'self-check.js');
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
  const sample = fs.readFileSync(path.join(samplesRoot, 'StreamingClient', 'client', 'self-check.js'), 'utf8');
  const server = fs.readFileSync(path.join(samplesRoot, 'StreamingClient', 'server', 'main.js'), 'utf8');
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
  const client = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'client', 'self-check.js'), 'utf8');
  const api = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'server', 'api', 'main.js'), 'utf8');
  const play = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'server', 'play', 'main.js'), 'utf8');
  const apiHandler = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'server', 'api', 'handlers', 'create-game-http-handler.js'), 'utf8');
  const createGame = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'server', 'play', 'handlers', 'create-game-handler.js'), 'utf8');
  const entryJoin = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'server', 'play', 'entry-spot', 'handlers', 'play-actor-join-game-handler.js'), 'utf8');
  const placeMark = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'server', 'play', 'game-spots', 'handlers', 'play-actor-place-mark-handler.js'), 'utf8');
  const required = [
    [client, '../server/api/main.js'],
    [client, '../server/play/main.js'],
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
  const client = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'client', 'self-check.js'), 'utf8');
  const session = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'server', 'session', 'main.js'), 'utf8');
  const round = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'shared', 'contracts', 'round.js'), 'utf8');
  const actor = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.SessionGateway', 'shared', 'actors', 'player-actor.js'), 'utf8');
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

test('Bingo sample covers four-player host start guards and bound push fanout', () => {
  const client = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'client', 'self-check.js'), 'utf8');
  const api = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'server', 'api', 'main.js'), 'utf8');
  const room = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'server', 'play', 'main.js'), 'utf8');
  const readme = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'README.ko.md'), 'utf8');
  const required = [
    [api, "{ actorId: 'p1', numbers: [7] }"],
    [api, "{ actorId: 'p4', numbers: [11] }"],
    [room, 'const requiredPlayers = 4'],
    [room, 'earlyHostStartRejected'],
    [room, 'nonHostStartRejected'],
    [room, "'BingoGameStarted'"],
    [room, "'BingoNumberDrawn'"],
    [room, "'BingoGameEnded'"],
    [room, 'hostActorId'],
    [client, "assert.deepEqual(result.room.winners, ['p1', 'p3'])"],
    [client, "message.packetName === 'BingoGameStarted'"],
    [client, "message.packetName === 'BingoNumberDrawn'"],
    [client, "message.packetName === 'BingoGameEnded'"],
    [readme, '네 player'],
    [readme, 'non-host start 요청은 거부된다'],
    [readme, '`p1`, `p3`']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);

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
