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

test('node topology samples run server roles as separate processes', () => {
  const cases = [
    ['TicTacToe', 'server/main.js', 'server/main.js'],
    ['TicTacToe', 'api-server/main.js', 'api-server/main.js'],
    ['TicTacToe', 'play-server/main.js', 'play-server/main.js'],
    ['TicTacToe.SessionGateway', 'session-server/main.js', 'session-server/main.js'],
    ['TicTacToe.SessionGateway', 'api-server/main.js', 'api-server/main.js'],
    ['TicTacToe.SessionGateway', 'play-server/main.js', 'play-server/main.js'],
    ['TicTacToe.SessionGateway', 'registry-server/main.js', 'registry-server/main.js'],
    ['Bingo', 'api-server/main.js', 'api-server/main.js'],
    ['Bingo', 'play-server/main.js', 'play-server/main.js'],
    ['Bingo', 'session-server/main.js', 'session-server/main.js'],
    ['Bingo', 'registry-server/main.js', 'registry-server/main.js']
  ];

  for (const [sample, serverRelative, clientReference] of cases) {
    const serverEntry = path.join(samplesRoot, sample, serverRelative);
    const clientEntry = path.join(samplesRoot, sample, 'client', 'self-check.js');
    const serverContent = fs.readFileSync(serverEntry, 'utf8');
    const clientContent = fs.readFileSync(clientEntry, 'utf8');

    assert.equal(fs.existsSync(serverEntry), true);
    assert.match(serverContent, /runRoleServer/);
    assert.match(clientContent, /(?:withRoleProcess|startRoleProcess)/);
    assert.match(clientContent, new RegExp(escapeRegExp(clientReference)));
  }
});

test('node topology samples keep role process protocol in the shared helper', () => {
  const protocolFiles = sampleSourceFiles(samplesRoot)
    .filter((file) => fs.readFileSync(file, 'utf8').includes('childProcess.spawn'));

  assert.deepEqual(
    protocolFiles.map((file) => path.relative(samplesRoot, file)),
    ['shared/role-process.js']
  );
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
  const sample = fs.readFileSync(path.join(samplesRoot, 'StreamingClient', 'src', 'self-check.js'), 'utf8');
  const required = [
    'ReconnectingInMemoryTransportFactory',
    'connectAttempts',
    'ZlinkStreamConnectionState.Reconnecting',
    'dispatchMode: connector.ZlinkStreamDispatchMode.Manual',
    ".request(json.toJson({ playerId: 'p1' }))",
    "client.on('ServerNotice'"
  ];
  const missing = required.filter((text) => !sample.includes(text));

  assert.deepEqual(missing, []);
});

test('TicTacToe sample covers separated api play roles timer and push notifications', () => {
  const client = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'client', 'self-check.js'), 'utf8');
  const server = fs.readFileSync(path.join(samplesRoot, 'TicTacToe', 'server', 'game-server.js'), 'utf8');
  const required = [
    [client, '../api-server/main.js'],
    [client, '../play-server/main.js'],
    [client, 'PlayerJoinedNotify'],
    [client, 'GameStateNotify'],
    [client, 'timerRegistered'],
    [server, "addTimer('turn-timeout'"],
    [server, 'class TurnTimeoutTimer'],
    [server, 'PlayerJoinedNotify'],
    [server, 'GameStateNotify']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);

  assert.deepEqual(missing, []);
});

test('Bingo sample covers four-player host start guards and bound push fanout', () => {
  const client = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'client', 'self-check.js'), 'utf8');
  const api = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'api-server', 'main.js'), 'utf8');
  const room = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'play-server', 'bingo-room.js'), 'utf8');
  const readme = fs.readFileSync(path.join(samplesRoot, 'Bingo', 'README.ko.md'), 'utf8');
  const required = [
    [api, "{ actorId: 'p1', numbers: [7] }"],
    [api, "{ actorId: 'p4', numbers: [11] }"],
    [api, 'earlyHostStartRejected'],
    [api, 'nonHostStartRejected'],
    [room, 'this.requiredPlayers = 4'],
    [room, "'BingoGameStarted'"],
    [room, "'BingoNumberDrawn'"],
    [room, "'BingoGameEnded'"],
    [room, 'hostActorId()'],
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
