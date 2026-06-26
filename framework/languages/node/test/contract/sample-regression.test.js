const assert = require('node:assert/strict');
const childProcess = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const workspaceRoot = path.resolve(__dirname, '..', '..');
const samplesRoot = path.join(workspaceRoot, 'samples');
const requiredSamples = [
  'TicTacToe.Ts',
  'Bingo.Ts',
  'DeliveryDispatch.Ts',
  'GameQuest.Ts',
  'ShoppingMall.Ts',
  'SupportChat.Ts'
];
const topologySamples = [
  'TicTacToe.Ts',
  'Bingo.Ts',
  'DeliveryDispatch.Ts',
  'GameQuest.Ts',
  'ShoppingMall.Ts',
  'SupportChat.Ts'
];

test('node samples define the required sample directories and README files', () => {
  const missing = [];
  for (const sample of requiredSamples) {
    for (const relative of ['package.json', 'README.ko.md', 'run_sample.sh', 'run_sample.ps1']) {
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
      'Client/tictactoe-client-scenario.ts',
      'Client/main.ts',
      'Server/Api/Handlers/authenticate-player-handler.ts',
      'Server/Api/Handlers/create-game-http-handler.ts',
      'Server/Api/main.ts',
      'Server/Play/Domain/TicTacToe/tictactoe-board.ts',
      'Server/Play/Domain/TicTacToe/tictactoe-match.ts',
      'Server/Play/Application/GameCreation/tictactoe-game-creator.ts',
      'Server/Play/Infrastructure/ZLink/Actors/play-actor.ts',
      'Server/Play/Infrastructure/ZLink/Actors/play-actor-factory.ts',
      'Server/Play/Infrastructure/ZLink/Handlers/create-game-handler.ts',
      'Server/Play/Infrastructure/ZLink/Sessions/play-session.ts',
      'Server/Play/Infrastructure/ZLink/Sessions/play-session-factory.ts',
      'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/Handlers/play-actor-join-game-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/Handlers/play-actor-place-mark-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/Handlers/tictactoe-game-timer-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/play-entry-spot.ts',
      'Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe-game-spot.ts',
      'Server/Play/main.ts',
      'Shared/Contracts/messages.ts'
    ],
    'Bingo.Ts': [
      'Client/bingo-client-scenario.ts',
      'Client/main.ts',
      'Server/Api/Handlers/authenticate-player-handler.ts',
      'Server/Api/Handlers/match-bingo-handler.ts',
      'Server/Api/main.ts',
      'Server/Play/Domain/Bingo/bingo-card.ts',
      'Server/Play/Domain/Bingo/bingo-game.ts',
      'Server/Play/Domain/Bingo/bingo-room-game.ts',
      'Server/Play/Domain/Bingo/bingo-room-models.ts',
      'Server/Play/Application/RoomAllocation/bingo-room-allocator.ts',
      'Server/Play/Infrastructure/ZLink/Actors/player-actor.ts',
      'Server/Play/Infrastructure/ZLink/Actors/player-actor-factory.ts',
      'Server/Play/Infrastructure/ZLink/Handlers/allocate-bingo-room-handler.ts',
      'Server/Play/Infrastructure/ZLink/Handlers/ensure-player-actor-handler.ts',
      'Server/Play/Infrastructure/ZLink/Handlers/match-bingo-channel-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/bingo-room-timer-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/Handlers/match-bingo-actor-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/Handlers/observe-bingo-events-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/stop-observing-bingo-events-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/submit-bingo-card-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/bingo-entry-spot.ts',
      'Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/bingo-room-spot.ts',
      'Server/Play/main.ts',
      'Server/Registry/main.ts',
      'Server/Registry/registry-server-host.ts',
      'Server/Session/Sessions/Handlers/authenticate-session-handler.ts',
      'Server/Session/Sessions/bingo-session.ts',
      'Server/Session/main.ts',
      'Server/Configuration/sample-names.ts',
      'Shared/Contracts/bingo_messages.proto',
      'Shared/Contracts/protobuf-codec.ts',
      'Shared/Contracts/messages.ts'
    ],
    'SupportChat.Ts': [
      'Client/supportchat-client-scenario.ts',
      'Client/main.ts',
      'Client/Configuration/sample-config.ts',
      'Client/Configuration/sample-names.ts',
      'Server/Api/Handlers/authenticate-user-handler.ts',
      'Server/Api/Handlers/open-conversation-handler.ts',
      'Server/Api/supportchat-api-module.ts',
      'Server/Api/main.ts',
      'Server/Support/Domain/SupportChat/conversation.ts',
      'Server/Support/Domain/SupportChat/conversation-models.ts',
      'Server/Support/Application/ConversationAssignment/support-conversation-allocator.ts',
      'Server/Support/Application/ConversationAssignment/agent-availability-directory.ts',
      'Server/Support/Application/ConversationAssignment/agent-assignment-service.ts',
      'Server/Support/Infrastructure/ZLink/Actors/support-user-actor.ts',
      'Server/Support/Infrastructure/ZLink/Actors/support-user-actor-factory.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/allocate-conversation-handler.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/assign-agent-handler.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/ensure-support-user-actor-handler.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/open-conversation-channel-handler.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/set-agent-available-channel-handler.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/join-conversation-channel-handler.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/send-chat-message-channel-handler.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/set-typing-channel-handler.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/close-conversation-channel-handler.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/supportchat-notifications-handler.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Notifications/conversation-event-mapper.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Notifications/support-notification-publisher.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/conversation-create-request.ts',
      'Server/Support/Infrastructure/ZLink/Spots/EntrySpot/support-entry-spot.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/conversation-spot.ts',
      'Server/Support/Infrastructure/ZLink/Spots/EntrySpot/Handlers/open-conversation-actor-handler.ts',
      'Server/Support/Infrastructure/ZLink/Spots/EntrySpot/Handlers/set-agent-available-handler.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Handlers/send-chat-message-handler.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Handlers/set-typing-handler.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Handlers/close-conversation-handler.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Handlers/conversation-idle-timer-handler.ts',
      'Server/Support/notification-delivery-log.ts',
      'Server/Support/supportchat-support-module.ts',
      'Server/Support/main.ts',
      'Server/Session/Sessions/Handlers/authenticate-session-handler.ts',
      'Server/Session/Sessions/supportchat-session.ts',
      'Server/Session/supportchat-session-module.ts',
      'Server/Session/main.ts',
      'Server/Registry/registry-server-host.ts',
      'Server/Registry/main.ts',
      'Server/Configuration/sample-config.ts',
      'Server/Configuration/sample-names.ts',
      'Server/runtime-support.ts',
      'Shared/Contracts/messages.ts'
    ],
    'DeliveryDispatch.Ts': [
      'Client/deliverydispatch-client-scenario.ts',
      'Client/main.ts',
      'Server/main.ts',
      'Shared/Configuration/sample-names.ts',
      'Shared/Contracts/messages.ts'
    ],
    'GameQuest.Ts': [
      'Client/gamequest-client-scenario.ts',
      'Client/main.ts',
      'Server/main.ts',
      'Shared/Configuration/sample-names.ts',
      'Shared/Contracts/messages.ts'
    ],
    'ShoppingMall.Ts': [
      'Client/shoppingmall-client-scenario.ts',
      'Client/main.ts',
      'Server/main.ts',
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

test('node Bingo and TicTacToe samples implement Entry Spot actor lifecycle flow', () => {
  const files = {
    bingoModule: readSample('Bingo.Ts', 'Server/Play/bingo-play-module.ts'),
    bingoEntry: readSample('Bingo.Ts', 'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/bingo-entry-spot.ts'),
    bingoRoom: readSample('Bingo.Ts', 'Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/bingo-room-spot.ts'),
    bingoAllocator: readSample('Bingo.Ts', 'Server/Play/Application/RoomAllocation/bingo-room-allocator.ts'),
    bingoMatch: readSample('Bingo.Ts', 'Server/Play/Infrastructure/ZLink/Handlers/match-bingo-channel-handler.ts'),
    ticTacToeModule: readSample('TicTacToe.Ts', 'Server/Play/tictactoe-play-module.ts'),
    ticTacToeEntry: readSample('TicTacToe.Ts', 'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/play-entry-spot.ts'),
    ticTacToeGame: readSample('TicTacToe.Ts', 'Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe-game-spot.ts'),
    ticTacToeCreate: readSample('TicTacToe.Ts', 'Server/Play/Application/GameCreation/tictactoe-game-creator.ts'),
    ticTacToeSession: readSample('TicTacToe.Ts', 'Server/Play/Infrastructure/ZLink/Sessions/play-session.ts')
  };
  const missing = [];
  const violations = [];
  for (const [name, content, text] of [
    ['Bingo module', files.bingoModule, '.addSpotFactory(BingoRoomSpot)'],
    ['Bingo match', files.bingoMatch, 'ZLINK_ACTOR_MANAGER'],
    ['Bingo entry', files.bingoEntry, '.joinSpot(roomId'],
    ['Bingo entry', files.bingoEntry, 'onCreateActor'],
    ['Bingo entry', files.bingoEntry, 'onJoinedActor'],
    ['Bingo entry', files.bingoEntry, 'destroyActor(actor'],
    ['Bingo room', files.bingoRoom, 'onActorJoin'],
    ['Bingo room', files.bingoRoom, 'onLeaveActor'],
    ['Bingo room', files.bingoRoom, 'context.leaveActor(actor'],
    ['TicTacToe module', files.ticTacToeModule, '.addSpotFactory(TicTacToeGameSpot)'],
    ['TicTacToe create', files.ticTacToeCreate, 'TICTACTOE_GAME_ROOM_PROVISIONER'],
    ['TicTacToe create', files.ticTacToeCreate, 'this.rooms.provision(roomId)'],
    ['TicTacToe entry', files.ticTacToeEntry, 'actor.context.joinSpot(roomId, request)'],
    ['TicTacToe entry', files.ticTacToeEntry, 'onCreateActor'],
    ['TicTacToe entry', files.ticTacToeEntry, 'onJoinedActor'],
    ['TicTacToe entry', files.ticTacToeEntry, 'destroyActor(actor'],
    ['TicTacToe game', files.ticTacToeGame, 'onActorJoin'],
    ['TicTacToe game', files.ticTacToeGame, 'onLeaveActor'],
    ['TicTacToe game', files.ticTacToeGame, 'this.context.leaveActor(actor)'],
    ['TicTacToe session', files.ticTacToeSession, 'this.context.actors.bind(actorRef)'],
    ['TicTacToe session', files.ticTacToeSession, 'await this.relayToActor(playHeader, payload, signal)']
  ]) {
    if (!content.includes(text)) {
      missing.push(`${name}:${text}`);
    }
  }
  for (const [name, content, pattern] of [
    ['Bingo allocator', files.bingoAllocator, /ZLINK_SPOT_MANAGER|\.getOrCreate\(|\.executeOnSpot|Infrastructure\/ZLink|RedisBingoMatchQueue/],
    ['Bingo entry', files.bingoEntry, /\.onActorJoin\s*\(/],
    ['TicTacToe entry', files.ticTacToeEntry, /cleanupFinishedRoom|\.onJoinedActor\s*\(/],
    ['TicTacToe session', files.ticTacToeSession, /TicTacToeGameCreator|cleanupFinishedRoom/]
  ]) {
    if (pattern.test(content)) {
      violations.push(name);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('node Bingo stop observing request is owned by the observer room Spot', () => {
  const entry = readSample('Bingo.Ts', 'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/bingo-entry-spot.ts');
  const room = readSample('Bingo.Ts', 'Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/bingo-room-spot.ts');
  const handler = readSample('Bingo.Ts', 'Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/stop-observing-bingo-events-handler.ts');

  assert.match(handler, /zlinkSpotActorRequestHandler/);
  assert.match(handler, /spot:\s*\(\)\s*=>\s*BingoRoomSpot/);
  assert.match(room, /actorRequest\(PacketNames\.stopObservingBingoEventsReq,\s*StopObservingBingoEventsHandler\)/);
  assert.doesNotMatch(entry, /actorRequest\(PacketNames\.stopObservingBingoEventsReq/);
});

test('node client flow files use ClientScenario names', () => {
  const violations = [];
  for (const sample of requiredSamples) {
    const clientRoot = path.join(samplesRoot, sample, 'Client');
    for (const file of sampleSourceFiles(clientRoot)) {
      const relative = relativePath(path.join(samplesRoot, sample), file);
      const content = fs.readFileSync(file, 'utf8');
      if (/client-app|self-check|TestScenario/.test(relative) || /ClientApp|TestScenario/.test(content)) {
        violations.push(`${sample}/${relative}`);
      }
    }
  }

  assert.deepEqual(violations, []);
});

test('node samples keep only the maintained canonical variants', () => {
  const entries = fs.readdirSync(samplesRoot, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => entry.name)
    .sort();

  assert.deepEqual(entries, [...requiredSamples].sort());
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

test('node dotnet-parity samples expose buildable scenario entrypoints', () => {
  const cases = [
    ['DeliveryDispatch.Ts', '@zlink-systems/sample-deliverydispatch-ts', 'deliverydispatch-client-scenario.ts', 'DeliveryDispatchClientScenario', 'PASS DeliveryDispatch.Ts'],
    ['GameQuest.Ts', '@zlink-systems/sample-gamequest-ts', 'gamequest-client-scenario.ts', 'GameQuestClientScenario', 'PASS GameQuest.Ts'],
    ['ShoppingMall.Ts', '@zlink-systems/sample-shoppingmall-ts', 'shoppingmall-client-scenario.ts', 'ShoppingMallClientScenario', 'PASS ShoppingMall.Ts']
  ];
  const missing = [];

  for (const [sample, packageName, scenarioFile, scenarioName, passMarker] of cases) {
    const packageJson = fs.readFileSync(path.join(samplesRoot, sample, 'package.json'), 'utf8');
    const tsconfig = fs.readFileSync(path.join(samplesRoot, sample, 'tsconfig.json'), 'utf8');
    const client = fs.readFileSync(path.join(samplesRoot, sample, 'Client', 'main.ts'), 'utf8');
    const scenario = fs.readFileSync(path.join(
      samplesRoot,
      sample,
      'Client',
      scenarioFile
    ), 'utf8');
    const server = fs.readFileSync(path.join(samplesRoot, sample, 'Server', 'main.ts'), 'utf8');
    const contracts = fs.readFileSync(path.join(samplesRoot, sample, 'Shared', 'Contracts', 'messages.ts'), 'utf8');

    for (const [content, text] of [
      [packageJson, packageName],
      [packageJson, 'tsc -p tsconfig.json'],
      [tsconfig, '"outDir": "dist"'],
      [tsconfig, '"Server/**/*.ts"'],
      [client, scenarioName],
      [client, passMarker],
      [scenario, 'ensure('],
      [server, sample === 'DeliveryDispatch.Ts'
        ? 'createDeliveryDispatchModule'
        : sample === 'GameQuest.Ts'
          ? 'createGameQuestModule'
          : 'createShoppingMallModule'],
      [contracts, 'PacketNames']
    ]) {
      if (!content.includes(text)) {
        missing.push(`${sample}:${text}`);
      }
    }
  }

  assert.deepEqual(missing, []);
});

test('SupportChat TypeScript Entry Spot uses API channel orchestration', () => {
  const supportEntry = fs.readFileSync(
    path.join(samplesRoot, 'SupportChat.Ts', 'Server', 'Support', 'Infrastructure', 'ZLink', 'Spots', 'EntrySpot', 'support-entry-spot.ts'),
    'utf8'
  );
  const apiHandler = fs.readFileSync(
    path.join(samplesRoot, 'SupportChat.Ts', 'Server', 'Api', 'Handlers', 'open-conversation-handler.ts'),
    'utf8'
  );

  assert.match(supportEntry, /context\.outbound\s*\n?\s*\.requestToChannel/);
  assert.match(supportEntry, /SampleNames\.apiChannel/);
  assert.match(supportEntry, /openConversationApiReq/);
  assert.doesNotMatch(supportEntry, /allocator\.allocate|assignNextAgent|actors\.get/);

  assert.match(apiHandler, /SampleNames\.supportChannel/);
  assert.match(apiHandler, /allocateConversationReq/);
  assert.match(apiHandler, /assignAgentReq/);
});

test('DeliveryDispatch TypeScript sample uses framework channel topology', () => {
  const clientScenario = readSample('DeliveryDispatch.Ts', 'Client/deliverydispatch-client-scenario.ts');
  const clientModule = readSample('DeliveryDispatch.Ts', 'Client/deliverydispatch-client-module.ts');
  const serverModule = readSample('DeliveryDispatch.Ts', 'Server/DispatchCenter/deliverydispatch-dispatch-module.ts');
  const serverMain = readSample('DeliveryDispatch.Ts', 'Server/main.ts');
  const runSample = fs.readFileSync(path.join(samplesRoot, 'DeliveryDispatch.Ts', 'run_sample.sh'), 'utf8');

  assert.match(clientScenario, /requestToChannel/);
  assert.match(clientScenario, /SampleNames\.dispatchChannel/);
  assert.match(clientModule, /zlinkFramework\(\)/);
  assert.match(clientModule, /\.addRegistryEndpoint\(config\.registryRouterEndpoint\)/);
  assert.match(clientModule, /\.enableClient\(\)/);
  assert.match(serverModule, /zlinkFramework\(\)/);
  assert.match(serverModule, /\.enableServer\(config\.dispatchEndpoint\)/);
  assert.match(serverModule, /addRequestHandler\(PacketNames\.createDeliveryReq/);
  assert.match(serverModule, /addRequestHandler\(PacketNames\.assignDeliveryReq/);
  assert.match(serverModule, /addRequestHandler\(PacketNames\.advanceDeliveryReq/);
  assert.match(serverMain, /NestFactory\.createApplicationContext/);
  assert.match(runSample, /DELIVERYDISPATCH_DISPATCH_ENDPOINT/);
  assert.match(runSample, /tcp:\/\/127\.0\.0\.1/);
  assert.doesNotMatch(clientScenario, /fetch\(|http\.createServer|SAMPLE_ENDPOINT|support::request_line/);
  assert.doesNotMatch(serverMain, /http\.createServer|SAMPLE_ENDPOINT/);
});

test('GameQuest TypeScript sample uses framework channel topology', () => {
  const clientScenario = readSample('GameQuest.Ts', 'Client/gamequest-client-scenario.ts');
  const clientModule = readSample('GameQuest.Ts', 'Client/gamequest-client-module.ts');
  const serverModule = readSample('GameQuest.Ts', 'Server/QuestMission/gamequest-quest-module.ts');
  const serverMain = readSample('GameQuest.Ts', 'Server/main.ts');
  const runSample = fs.readFileSync(path.join(samplesRoot, 'GameQuest.Ts', 'run_sample.sh'), 'utf8');

  assert.match(clientScenario, /requestToChannel/);
  assert.match(clientScenario, /SampleNames\.questChannel/);
  assert.match(clientModule, /zlinkFramework\(\)/);
  assert.match(clientModule, /\.addRegistryEndpoint\(config\.registryRouterEndpoint\)/);
  assert.match(clientModule, /\.enableClient\(\)/);
  assert.match(serverModule, /zlinkFramework\(\)/);
  assert.match(serverModule, /\.enableServer\(config\.questEndpoint\)/);
  assert.match(serverModule, /addRequestHandler\(PacketNames\.enterAreaReq/);
  assert.match(serverModule, /addRequestHandler\(PacketNames\.killMonsterReq/);
  assert.match(serverModule, /addRequestHandler\(PacketNames\.collectItemReq/);
  assert.match(serverModule, /addRequestHandler\(PacketNames\.completeMissionReq/);
  assert.match(serverMain, /NestFactory\.createApplicationContext/);
  assert.match(runSample, /GAMEQUEST_QUEST_ENDPOINT/);
  assert.match(runSample, /tcp:\/\/127\.0\.0\.1/);
  assert.doesNotMatch(clientScenario, /fetch\(|http\.createServer|SAMPLE_ENDPOINT|support::request_line/);
  assert.doesNotMatch(serverMain, /http\.createServer|SAMPLE_ENDPOINT/);
});

test('ShoppingMall TypeScript sample uses framework channel topology', () => {
  const clientScenario = readSample('ShoppingMall.Ts', 'Client/shoppingmall-client-scenario.ts');
  const clientModule = readSample('ShoppingMall.Ts', 'Client/shoppingmall-client-module.ts');
  const serverModule = readSample('ShoppingMall.Ts', 'Server/OrderWorkflow/shoppingmall-workflow-module.ts');
  const serverMain = readSample('ShoppingMall.Ts', 'Server/main.ts');
  const runSample = fs.readFileSync(path.join(samplesRoot, 'ShoppingMall.Ts', 'run_sample.sh'), 'utf8');

  assert.match(clientScenario, /requestToChannel/);
  assert.match(clientScenario, /SampleNames\.workflowChannel/);
  assert.match(clientModule, /zlinkFramework\(\)/);
  assert.match(clientModule, /\.addRegistryEndpoint\(config\.registryRouterEndpoint\)/);
  assert.match(clientModule, /\.enableClient\(\)/);
  assert.match(serverModule, /zlinkFramework\(\)/);
  assert.match(serverModule, /\.enableServer\(config\.workflowEndpoint\)/);
  assert.match(serverModule, /addRequestHandler\(PacketNames\.startOrderReq/);
  assert.match(serverModule, /addRequestHandler\(PacketNames\.continueOrderWorkflowReq/);
  assert.match(serverMain, /NestFactory\.createApplicationContext/);
  assert.match(runSample, /SHOPPINGMALL_WORKFLOW_ENDPOINT/);
  assert.match(runSample, /tcp:\/\/127\.0\.0\.1/);
  assert.doesNotMatch(clientScenario, /fetch\(|http\.createServer|SAMPLE_ENDPOINT|support::request_line/);
  assert.doesNotMatch(serverMain, /http\.createServer|SAMPLE_ENDPOINT/);
});

test('dotnet-parity TypeScript clients do not import server modules', () => {
  const violations = [];
  for (const sample of ['DeliveryDispatch.Ts', 'GameQuest.Ts', 'ShoppingMall.Ts']) {
    for (const file of sampleSourceFiles(path.join(samplesRoot, sample, 'Client'))) {
      const content = fs.readFileSync(file, 'utf8');
      if (/from ['"]\.\.\/Server\//.test(content)) {
        violations.push(relativePath(samplesRoot, file));
      }
    }
  }

  assert.deepEqual(violations, []);
});

test('node samples use only framework and connector public APIs', () => {
  const violations = [];
  for (const file of sampleSourceFiles(samplesRoot)) {
    const content = fs.readFileSync(file, 'utf8');
    if (/bindings\/node|runtime\/native|src\/zlink\/runtime|packages\/[^/]+\/src/.test(content)) {
      violations.push(relativePath(workspaceRoot, file));
    }
  }

  assert.deepEqual(violations, []);
});

test('node framework samples exercise the real NestJS application context', () => {
  const missing = [];
  for (const sample of ['TicTacToe.Ts', 'Bingo.Ts']) {
    const usesNestModule = sampleSourceFiles(path.join(samplesRoot, sample))
      .some((file) => {
        const content = fs.readFileSync(file, 'utf8');
        return content.includes('@zlink-systems/nestjs')
          || content.includes('packages/nestjs/dist');
      });
    if (!usesNestModule) {
      missing.push(sample);
    }
  }

  const hiddenServerRuntime = [];
  for (const file of sampleSourceFiles(samplesRoot)) {
    const relative = relativePath(samplesRoot, file);
    if (relative.startsWith('shared/') || relative.includes('/dist/')) {
      continue;
    }
    const content = fs.readFileSync(file, 'utf8');
    if (/startChannelServer|startRouteServer|createZLinkNestRuntime|nestjs-provider-runtime/.test(content)) {
      hiddenServerRuntime.push(relative);
    }
  }

  const serverRoles = [
    ['TicTacToe.Ts/Server/Api/main.ts', 'TicTacToe.Ts/Server/Api/tictactoe-api-module.ts', 'createTicTacToeApiModule'],
    ['TicTacToe.Ts/Server/Play/main.ts', 'TicTacToe.Ts/Server/Play/tictactoe-play-module.ts', 'createTicTacToePlayModule'],
    ['Bingo.Ts/Server/Api/main.ts', 'Bingo.Ts/Server/Api/bingo-api-module.ts', 'createBingoApiModule'],
    ['Bingo.Ts/Server/Play/main.ts', 'Bingo.Ts/Server/Play/bingo-play-module.ts', 'createBingoPlayModule'],
    ['Bingo.Ts/Server/Session/main.ts', 'Bingo.Ts/Server/Session/bingo-session-module.ts', 'createBingoSessionModule']
  ];
  for (const [mainRelative, moduleRelative, factoryName] of serverRoles) {
    const main = fs.readFileSync(path.join(samplesRoot, mainRelative), 'utf8');
    const module = fs.readFileSync(path.join(samplesRoot, moduleRelative), 'utf8');
    if (!main.includes(factoryName)) {
      missing.push(`${mainRelative}:${factoryName}`);
    }
    if (!main.includes('NestFactory.createApplicationContext')) {
      missing.push(`${mainRelative}:NestFactory.createApplicationContext`);
    }
    for (const text of ['providers: [', "require('@nestjs/common')", 'ZLinkModule.forRoot']) {
      if (main.includes(text)) {
        hiddenServerRuntime.push(`${mainRelative}:${text}`);
      }
    }
    if (
      !module.includes("require('@nestjs/common')")
      && !module.includes("from '@nestjs/common'")
      && !module.includes('zlinkModule')
    ) {
      missing.push(`${moduleRelative}:@nestjs/common`);
    }
    if (!module.includes('ZLinkModule.forRoot')) {
      missing.push(`${moduleRelative}:ZLinkModule.forRoot`);
    }
    if (
      !moduleRelative.includes('/Registry/')
      && !/providers:\s*(?:\[|zlinkDiscoverProviders)/.test(module)
      && !module.includes('providerDiscovery')
      && !module.includes('zlinkModule(__dirname')
    ) {
      missing.push(`${moduleRelative}:providers`);
    }
  }

  const registryMain = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts/Server/Registry/main.ts'), 'utf8');
  const registryHost = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts/Server/Registry/registry-server-host.ts'), 'utf8');
  if (!registryMain.includes('createBingoRegistryServer')) {
    missing.push('Bingo.Ts/Server/Registry/main.ts:createBingoRegistryServer');
  }
  if (!registryMain.includes('.start()') || !registryMain.includes('.close()')) {
    missing.push('Bingo.Ts/Server/Registry/main.ts:start-close');
  }
  if (!registryHost.includes('ZLinkRegistryModule.forRoot')) {
    missing.push('Bingo.Ts/Server/Registry/registry-server-host.ts:ZLinkRegistryModule.forRoot');
  }
  if (!registryHost.includes('NestFactory.createApplicationContext')) {
    missing.push('Bingo.Ts/Server/Registry/registry-server-host.ts:NestFactory.createApplicationContext');
  }
  for (const text of ['ZLinkRegistryRuntime', "require('@nestjs/common')"]) {
    if (registryMain.includes(text) || registryHost.includes(text)) {
      hiddenServerRuntime.push(`Bingo.Ts/Server/Registry:${text}`);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(hiddenServerRuntime, []);
});

test('TicTacToe TypeScript sample builds and exposes basic TypeScript roles', () => {
  const packageJson = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'package.json'), 'utf8');
  const tsconfig = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'tsconfig.json'), 'utf8');
  const client = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Client', 'main.ts'), 'utf8');
  const api = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Api', 'main.ts'), 'utf8');
  const play = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'main.ts'), 'utf8');
  const readme = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'README.ko.md'), 'utf8');
  const runSamples = fs.readFileSync(path.join(samplesRoot, 'run_samples.sh'), 'utf8');
  const required = [
    [packageJson, '@zlink-systems/sample-tictactoe-ts'],
    [packageJson, 'tsc -p tsconfig.json'],
    [tsconfig, '"outDir": "dist"'],
    [tsconfig, '"Server/**/*.ts"'],
    [client, 'loadSampleConfig'],
    [client, 'PASS TicTacToe.Ts'],
    [api, 'TicTacToeApiModule'],
    [play, 'TicTacToePlayModule'],
    [readme, 'TicTacToe TypeScript Sample'],
    [runSamples, 'TicTacToe.Ts/run_sample.sh'],
    [runSamples, 'run_sample.sh']
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
      violations.push(`${relativePath(samplesRoot, file)} references the JavaScript TicTacToe sample`);
    }
    if (content.includes('@ts-nocheck')) {
      violations.push(`${relativePath(samplesRoot, file)} disables TypeScript checking`);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('TicTacToe TypeScript sample mirrors dotnet game state contract', () => {
  const readme = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'README.ko.md'), 'utf8');
  const client = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Client', 'tictactoe-client-scenario.ts'), 'utf8');
  const board = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Domain', 'TicTacToe', 'tictactoe-board.ts'), 'utf8');
  const match = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Domain', 'TicTacToe', 'tictactoe-match.ts'), 'utf8');
  const joinHandler = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'EntrySpot', 'Handlers', 'play-actor-join-game-handler.ts'), 'utf8');
  const moveHandler = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'TicTacToeGameSpot', 'Handlers', 'play-actor-place-mark-handler.ts'), 'utf8');
  const gameSpot = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'TicTacToeGameSpot', 'tictactoe-game-spot.ts'), 'utf8');
  const playActor = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Infrastructure', 'ZLink', 'Actors', 'play-actor.ts'), 'utf8');
  const playSession = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Infrastructure', 'ZLink', 'Sessions', 'play-session.ts'), 'utf8');
  const required = [
    [board, 'class TicTacToeBoard'],
    [match, 'class TicTacToeMatch'],
    [match, 'this.status = GameStatus.InProgress'],
    [match, 'this.status = GameStatus.Won'],
    [match, 'this.status = GameStatus.TurnTimedOut'],
    [joinHandler, 'entrySpot.join(actor, request.player ?? actor, request.roomId)'],
    [moveHandler, 'spot.placeMark(actor, request.cell)'],
    [gameSpot, 'gameStateNotify(state)'],
    [playActor, 'this.context.boundSession'],
    [playSession, 'this.context.actors.bind(actorRef)'],
    [client, 'payload.state.status === GameStatus.InProgress'],
    [client, 'stateOf(client1FinalMove).status === GameStatus.Won'],
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
  const client = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Client', 'main.ts'), 'utf8');
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
    [client, "from './bingo-client-scenario'"],
    [client, 'loadSampleConfig'],
    [client, 'bingo=completed'],
    [api, 'async function bootstrap'],
    [session, 'async function bootstrap'],
    [play, 'async function bootstrap'],
    [readme, 'TypeScript Client/Server/Shared 구조'],
    [runSamples, 'Bingo.Ts/run_sample.sh'],
    [runSamples, 'run_sample.sh']
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
      violations.push(`${relativePath(samplesRoot, file)} references the JavaScript Bingo sample`);
    }
    if (content.includes('@ts-nocheck')) {
      violations.push(`${relativePath(samplesRoot, file)} disables TypeScript checking`);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('Bingo TypeScript sample uses registry discovery instead of direct server peer endpoints', () => {
  const api = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Api', 'main.ts'), 'utf8');
  const apiModule = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Api', 'bingo-api-module.ts'), 'utf8');
  const play = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Play', 'main.ts'), 'utf8');
  const playModule = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Play', 'bingo-play-module.ts'), 'utf8');
  const session = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Session', 'main.ts'), 'utf8');
  const sessionModule = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Session', 'bingo-session-module.ts'), 'utf8');
  const registry = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Registry', 'registry-server-host.ts'), 'utf8');
  const required = [
    [registry, 'ZLinkRegistryModule.forRoot'],
    [registry, 'NestFactory.createApplicationContext'],
    [registry, 'registryPubEndpoint'],
    [registry, 'registryRouterEndpoint'],
    [apiModule, '.useDiscovery()'],
    [apiModule, '.addRegistryEndpoint(config.registryRouterEndpoint)'],
    [apiModule, '.addClientServerChannel(SampleNames.playChannel'],
    [apiModule, '.enableClient()'],
    [playModule, '.useDiscovery()'],
    [playModule, '.addRegistryEndpoint(config.registryRouterEndpoint)'],
    [sessionModule, '.useDiscovery()'],
    [sessionModule, '.addRegistryEndpoint(endpoints.registryRouterEndpoint)']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);
  const violations = [];
  for (const [content, text] of [
    [api, 'createRegistryClient'],
    [play, 'createRegistryClient'],
    [session, 'createRegistryClient'],
    [api, 'registry.resolve'],
    [play, 'registry.register'],
    [session, 'registry.resolve'],
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
    'Infrastructure',
    'ZLink',
    'Spots',
    'BingoRoomSpot',
    'bingo-room-spot.ts'
  ), 'utf8');
  const drawIndex = roomSpot.indexOf('PacketNames.numberDrawnNotify');
  const finishedBranchIndex = roomSpot.indexOf('if (drawn.finished)');
  const endedIndex = roomSpot.indexOf('PacketNames.gameEndedNotify');

  assert.equal(drawIndex > 0, true);
  assert.equal(finishedBranchIndex > drawIndex, true);
  assert.equal(endedIndex > finishedBranchIndex, true);
});

test('node topology samples run server roles as separate processes over TCP route endpoints', () => {
  const cases = [
    ['TicTacToe.Ts', 'Server/Api/main.ts', 'TICTACTOE_API_A_ENDPOINT'],
    ['TicTacToe.Ts', 'Server/Api/main.ts', 'TICTACTOE_API_B_ENDPOINT'],
    ['TicTacToe.Ts', 'Server/Play/main.ts', 'TICTACTOE_PLAY_A_CHANNEL_ENDPOINT'],
    ['TicTacToe.Ts', 'Server/Play/main.ts', 'TICTACTOE_PLAY_B_CHANNEL_ENDPOINT'],
    ['TicTacToe.Ts', 'Server/Play/main.ts', 'TICTACTOE_PLAY_A_SPOT_ENDPOINT'],
    ['TicTacToe.Ts', 'Server/Play/main.ts', 'TICTACTOE_PLAY_B_SPOT_ENDPOINT'],
    ['TicTacToe.Ts', 'Server/Play/main.ts', 'TICTACTOE_PLAY_A_SPOT_PUBSUB_ENDPOINT'],
    ['TicTacToe.Ts', 'Server/Play/main.ts', 'TICTACTOE_PLAY_B_SPOT_PUBSUB_ENDPOINT'],
    ['Bingo.Ts', 'Server/Api/main.ts', 'BINGO_API_A_ENDPOINT'],
    ['Bingo.Ts', 'Server/Api/main.ts', 'BINGO_API_B_ENDPOINT'],
    ['Bingo.Ts', 'Server/Play/main.ts', 'BINGO_PLAY_A_ENDPOINT'],
    ['Bingo.Ts', 'Server/Play/main.ts', 'BINGO_PLAY_B_ENDPOINT'],
    ['Bingo.Ts', 'Server/Session/main.ts', 'BINGO_SESSION_A_ENDPOINT'],
    ['Bingo.Ts', 'Server/Session/main.ts', 'BINGO_SESSION_B_ENDPOINT'],
    ['Bingo.Ts', 'Server/Registry/main.ts', 'BINGO_REGISTRY_PUB_ENDPOINT'],
    ['Bingo.Ts', 'Server/Registry/main.ts', 'BINGO_REGISTRY_ROUTER_ENDPOINT']
  ];
  const clientEndpointEnvs = new Set([
    'TICTACTOE_PLAY_A_STREAM_ENDPOINT',
    'TICTACTOE_PLAY_B_STREAM_ENDPOINT',
    'TICTACTOE_API_HTTP_ENDPOINT',
    'BINGO_SESSION_A_ENDPOINT',
    'BINGO_SESSION_B_ENDPOINT'
  ]);

  for (const [sample, serverRelative, endpointEnv] of cases) {
    const serverEntry = path.join(samplesRoot, sample, serverRelative);
    const runSample = fs.readFileSync(path.join(samplesRoot, sample, 'run_sample.sh'), 'utf8');
    const clientEntry = path.join(samplesRoot, sample, 'Client', 'main.ts');
    const serverContent = fs.readFileSync(serverEntry, 'utf8');
    const clientContent = fs.readFileSync(clientEntry, 'utf8');

    assert.equal(fs.existsSync(serverEntry), true);
    assert.match(serverContent, /loadSampleConfig|forRootFactory/);
    assert.match(runSample, new RegExp(escapeRegExp(endpointEnv)));
    assert.match(runSample, /ZLINK_SAMPLE_CONFIG/);
    assert.match(runSample, /start_server/);
    if (clientEndpointEnvs.has(endpointEnv)) {
      assert.match(clientContent, /loadSampleConfig/);
    }
  }
});

test('node topology samples do not use stdin command protocol as messaging', () => {
  const violations = [];
  for (const file of sampleSourceFiles(samplesRoot)) {
    const content = fs.readFileSync(file, 'utf8');
    if (/runRoleServer|startRoleProcess|withRoleProcess|command ===|stdin\.write/.test(content)) {
      violations.push(relativePath(samplesRoot, file));
    }
  }

  assert.deepEqual(violations, []);
});

test('node samples do not hide readiness with sleeps or pre-ready pings', () => {
  const violations = [];
  const allowedTimingFiles = new Set([
    'samples/Bingo.Ts/Server/runtime-support.ts',
    'samples/SupportChat.Ts/Client/supportchat-client-scenario.ts',
    'samples/SupportChat.Ts/Server/Support/notification-delivery-log.ts',
    'samples/SupportChat.Ts/Server/runtime-support.ts'
  ]);
  for (const file of sampleSourceFiles(samplesRoot)) {
    if (allowedTimingFiles.has(relativePath(workspaceRoot, file))) {
      continue;
    }
    const content = fs.readFileSync(file, 'utf8');
    if (/\bsleep\s*\(|setTimeout\s*\(|beforeReady/.test(content)) {
      violations.push(relativePath(workspaceRoot, file));
    }
  }

  assert.deepEqual(violations, []);
});

test('node client samples wait for push packets through stream connector helpers', () => {
  const bingoApp = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Client', 'bingo-client-scenario.ts'), 'utf8');
  const ticTacToeClient = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Client', 'tictactoe-client-scenario.ts'), 'utf8');
  const missing = [];
  const violations = [];
  for (const [name, content] of [
    ['Bingo.Ts/Client/bingo-client-scenario.ts', bingoApp],
    ['TicTacToe.Ts/Client/tictactoe-client-scenario.ts', ticTacToeClient]
  ]) {
    if (!/\.waitFor(?:<|\()/.test(content)) {
      missing.push(`${name}:.waitFor(`);
    }
    if (!/\.waitFor(?:<|\()[\s\S]*?\.submit\(/.test(content)) {
      missing.push(`${name}:.waitFor(...).submit(`);
    }
  }
  for (const [name, content] of [
    ['Bingo.Ts/Client/bingo-client-scenario.ts', bingoApp],
    ['TicTacToe.Ts/Client/tictactoe-client-scenario.ts', ticTacToeClient]
  ]) {
    if (/waitForJson|waitForNotify|async function waitFor\s*\(|\.waitFor(?:<[^>]+>)?\([^)]*,/.test(content)) {
      violations.push(name);
    }
    if (/\.on<[^>]+>\(/.test(content)) {
      violations.push(`${name}:.on`);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('node client scenarios follow the common sample document order', () => {
  const bingoApp = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Client', 'bingo-client-scenario.ts'), 'utf8');
  const ticTacToeClient = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Client', 'tictactoe-client-scenario.ts'), 'utf8');

  assertOrdered('Bingo.Ts/Client/bingo-client-scenario.ts', bingoApp, [
    "1. Clients connect only to Session streams, authenticate",
    'client1.request(authenticateReq(BingoSamplePlayers.player1))',
    'client2.request(authenticateReq(BingoSamplePlayers.player2))',
    '2. player-1 matches first',
    'client1.request(matchBingoReq())',
    'client1MatchRes.roomId.length > 0',
    'await client1SelfJoinNotify',
    '4-6. player-2 joins the same room',
    '.waitFor<PlayerJoinedNotify>',
    'client2SelfJoinNotify',
    '.waitFor<StateEnvelope>(PacketNames.gameStartedNotify)',
    '.waitFor<StateEnvelope>(PacketNames.gameStartedNotify)',
    'client2.request(matchBingoReq())',
    'await client2SelfJoinNotify',
    '7. Both clients submit deterministic cards',
    '.request(submitBingoCardReq',
    '.request(submitBingoCardReq',
    'stateOf(client1Card).players.length === 2',
    '8. Number drawing is server-driven',
    'requireSameDraw(client1Draw.payload, client2Draw.payload, drawTask.drawSeq)',
    '9. Both clients receive the final finished state',
    'client1EndedTask',
    'ended.status === BingoRoomStatus.Finished'
  ]);

  assertOrdered('TicTacToe.Ts/Client/tictactoe-client-scenario.ts', ticTacToeClient, [
    '1. Create the room through API',
    ".body(createGameReq('match-ready'))",
    'game.roomId.length > 0',
    'game.ownerPlayEndpoint.length > 0',
    'createPlayerClient(game.ownerPlayEndpoint',
    'createPlayerClient(observerPlayEndpoint',
    '2. Host, guest, and observer connect directly',
    "client1.request(authenticateReq('player-x'))",
    "client2.request(authenticateReq('player-o'))",
    '3. Host joins by explicit RoomId',
    'client1.request(joinGameReq(game.roomId))',
    "stateOf(client1Join).roomId === game.roomId",
    'stateOf(client1Join).xActorId === client1Auth.player.actorId',
    'client1SawClient2Join',
    '4-6. Guest joins by the same RoomId',
    'client2.request(joinGameReq(game.roomId))',
    'stateOf(client2Join).oActorId === client2Auth.player.actorId',
    'client1Running.payload.state.nextTurn === GameMarks.x',
    '7. Each move response is matched with the opponent notify',
    'client1.request(placeMarkStreamReq(0))',
    "stateOf(client1Move1).board === 'X........'",
    '8. The final host move wins',
    'client1.request(placeMarkStreamReq(2))',
    "stateOf(client1FinalMove).board === 'XXXOO....'",
    'stateOf(client1FinalMove).status === GameStatus.Won'
  ]);
});

test('node samples use the codecs required by the common specs', () => {
  const ticTacToeClient = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Client', 'tictactoe-client-scenario.ts'), 'utf8');
  const ticTacToePlay = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'tictactoe-play-module.ts'), 'utf8');
  const ticTacToeContracts = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Shared', 'Contracts', 'messages.ts'), 'utf8');
  const bingoClient = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Client', 'main.ts'), 'utf8');
  const bingoSessionModule = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Session', 'bingo-session-module.ts'), 'utf8');
  const bingoSession = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Session', 'Sessions', 'bingo-session.ts'), 'utf8');
  const bingoRoomSpot = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'BingoRoomSpot', 'bingo-room-spot.ts'), 'utf8');
  const bingoContracts = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Shared', 'Contracts', 'messages.ts'), 'utf8');
  const bingoCodec = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Shared', 'Contracts', 'protobuf-codec.ts'), 'utf8');
  const bingoProto = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Shared', 'Contracts', 'bingo_messages.proto'), 'utf8');
  const required = [
    [ticTacToeClient, 'zlinkStreamConnectorFactory.create'],
    [ticTacToePlay, '.addStreamNode(SampleNames.playStream'],
    [bingoClient, 'bingoProtobuf'],
    [bingoSessionModule, 'zlinkProtobufCodec'],
    [bingoSessionModule, '.codecs()'],
    [bingoSession, 'payload.decode<AuthenticateReq>'],
    [bingoRoomSpot, 'request.decode<BingoRoomJoinReq>'],
    [bingoProto, 'message AuthenticateReq'],
    [bingoProto, 'message BingoRoomState'],
    [bingoProto, 'message BingoNumberDrawnNotify']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);
  const violations = [];
  for (const sample of ['Bingo.Ts', 'TicTacToe.Ts', 'SupportChat.Ts']) {
    for (const file of sampleSourceFiles(path.join(samplesRoot, sample))) {
      if (!file.endsWith('.ts')) {
        continue;
      }
      const content = fs.readFileSync(file, 'utf8');
      const relative = relativePath(samplesRoot, file);
      if (sample === 'TicTacToe.Ts' && /MessagePack|msgpack|toMsgPack|fromMsgPack|zlinkStreamMessagePackCodec|createMessagePackMessage|readMessagePackMessage/.test(content)) {
        violations.push(relative);
      }
      if (sample === 'Bingo.Ts' && /MessagePack|msgpack|toMsgPack|fromMsgPack|zlinkStreamMessagePackCodec|createMessagePackMessage|readMessagePackMessage/.test(content)) {
        violations.push(relative);
      }
      if (/bingoChannelHandlerOptions|decodeBingoChannelReply|submit<Buffer>|\.then\(decode/.test(content)) {
        violations.push(relative);
      }
      if (/payload\.getString\(|Message\.from\(|Buffer\.from\(/.test(content)
          && !isAllowedSampleRawBoundaryFile(relative)) {
        violations.push(`${relative}:raw-codec-helper`);
      }
      if (/writeVarint|readVarint|schemaTable|manualSchema|wireType/.test(content)) {
        violations.push(relative);
      }
      if (/addSerializer\s*\(|bingoProtobufSerializer|bingoProtobufContentType/.test(content)) {
        violations.push(relative);
      }
      if (sample === 'Bingo.Ts' && /createProtobufMessage|readProtobufMessage|bingoMessage|readBingoMessage/.test(content)) {
        violations.push(`${relative}:protobuf-message-helper`);
      }
      if (sample === 'Bingo.Ts'
          && /fromBingoProto|toBingoProto/.test(content)
          && !isAllowedBingoRawSessionCodecFile(relative)) {
        violations.push(`${relative}:protobuf-session-helper`);
      }
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('TicTacToe server uses framework stream session instead of connector framing', () => {
  const checked = [
    'Server/Play/tictactoe-play-module.ts',
    'Server/Play/Infrastructure/ZLink/Actors/play-actor.ts',
    'Server/Play/Infrastructure/ZLink/Sessions/play-session.ts',
    'Server/Play/Infrastructure/ZLink/Sessions/play-session-factory.ts',
    'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/Handlers/play-actor-join-game-handler.ts',
    'Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/Handlers/play-actor-place-mark-handler.ts',
    'Shared/Contracts/messages.ts'
  ];
  const missing = [];
  const violations = [];

  for (const relative of checked) {
    const content = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', relative), 'utf8');
    if (/stream-connector|ZlinkStream(Frame|Codec)|net\.createServer|tryReadFrame/.test(content)) {
      violations.push(relative);
    }
  }

  const playModule = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'tictactoe-play-module.ts'), 'utf8');
  const playSession = fs.readFileSync(path.join(
    samplesRoot,
    'TicTacToe.Ts',
    'Server',
    'Play',
    'Infrastructure',
    'ZLink',
    'Sessions',
    'play-session.ts'
  ), 'utf8');
  const playActor = fs.readFileSync(path.join(
    samplesRoot,
    'TicTacToe.Ts',
    'Server',
    'Play',
    'Infrastructure',
    'ZLink',
    'Actors',
    'play-actor.ts'
  ), 'utf8');
  const playJoinHandler = fs.readFileSync(path.join(
    samplesRoot,
    'TicTacToe.Ts',
    'Server',
    'Play',
    'Infrastructure',
    'ZLink',
    'Spots',
    'EntrySpot',
    'Handlers',
    'play-actor-join-game-handler.ts'
  ), 'utf8');
  const gameSpot = fs.readFileSync(path.join(
    samplesRoot,
    'TicTacToe.Ts',
    'Server',
    'Play',
    'Infrastructure',
    'ZLink',
    'Spots',
    'TicTacToeGameSpot',
    'tictactoe-game-spot.ts'
  ), 'utf8');
  for (const text of [
    '.addStreamNode(SampleNames.playStream',
    '.registerSession(PlaySessionFactory)',
    'context.client.reply',
    'actorManager.getOrCreate',
    'joined.actor.push('
  ]) {
    if (!`${playModule}\n${playSession}\n${playActor}\n${playJoinHandler}\n${gameSpot}`.includes(text)) {
      missing.push(text);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('node samples keep contracts separate from sample configuration and application roles explicit', () => {
  const ticTacToeContracts = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Shared', 'Contracts', 'messages.ts'), 'utf8');
  const ticTacToeSettings = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Configuration', 'sample-settings.ts'), 'utf8');
  const ticTacToeCreator = fs.readFileSync(path.join(
    samplesRoot,
    'TicTacToe.Ts',
    'Server',
    'Play',
    'Application',
    'GameCreation',
    'tictactoe-game-creator.ts'
  ), 'utf8');
  const bingoAllocator = fs.readFileSync(path.join(
    samplesRoot,
    'Bingo.Ts',
    'Server',
    'Play',
    'Application',
    'RoomAllocation',
    'bingo-room-allocator.ts'
  ), 'utf8');
  const required = [
    [ticTacToeSettings, 'SampleNames'],
    [ticTacToeSettings, 'SampleTimings'],
    [ticTacToeCreator, 'class TicTacToeGameCreator'],
    [bingoAllocator, 'class BingoRoomAllocator']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);
  const violations = [];
  for (const text of [
    'SampleNames',
    'SampleTimings',
    'TicTacToeGameDirectory',
    'BingoRoomDirectory'
  ]) {
    if (ticTacToeContracts.includes(text)) {
      violations.push(`TicTacToe.Ts/Shared/Contracts/messages.ts:${text}`);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('TicTacToe uses manual handler registration and Bingo keeps automatic registration', () => {
  const apiMain = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Api', 'main.ts'), 'utf8');
  const playMain = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'main.ts'), 'utf8');
  const apiModule = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Api', 'tictactoe-api-module.ts'), 'utf8');
  const playModule = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'tictactoe-play-module.ts'), 'utf8');
  const apiHandler = fs.readFileSync(path.join(
    samplesRoot,
    'TicTacToe.Ts',
    'Server',
    'Api',
    'Handlers',
    'authenticate-player-handler.ts'
  ), 'utf8');
  const playHandler = fs.readFileSync(path.join(
    samplesRoot,
    'TicTacToe.Ts',
    'Server',
    'Play',
    'Infrastructure',
    'ZLink',
    'Handlers',
    'create-game-handler.ts'
  ), 'utf8');
  const ticTacToeTimerHandler = fs.readFileSync(path.join(
    samplesRoot,
    'TicTacToe.Ts',
    'Server',
    'Play',
    'Infrastructure',
    'ZLink',
    'Spots',
    'TicTacToeGameSpot',
    'Handlers',
    'tictactoe-game-timer-handler.ts'
  ), 'utf8');
  const bingoPlayModule = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Play', 'bingo-play-module.ts'), 'utf8');
  const bingoTimerHandler = fs.readFileSync(path.join(
    samplesRoot,
    'Bingo.Ts',
    'Server',
    'Play',
    'Infrastructure',
    'ZLink',
    'Spots',
    'BingoRoomSpot',
    'Handlers',
    'bingo-room-timer-handler.ts'
  ), 'utf8');
  const nestPackage = fs.readFileSync(path.join(workspaceRoot, 'packages', 'nestjs', 'src', 'index.ts'), 'utf8');
  const required = [
    [nestPackage, 'export function zlinkRequestHandler'],
    [nestPackage, 'export function zlinkSpotTimerHandler'],
    [apiModule, '.addRequestHandler(PacketNames.authenticatePlayerReq, AuthenticatePlayerHandler)'],
    [playModule, '.addRequestHandler(PacketNames.createGame, CreateGameHandler)'],
    [playModule, 'CreateGameHandler'],
    [playModule, 'PlayActorJoinGameHandler'],
    [playModule, 'PlayActorPlaceMarkHandler'],
    [fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'EntrySpot', 'play-entry-spot.ts'), 'utf8'),
      'this.context.handlers.actorRequest(PacketNames.joinGameReq, PlayActorJoinGameHandler)'],
    [fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'TicTacToeGameSpot', 'tictactoe-game-spot.ts'), 'utf8'),
      'this.context.handlers.actorRequest(PacketNames.placeMarkReq, PlayActorPlaceMarkHandler)'],
    [ticTacToeTimerHandler, 'class TicTacToeGameTimerHandler'],
    [bingoTimerHandler, 'class BingoRoomTimerHandler'],
    [bingoTimerHandler, '@zlinkSpotTimerHandler({'],
    [fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Api', 'bingo-api-module.ts'), 'utf8'), '.addHandlerGroup(\'api\')'],
    [bingoPlayModule, '.addHandlerGroup(\'play\')'],
    [bingoPlayModule, 'zlinkModule(__dirname'],
    [playModule, '.addStreamNode(SampleNames.playStream']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);
  const violations = [];
  if (nestPackage.includes('export function zlinkHandlers')) {
    violations.push('@zlink-systems/nestjs:zlinkHandlers');
  }
  for (const [name, content] of [
    ['TicTacToe.Ts/Server/Api/Handlers/authenticate-player-handler.ts', apiHandler],
    ['TicTacToe.Ts/Server/Play/Infrastructure/ZLink/Handlers/create-game-handler.ts', playHandler],
    ['TicTacToe.Ts/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/Handlers/tictactoe-game-timer-handler.ts', ticTacToeTimerHandler],
    ['Bingo.Ts/Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/bingo-room-timer-handler.ts', bingoTimerHandler]
  ]) {
    if (/zlink(?:Request|Send|Publish|SpotActorRequest|EntrySpotActorRequest|SpotTimer)Handler\([^;\n]*\)\([A-Z]/.test(content)) {
      violations.push(`${name}:manual-decorator-call`);
    }
  }
  for (const [name, content] of [
    ['TicTacToe.Ts/Server/Api/main.ts', apiMain],
    ['TicTacToe.Ts/Server/Play/main.ts', playMain],
    ['Bingo.Ts/Server/Api/main.ts', fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Api', 'main.ts'), 'utf8')],
    ['Bingo.Ts/Server/Play/main.ts', fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Play', 'main.ts'), 'utf8')],
    ['Bingo.Ts/Server/Registry/main.ts', fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Registry', 'main.ts'), 'utf8')]
  ]) {
    if (content.includes('zlinkHandlers')) {
      violations.push(name);
    }
  }
  for (const text of [
    'providers: [',
    "require('@nestjs/common')",
    'ZLinkModule.forRoot',
    'CreateGameHandler',
    'AuthenticatePlayerHandler',
    'PlayActorJoinGameHandler',
    'PlayActorPlaceMarkHandler'
  ]) {
    for (const [name, content] of [
      ['TicTacToe.Ts/Server/Api/main.ts', apiMain],
      ['TicTacToe.Ts/Server/Play/main.ts', playMain]
    ]) {
      if (content.includes(text)) {
        violations.push(`${name}:${text}`);
      }
    }
  }
  for (const text of [
    'zlinkDiscoverProviders',
    ".addHandlerGroup('play')",
    "@zlinkRequestHandler('play', PacketNames.createGame)",
    '@zlinkSpotTimerHandler({'
  ]) {
    if (playModule.includes(text) || playHandler.includes(text) || ticTacToeTimerHandler.includes(text)) {
      violations.push(`TicTacToe.manual:${text}`);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('Bingo TypeScript sample separates room lifecycle from pure bingo game rules', () => {
  const roomGame = fs.readFileSync(path.join(
    samplesRoot,
    'Bingo.Ts',
    'Server',
    'Play',
    'Domain',
    'Bingo',
    'bingo-room-game.ts'
  ), 'utf8');
  const bingoGame = fs.readFileSync(path.join(
    samplesRoot,
    'Bingo.Ts',
    'Server',
    'Play',
    'Domain',
    'Bingo',
    'bingo-game.ts'
  ), 'utf8');
  const required = [
    [bingoGame, 'class BingoGame'],
    [bingoGame, 'submitCard'],
    [bingoGame, 'drawNext'],
    [bingoGame, 'this.winners.push'],
    [roomGame, 'new BingoGame'],
    [roomGame, 'this.game.submitCard'],
    [roomGame, 'this.game.drawNext']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);
  const violations = [];
  for (const text of [
    'new BingoCard',
    'this.winners.push',
    'player.card.mark('
  ]) {
    if (roomGame.includes(text)) {
      violations.push(text);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('Bingo TypeScript sample normalizes wire room settings before creating room state', () => {
  const roomSpot = fs.readFileSync(path.join(
    samplesRoot,
    'Bingo.Ts',
    'Server',
    'Play',
    'Infrastructure',
    'ZLink',
    'Spots',
    'BingoRoomSpot',
    'bingo-room-spot.ts'
  ), 'utf8');
  const roomModels = fs.readFileSync(path.join(
    samplesRoot,
    'Bingo.Ts',
    'Server',
    'Play',
    'Domain',
    'Bingo',
    'bingo-room-models.ts'
  ), 'utf8');

  assert.match(roomModels, /function roomSettingsFromPayload/);
  assert.match(roomSpot, /const settings = request\.decode<unknown>/);
  assert.match(roomSpot, /roomSettingsFromPayload\(settings\)/);
  assert.doesNotMatch(roomSpot, /protobufSerializer\.deserialize/);
});

test('Bingo TypeScript sample exposes spot actor contracts explicitly', () => {
  const playModule = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Play', 'bingo-play-module.ts'), 'utf8');
  const roomSpot = fs.readFileSync(path.join(
    samplesRoot,
    'Bingo.Ts',
    'Server',
    'Play',
    'Infrastructure',
    'ZLink',
    'Spots',
    'BingoRoomSpot',
    'bingo-room-spot.ts'
  ), 'utf8');
  const entrySpot = fs.readFileSync(path.join(
    samplesRoot,
    'Bingo.Ts',
    'Server',
    'Play',
    'Infrastructure',
    'ZLink',
    'Spots',
    'EntrySpot',
    'bingo-entry-spot.ts'
  ), 'utf8');
  const matchHandler = fs.readFileSync(path.join(
    samplesRoot,
    'Bingo.Ts',
    'Server',
    'Play',
    'Infrastructure',
    'ZLink',
    'Spots',
    'EntrySpot',
    'Handlers',
    'match-bingo-actor-handler.ts'
  ), 'utf8');
  const submitHandler = fs.readFileSync(path.join(
    samplesRoot,
    'Bingo.Ts',
    'Server',
    'Play',
    'Infrastructure',
    'ZLink',
    'Spots',
    'BingoRoomSpot',
    'Handlers',
    'submit-bingo-card-handler.ts'
  ), 'utf8');
  const frameworkSpotContract = fs.readFileSync(path.join(
    workspaceRoot,
    'packages',
    'framework',
    'src',
    'contracts',
    'Spots',
    'ZLinkSpot.ts'
  ), 'utf8');
  const required = [
    [frameworkSpotContract, 'interface ZLinkSpot<TActor extends ZLinkActor = ZLinkActor>'],
    [frameworkSpotContract, 'interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor>'],
    [playModule, '.actorFactory(SampleNames.playerActorType, PlayerActorFactory)'],
    [playModule, '.addSpotMesh(SampleNames.roomSpotNode'],
    [playModule, '.addEntrySpot(BingoEntrySpot)'],
    [playModule, '.addSpotFactory(BingoRoomSpot)'],
    [roomSpot, 'implements ZLinkSpot<PlayerActorType>'],
    [roomSpot, 'onActorJoin(actor: PlayerActorType'],
    [roomSpot, 'onJoinedActor(actor: PlayerActorType'],
    [roomSpot, 'onLeaveActor(actor: PlayerActorType'],
    [roomSpot, 'onDisconnectActor(actor: PlayerActorType'],
    [entrySpot, 'implements ZLinkEntrySpot<PlayerActorType>'],
    [entrySpot, 'onJoinedActor(actor: PlayerActorType'],
    [entrySpot, 'onLeaveActor(actor: PlayerActorType'],
    [entrySpot, 'onDisconnectActor(actor: PlayerActorType'],
    [matchHandler, 'zlinkEntrySpotActorRequestHandler'],
    [matchHandler, 'entrySpot: () => BingoEntrySpot'],
    [matchHandler, 'actor: () => PlayerActor'],
    [matchHandler, 'packetName: PacketNames.matchBingoReq'],
    [matchHandler, 'implements ZLinkEntrySpotActorRequestHandler<BingoEntrySpotType, PlayerActorType, MatchBingoReq, MatchBingoRes>'],
    [submitHandler, 'zlinkSpotActorRequestHandler'],
    [submitHandler, 'spot: () => BingoRoomSpot'],
    [submitHandler, 'actor: () => PlayerActor'],
    [submitHandler, 'packetName: PacketNames.submitBingoCardReq'],
    [submitHandler, 'implements ZLinkSpotActorRequestHandler<BingoRoomSpotType, PlayerActorType, SubmitBingoCardReq, SubmitBingoCardRes>']
  ];
  const missing = required
    .filter(([content, text]) => !content.includes(text))
    .map(([, text]) => text);

  const violations = [];
  for (const [name, content] of [
    ['Bingo.Ts/Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/bingo-room-spot.ts', roomSpot],
    ['Bingo.Ts/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/bingo-entry-spot.ts', entrySpot]
  ]) {
    if (content.includes('addActorPacket')) {
      violations.push(name);
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('node TypeScript samples keep actor destroy in Entry Spot after room leave', () => {
  const cases = [
    {
      sample: 'Bingo.Ts',
      actor: ['Server', 'Play', 'Infrastructure', 'ZLink', 'Actors', 'player-actor.ts'],
      entrySpot: ['Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'EntrySpot', 'bingo-entry-spot.ts'],
      userSpot: ['Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'BingoRoomSpot', 'bingo-room-spot.ts'],
      readme: ['README.ko.md']
    },
    {
      sample: 'TicTacToe.Ts',
      actor: ['Server', 'Play', 'Infrastructure', 'ZLink', 'Actors', 'play-actor.ts'],
      entrySpot: ['Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'EntrySpot', 'play-entry-spot.ts'],
      userSpot: ['Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'TicTacToeGameSpot', 'tictactoe-game-spot.ts'],
      readme: ['README.ko.md']
    }
  ];
  const missing = [];

  for (const sample of cases) {
    const actor = fs.readFileSync(path.join(samplesRoot, sample.sample, ...sample.actor), 'utf8');
    const entrySpot = fs.readFileSync(path.join(samplesRoot, sample.sample, ...sample.entrySpot), 'utf8');
    const userSpot = fs.readFileSync(path.join(samplesRoot, sample.sample, ...sample.userSpot), 'utf8');
    const readme = fs.readFileSync(path.join(samplesRoot, sample.sample, ...sample.readme), 'utf8');
    const runSample = fs.readFileSync(path.join(samplesRoot, sample.sample, 'run_sample.sh'), 'utf8');

    for (const [label, content, text] of [
      ['actor', actor, 'destroyAfterEntrySpotJoin'],
      ['actor', actor, 'markForDestroyAfterRoomLeave'],
      ['actor', actor, 'markDisconnected'],
      ['entrySpot', entrySpot, 'onJoinedActor'],
      ['entrySpot', entrySpot, 'destroyActor'],
      ['entrySpot', entrySpot, 'onDisconnectActor'],
      ['userSpot', userSpot, 'leaveActor'],
      ['userSpot', userSpot, 'markForDestroyAfterRoomLeave'],
      ['userSpot', userSpot, 'onDisconnectActor'],
      ['readme', readme, '`leaveActor`'],
      ['readme', readme, '`destroyActor`'],
      ['readme', readme, '`onDisconnectActor`'],
      ['readme', readme, 'client self-check'],
      ['runner', runSample, 'node "${SCRIPT_DIR}/dist/Client/main.js"']
    ]) {
      if (!content.includes(text)) {
        missing.push(`${sample.sample}:${label}:${text}`);
      }
    }
    if (userSpot.includes('destroyActor')) {
      missing.push(`${sample.sample}:userSpot:destroyActor`);
    }
  }

  assert.deepEqual(missing, []);
});

test('node sample runners own server process orchestration', () => {
  const missing = [];
  for (const sample of topologySamples) {
    const runSample = fs.readFileSync(path.join(samplesRoot, sample, 'run_sample.sh'), 'utf8');
    const runSamplePs1 = fs.readFileSync(path.join(samplesRoot, sample, 'run_sample.ps1'), 'utf8');
    const client = fs.readFileSync(path.join(samplesRoot, sample, 'Client', 'main.ts'), 'utf8');
    for (const text of [
      'start_server',
      'wait_port',
      'trap cleanup EXIT',
      'node "${SCRIPT_DIR}/dist/Client/main.js"'
    ]) {
      if (!runSample.includes(text)) {
        missing.push(`${sample}:${text}`);
      }
    }
    for (const text of [
      'Start-Server',
      'Wait-Port',
      'Start-Process -FilePath "node"',
      'dist/Client/main.js'
    ]) {
      if (!runSamplePs1.includes(text)) {
        missing.push(`${sample}:ps1:${text}`);
      }
    }
    if (!client.includes('loadSampleConfig')) {
      missing.push(`${sample}:loadSampleConfig`);
    }
    if (/child_process|spawn\(|fork\(|execFile/.test(client)) {
      missing.push(`${sample}:client starts server process`);
    }
  }

  assert.deepEqual(missing, []);
});

test('node samples keep only contracts and shared sample configuration under Shared', () => {
  const violations = [];
  for (const sample of requiredSamples) {
    const sharedRoot = path.join(samplesRoot, sample, 'Shared');
    for (const file of sampleSourceFiles(sharedRoot)) {
      const relative = relativePath(path.join(samplesRoot, sample), file);
      if (!relative.startsWith('Shared/Contracts/')
        && relative !== 'Shared/Configuration/sample-names.ts') {
        violations.push(relative);
      }
    }
  }

  assert.deepEqual(violations, []);
});

test('node top-level sample runners execute every maintained sample', () => {
  const shellRunner = fs.readFileSync(path.join(samplesRoot, 'run_samples.sh'), 'utf8');
  const powershellRunner = fs.readFileSync(path.join(samplesRoot, 'run_samples.ps1'), 'utf8');
  const missing = [];

  for (const sample of requiredSamples) {
    if (!shellRunner.includes(`${sample}/run_sample.sh`)) {
      missing.push(`sh:${sample}`);
    }
    if (!powershellRunner.includes(`${sample}/run_sample.ps1`)) {
      missing.push(`ps1:${sample}`);
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
          violations.push(`${relativePath(samplesRoot, file)} matches ${pattern}`);
        }
      }
    }
  }

  assert.deepEqual(violations, []);
});

test('node samples do not keep unreachable TypeScript files', () => {
  const violations = findUnreachableSampleTypeScriptFiles();

  assert.deepEqual(violations, []);
});

test('node framework source tree does not keep emitted JavaScript beside TypeScript sources', () => {
  const srcRoot = path.join(workspaceRoot, 'packages', 'framework', 'src');
  const emitted = listFiles(srcRoot)
    .filter((file) => file.endsWith('.js'))
    .map((file) => relativePath(workspaceRoot, file))
    .sort();

  assert.deepEqual(emitted, []);
});

test(
  'node run_samples.sh executes every sample self-check',
  process.env.ZLINK_SKIP_NODE_SAMPLE_SELF_CHECK === '1'
    ? { skip: 'sample self-checks are not part of the framework CI release gate' }
    : {},
  () => {
    if (process.platform !== 'linux') {
      return;
    }

    const output = childProcess.execFileSync(path.join(samplesRoot, 'run_samples.sh'), {
      cwd: workspaceRoot,
      encoding: 'utf8'
    });

    assert.match(output, /node actor lifecycle sample gate completed/);
    for (const sample of requiredSamples) {
      assert.match(output, new RegExp(`PASS ${escapeRegExp(sample)}`));
    }
  }
);

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
      if (entry.name === 'dist' || entry.name === 'node_modules') {
        continue;
      }
      files.push(...sampleSourceFiles(fullPath));
    } else if (entry.isFile() && /\.(?:js|ts|mjs|cjs|md|sh|ps1|proto)$/.test(entry.name)) {
      files.push(fullPath);
    }
  }
  return files;
}

function listFiles(root) {
  const files = [];
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const fullPath = path.join(root, entry.name);
    if (entry.isDirectory()) {
      if (entry.name === 'dist' || entry.name === 'node_modules') {
        continue;
      }
      files.push(...listFiles(fullPath));
    } else if (entry.isFile()) {
      files.push(fullPath);
    }
  }
  return files;
}

function findUnreachableSampleTypeScriptFiles() {
  const files = new Set(listFiles(samplesRoot).filter((file) => file.endsWith('.ts')));
  const used = new Set();
  const queue = [];

  function add(file) {
    const normalized = path.normalize(file);
    if (files.has(normalized) && !used.has(normalized)) {
      used.add(normalized);
      queue.push(normalized);
    }
  }

  for (const sample of requiredSamples) {
    // Every role entry point is a `main.ts`; discover them dynamically so samples
    // with different server roles (Support, Dispatch, GameApi, CommerceApi, ...) are covered.
    for (const file of listFiles(path.join(samplesRoot, sample))) {
      if (path.basename(file) === 'main.ts') {
        add(file);
      }
    }
  }

  while (queue.length > 0) {
    const file = queue.shift();
    const content = fs.readFileSync(file, 'utf8');
    addDiscoveredProviderFiles(file, content, add, files);
    for (const specifier of importSpecifiers(content)) {
      const resolved = resolveSampleImport(file, specifier, files);
      if (resolved !== null) {
        add(resolved);
      }
    }
  }

  return [...files]
    .filter((file) => !used.has(file))
    .map((file) => relativePath(samplesRoot, file))
    .sort();
}

function addDiscoveredProviderFiles(file, content, add, files) {
  const discoveryPatterns = [/zlinkDiscoverProviders\(path\.join\(__dirname,\s*([^)]*)\)\)/g];
  if (content.includes('providerDiscovery')) {
    discoveryPatterns.push(/path\.join\(__dirname,\s*([^)]*)\)/g);
  }
  if (content.includes('zlinkModule(__dirname')) {
    for (const discoveredRoot of defaultZLinkProviderDiscoveryRoots(path.dirname(file))) {
      for (const candidate of files) {
        if (candidate.startsWith(`${discoveredRoot}${path.sep}`)) {
          add(candidate);
        }
      }
    }
  }

  for (const discoveryPattern of discoveryPatterns) {
    for (const match of content.matchAll(discoveryPattern)) {
      const parts = [...match[1].matchAll(/'([^']+)'/g)].map((part) => part[1]);
      if (parts.length === 0) {
        continue;
      }
      const discoveredRoot = path.resolve(path.dirname(file), ...parts);
      for (const candidate of files) {
        if (candidate.startsWith(`${discoveredRoot}${path.sep}`)) {
          add(candidate);
        }
      }
    }
  }
}

function defaultZLinkProviderDiscoveryRoots(roleRoot) {
  return [
    path.join(roleRoot, 'Handlers'),
    path.join(roleRoot, 'Infrastructure', 'ZLink', 'Handlers'),
    path.join(roleRoot, 'Infrastructure', 'ZLink', 'Spots', 'Handlers')
  ].filter((rootDir) => fs.existsSync(rootDir));
}

function importSpecifiers(content) {
  const specifiers = [];
  for (const pattern of [
    /require\(['"]([^'"]+)['"]\)/g,
    /from ['"]([^'"]+)['"]/g,
    /import\(['"]([^'"]+)['"]\)/g
  ]) {
    for (const match of content.matchAll(pattern)) {
      specifiers.push(match[1]);
    }
  }
  return specifiers;
}

function resolveSampleImport(fromFile, specifier, files) {
  if (!specifier.startsWith('.')) {
    return null;
  }
  const base = path.resolve(path.dirname(fromFile), specifier);
  for (const candidate of [
    `${base}.ts`,
    path.join(base, 'index.ts')
  ]) {
    if (files.has(candidate)) {
      return candidate;
    }
  }
  return null;
}

function assertOrdered(name, content, snippets) {
  let offset = 0;
  for (const snippet of snippets) {
    const index = content.indexOf(snippet, offset);
    assert.notEqual(index, -1, `${name} is missing ordered scenario snippet: ${snippet}`);
    offset = index + snippet.length;
  }
}

function readSample(sample, relative) {
  return fs.readFileSync(path.join(samplesRoot, sample, relative), 'utf8');
}

function relativePath(base, file) {
  return path.relative(base, file).split(path.sep).join('/');
}

function isAllowedBingoRawSessionCodecFile(relative) {
  return [
    'Bingo.Ts/Shared/Contracts/messages.ts',
    'Bingo.Ts/Shared/Contracts/protobuf-codec.ts',
    'Bingo.Ts/Server/Session/main.ts'
  ].includes(relative);
}

function isAllowedSampleRawBoundaryFile(relative) {
  return [
    'Bingo.Ts/Shared/Contracts/protobuf-codec.ts',
    'Bingo.Ts/Server/Api/bingo-api-module.ts',
    'Bingo.Ts/Server/Play/bingo-play-module.ts',
    'Bingo.Ts/Server/Session/bingo-session-module.ts',
    'Bingo.Ts/Server/Play/Infrastructure/ZLink/Matchmaking/redis-bingo-match-queue.ts',
    'TicTacToe.Ts/Server/Configuration/redis-room-route-store.ts'
  ].includes(relative);
}

function isAllowedBingoCodecConfigurationFile(relative) {
  return [
    'Bingo.Ts/Server/Api/bingo-api-module.ts',
    'Bingo.Ts/Server/Play/bingo-play-module.ts',
    'Bingo.Ts/Server/Session/bingo-session-module.ts'
  ].includes(relative);
}

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}
