const assert = require('node:assert/strict');
const childProcess = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const workspaceRoot = path.resolve(__dirname, '..', '..');
const samplesRoot = path.join(workspaceRoot, 'samples');
const commonSampleDocsRoot = path.resolve(workspaceRoot, '..', '..', 'doc', 'framework', 'common', 'sample');
const requiredSamples = [
  'TicTacToe.Ts',
  'Bingo.Ts',
  'DeliveryDispatch.Ts',
  'SupportChat.Ts',
  'GameQuest.Ts',
  'ShoppingMall.Ts'
];
const topologySamples = [
  'TicTacToe.Ts',
  'Bingo.Ts',
  'DeliveryDispatch.Ts',
  'SupportChat.Ts',
  'GameQuest.Ts',
  'ShoppingMall.Ts'
];

test('node samples define required files and use only common sample documents', () => {
  const missing = [];
  if (fs.existsSync(path.join(samplesRoot, 'README.ko.md'))) {
    missing.push('samples/README.ko.md must not duplicate the common sample specification');
  }
  for (const sample of requiredSamples) {
    for (const relative of ['package.json', 'run_sample.sh', 'run_sample.ps1']) {
      const target = path.join(samplesRoot, sample, relative);
      if (!fs.existsSync(target)) {
        missing.push(`${sample}/${relative}`);
      }
    }
    for (const obsolete of ['README.ko.md', 'sample-porting-inventory.ko.md']) {
      if (fs.existsSync(path.join(samplesRoot, sample, obsolete))) {
        missing.push(`${sample}/${obsolete} must not duplicate the common sample specification`);
      }
    }
  }
  if (!fs.existsSync(path.join(commonSampleDocsRoot, 'README.ko.md'))) {
    missing.push('framework/doc/framework/common/sample/README.ko.md');
  }
  if (!fs.existsSync(path.join(samplesRoot, 'run_samples.sh'))) {
    missing.push('run_samples.sh');
  }
  if (fs.existsSync(path.join(samplesRoot, 'shared'))) {
    missing.push('samples/shared must not hide sample logic');
  }

  assert.deepEqual(missing, []);
});

test('node topology samples implement the common sample role layout', () => {
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
      'Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/bingo-room-timer-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/Handlers/match-bingo-actor-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/Handlers/observe-bingo-events-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/stop-observing-bingo-events-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/submit-bingo-card-handler.ts',
      'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/bingo-entry-spot.ts',
      'Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/bingo-room-spot.ts',
      'Server/Play/main.ts',
      'Server/Session/Sessions/Handlers/authenticate-session-handler.ts',
      'Server/Session/Sessions/bingo-session.ts',
      'Server/Session/main.ts',
      'Server/Configuration/location-store.ts',
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
      'Server/Api/Handlers/open-conversation-handler.ts',
      'Server/Api/Handlers/authenticate-user-handler.ts',
      'Server/Api/supportchat-api-module.ts',
      'Server/Api/main.ts',
      'Server/Support/Domain/SupportChat/conversation.ts',
      'Server/Support/Domain/SupportChat/conversation-models.ts',
      'Server/Support/Domain/SupportChat/conversation-events.ts',
      'Server/Support/Domain/SupportChat/conversation-policy.ts',
      'Server/Support/Application/ConversationAssignment/support-conversation-allocator.ts',
      'Server/Support/Application/ConversationAssignment/agent-availability-directory.ts',
      'Server/Support/Application/ConversationAssignment/agent-assignment-service.ts',
      'Server/Support/Infrastructure/ZLink/Actors/support-user-actor.ts',
      'Server/Support/Infrastructure/ZLink/Actors/support-user-actor-factory.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/allocate-conversation-handler.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/ensure-agent-conversation-handler.ts',
      'Server/Support/Infrastructure/ZLink/Handlers/ensure-support-user-actor-handler.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Notifications/conversation-event-mapper.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Notifications/support-notification-publisher.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/conversation-create-request.ts',
      'Server/Support/Infrastructure/ZLink/Spots/EntrySpot/support-entry-spot.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/conversation-spot.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Handlers/conversation-idle-timer-handler.ts',
      'Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Handlers/conversation-actor-handlers.ts',
      'Server/Support/Infrastructure/ZLink/Spots/EntrySpot/support-entry-handlers.ts',
      'Server/Support/supportchat-support-module.ts',
      'Server/Support/main.ts',
      'Server/Session/Sessions/supportchat-session.ts',
      'Server/Session/supportchat-session-module.ts',
      'Server/Session/main.ts',
      'Server/Configuration/sample-config.ts',
      'Server/Configuration/sample-names.ts',
      'Server/runtime-support.ts',
      'Shared/Contracts/messages.ts'
    ],
    'DeliveryDispatch.Ts': [
      'Client/deliverydispatch-client-scenario.ts',
      'Client/main.ts',
      'Server/main.ts',
      'Server/Courier/courier-actor.ts',
      'Server/Courier/courier-entry-spot.ts',
      'Server/Courier/courier-module.ts',
      'Server/Courier/offer-delivery-handler.ts',
      'Server/CourierSession/courier-session.ts',
      'Server/CourierSession/courier-session-module.ts',
      'Server/DispatchApi/dispatch-api-module.ts',
      'Server/DispatchCenter/dispatch-center-module.ts',
      'Server/DispatchCenter/dispatch-worker.ts',
      'Server/Probe/probe.ts',
      'Server/Session/customer-session.ts',
      'Server/Tracking/tracking-module.ts',
      'Shared/Configuration/sample-names.ts',
      'Shared/Contracts/messages.ts'
    ],
    'GameQuest.Ts': [
      'Client/gamequest-client-scenario.ts',
      'Client/main.ts',
      'Server/main.ts',
      'Server/GameApi/game-api-module.ts',
      'Server/GameApi/game-api-server.ts',
      'Server/GameApi/Application/gameplay-action-service.ts',
      'Server/GameApi/Infrastructure/ZLink/gameplay-event-publisher.ts',
      'Server/QuestMission/Application/quest-owner-router.ts',
      'Server/QuestMission/Infrastructure/ZLink/gameplay-event-route-handler.ts',
      'Server/QuestMission/Infrastructure/ZLink/player-quest-spot-provisioner.ts',
      'Server/QuestMission/Infrastructure/ZLink/Spots/PlayerQuestSpot/player-quest-spot.ts',
      'Server/Registry/registry-module.ts',
      'Server/Shared/Store/quest-progress-store.ts',
      'Shared/Configuration/sample-names.ts',
      'Shared/Contracts/messages.ts'
    ],
    'ShoppingMall.Ts': [
      'Client/shoppingmall-client-scenario.ts',
      'Client/main.ts',
      'Server/main.ts',
      'Server/Shared/Store/order-store.ts',
      'Shared/Configuration/sample-names.ts',
      'Shared/Contracts/messages.ts'
    ]
  };
  const missing = [];
  for (const [sample, relatives] of Object.entries(expected)) {
    if (!topologySamples.includes(sample)) {
      continue;
    }
    for (const relative of relatives) {
      if (!fs.existsSync(path.join(samplesRoot, sample, relative))) {
        missing.push(`${sample}/${relative}`);
      }
    }
  }

  assert.deepEqual(missing, []);
});

test('GameQuest TypeScript sample registers required sample and provisions player quest spots', () => {
  assert.ok(requiredSamples.includes('GameQuest.Ts'));

  const names = readSample('GameQuest.Ts', 'Shared/Configuration/sample-names.ts');
  const publisher = readSample('GameQuest.Ts', 'Server/GameApi/Infrastructure/ZLink/gameplay-event-publisher.ts');
  const sessionModule = readSample('GameQuest.Ts', 'Server/GameApi/game-api-module.ts');
  const questModule = readSample('GameQuest.Ts', 'Server/QuestMission/gamequest-quest-module.ts');
  const provisioner = readSample(
    'GameQuest.Ts',
    'Server/QuestMission/Infrastructure/ZLink/player-quest-spot-provisioner.ts'
  );
  const spot = readSample(
    'GameQuest.Ts',
    'Server/QuestMission/Infrastructure/ZLink/Spots/PlayerQuestSpot/player-quest-spot.ts'
  );
  const spotHandlers = readSample(
    'GameQuest.Ts',
    'Server/QuestMission/Infrastructure/ZLink/Spots/PlayerQuestSpot/player-quest-spot-handlers.ts'
  );
  const missing = [];

  for (const [content, text] of [
    [names, 'questMissionRouteRid(playerId: string)'],
    [names, 'ownerIndex(playerId) === 0 ? \'mission-a\' : \'mission-b\''],
    [publisher, '.sendToNode(SampleNames.questMissionRouteChannel, questMissionRouteRid(event.playerId), message)'],
    [sessionModule, '.addSpotMesh(SampleNames.playerQuestSpotMesh)'],
    [questModule, '.addSpotFactory(PlayerQuestSpot)'],
    [provisioner, 'ZLINK_SPOT_MANAGER'],
    [provisioner, 'this.spots.getOrCreate(PlayerQuestSpot, spotRid, { playerId })'],
    [spot, 'private aggregate: PlayerQuestAggregate | undefined'],
    [spot, 'ensureAggregate(load: () => PlayerQuestAggregate)'],
    [spotHandlers, 'this.processor.rehydrate(message.playerId)'],
    [spotHandlers, 'spot.replaceAggregate(result.aggregate)'],
    [spotHandlers, 'this.processor.rehydrate(request.playerId)']
  ]) {
    if (!content.includes(text)) {
      missing.push(text);
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
    bingoAllocate: readSample('Bingo.Ts', 'Server/Play/Infrastructure/ZLink/Handlers/allocate-bingo-room-handler.ts'),
    bingoEnsureActor: readSample('Bingo.Ts', 'Server/Play/Infrastructure/ZLink/Handlers/ensure-player-actor-handler.ts'),
    bingoApiMatch: readSample('Bingo.Ts', 'Server/Api/Handlers/match-bingo-handler.ts'),
    bingoActorMatch: readSample('Bingo.Ts', 'Server/Play/Infrastructure/ZLink/Spots/EntrySpot/Handlers/match-bingo-actor-handler.ts'),
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
    ['Bingo API match', files.bingoApiMatch, 'ZLINK_ROUTE_CLIENT'],
    ['Bingo allocate', files.bingoAllocate, 'ZLINK_SPOT_MANAGER'],
    ['Bingo ensure actor', files.bingoEnsureActor, 'ZLINK_ACTOR_MANAGER'],
    ['Bingo actor match', files.bingoActorMatch, '.joinSpot(roomId'],
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
    ['TicTacToe session', files.ticTacToeSession, 'this.context.actors.bindOrGet(actorRef)'],
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
  assert.match(handler, /actor:\s*\(\)\s*=>\s*PlayerActor/);
  assert.match(handler, /packetName:\s*PacketNames\.stopObservingBingoEventsReq/);
  assert.match(room, /async stopObserving\(/);
  assert.doesNotMatch(room, /actorRequest\(PacketNames\.stopObservingBingoEventsReq/);
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

test('node common-spec samples expose buildable scenario entrypoints', () => {
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
        ? '--role'
        : sample === 'GameQuest.Ts'
          ? '--role'
          : '--role'],
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
  const allocator = readSample(
    'SupportChat.Ts',
    'Server/Support/Application/ConversationAssignment/support-conversation-allocator.ts'
  );
  const assignment = readSample(
    'SupportChat.Ts',
    'Server/Support/Application/ConversationAssignment/agent-assignment-service.ts'
  );
  const apiHandler = fs.readFileSync(
    path.join(samplesRoot, 'SupportChat.Ts', 'Server', 'Api', 'Handlers', 'open-conversation-handler.ts'),
    'utf8'
  );

  assert.match(allocator, /new Conversation\(/);
  assert.doesNotMatch(allocator, /ZLink|requestToChannel|actors\.get/);
  assert.match(assignment, /assignNextAgent\(\)/);
  assert.doesNotMatch(assignment, /ZLink|requestToChannel|actors\.get/);

  assert.match(apiHandler, /SampleNames\.supportChannel/);
  assert.match(apiHandler, /allocateConversation\(/);
  assert.match(apiHandler, /submit<AllocateConversationRes>/);
});

test('DeliveryDispatch TypeScript sample uses framework channel topology', () => {
  const clientScenario = readSample('DeliveryDispatch.Ts', 'Client/deliverydispatch-client-scenario.ts');
  const dispatchApiModule = readSample('DeliveryDispatch.Ts', 'Server/DispatchApi/dispatch-api-module.ts');
  const dispatchCenterModule = readSample('DeliveryDispatch.Ts', 'Server/DispatchCenter/dispatch-center-module.ts');
  const courierModule = readSample('DeliveryDispatch.Ts', 'Server/Courier/courier-module.ts');
  const courierSessionModule = readSample('DeliveryDispatch.Ts', 'Server/CourierSession/courier-session-module.ts');
  const courierSession = readSample('DeliveryDispatch.Ts', 'Server/CourierSession/courier-session.ts');
  const customerSession = readSample('DeliveryDispatch.Ts', 'Server/Session/customer-session.ts');
  const names = readSample('DeliveryDispatch.Ts', 'Shared/Configuration/sample-names.ts');
  const trackingModule = readSample('DeliveryDispatch.Ts', 'Server/Tracking/tracking-module.ts');
  const sessionModule = readSample('DeliveryDispatch.Ts', 'Server/Session/session-module.ts');
  const serverMain = readSample('DeliveryDispatch.Ts', 'Server/main.ts');
  const runSample = fs.readFileSync(path.join(samplesRoot, 'DeliveryDispatch.Ts', 'run_sample.sh'), 'utf8');

  assert.match(clientScenario, /BrowserHttpClient/);
  assert.match(clientScenario, /\.fetch<CreateDeliveryRes>\(\)/);
  assert.match(clientScenario, /\.fetch<ServerAssertionRes>\(\)/);
  assert.match(clientScenario, /customer\.request\(subscribeDelivery/);
  assert.match(clientScenario, /waitFor<DeliveryStatusNotify>/);
  assert.match(dispatchApiModule, /\.addClientServerChannel\(SampleNames\.dispatchChannel\)/);
  assert.match(dispatchApiModule, /\.enableClient\(\)/);
  assert.match(dispatchCenterModule, /\.enableServer\(config\.dispatchEndpoint\)/);
  assert.match(dispatchCenterModule, /\.addRouteMeshChannel\(SampleNames\.courierActorNodeRouteChannel\)/);
  assert.match(dispatchCenterModule, /\.addClientServerChannel\(SampleNames\.trackingChannel\)/);
  assert.match(courierModule, /\.addRouteMeshChannel\(SampleNames\.courierActorNodeRouteChannel\)/);
  assert.match(courierModule, /\.addSpotMesh\(SampleNames\.courierActorSpotMesh\)/);
  assert.match(courierSessionModule, /\.addRouteMeshChannel\(SampleNames\.courierActorNodeRouteChannel\)/);
  assert.match(courierSessionModule, /\.addSpotMesh\(SampleNames\.courierActorSpotMesh\)/);
  assert.match(trackingModule, /\.addClientServerChannel\(SampleNames\.trackingChannel\)/);
  assert.match(trackingModule, /\.addSpotMesh\(SampleNames\.customerActorSpotMesh\)/);
  assert.match(sessionModule, /\.addStreamNode\(SampleNames\.customerStreamNode\)/);
  assert.match(sessionModule, /\.addSpotMesh\(SampleNames\.customerActorSpotMesh\)/);
  assert.match(courierSession, /resolveActor\(\{ actorId: courierId \}\)/);
  assert.match(courierSession, /bindOrGet\(actorRef\)/);
  assert.match(customerSession, /resolveActor\(\{ actorId: CustomerId \}\)/);
  assert.match(names, /courierActorNodeRouteChannel: 'delivery-couriers'/);
  assert.match(names, /courierActorSpotMesh: 'delivery-couriers'/);
  assert.match(serverMain, /NestFactory\.createApplicationContext/);
  for (const role of ['tracking', 'customer-gateway', 'courier-session', 'courier-spot-node1', 'courier-spot-node2', 'dispatch']) {
    assert.match(serverMain, new RegExp(`role === '${role}'`));
  }
  assert.match(runSample, /DELIVERYDISPATCH_CENTER_ROUTE/);
  assert.match(runSample, /DELIVERYDISPATCH_SESSION_STREAM/);
  assert.match(runSample, /ws:\/\/127\.0\.0\.1/);
  assert.match(runSample, /DELIVERYDISPATCH_CENTER_ROUTE="tcp:\/\/127\.0\.0\.1/);
  assert.doesNotMatch(clientScenario, /requestToChannel|SAMPLE_ENDPOINT|support::request_line/);
  assert.doesNotMatch(serverMain, /courier-gateway/);
  assert.doesNotMatch(serverMain, /role === 'dispatch-api'|role === 'dispatch-center'|role === 'session'/);
  assert.doesNotMatch(names, /deliverydispatch\.courier/);
  assert.doesNotMatch(serverMain, /SAMPLE_ENDPOINT/);
});

test('GameQuest TypeScript sample uses framework channel topology', () => {
  const clientScenario = readSample('GameQuest.Ts', 'Client/gamequest-client-scenario.ts');
  const clientMain = readSample('GameQuest.Ts', 'Client/main.ts');
  const apiModule = readSample('GameQuest.Ts', 'Server/GameApi/game-api-module.ts');
  const questModule = readSample('GameQuest.Ts', 'Server/QuestMission/gamequest-quest-module.ts');
  const apiServer = readSample('GameQuest.Ts', 'Server/GameApi/game-api-server.ts');
  const gameplayService = readSample('GameQuest.Ts', 'Server/GameApi/Application/gameplay-action-service.ts');
  const gameplayDomain = readSample('GameQuest.Ts', 'Server/GameApi/Domain/gameplay-domain.ts');
  const gameplayPublisher = readSample('GameQuest.Ts', 'Server/GameApi/Infrastructure/ZLink/gameplay-event-publisher.ts');
  const questProcessor = readSample('GameQuest.Ts', 'Server/QuestMission/Application/quest-event-processor.ts');
  const questOwnerRouter = readSample('GameQuest.Ts', 'Server/QuestMission/Application/quest-owner-router.ts');
  const questDomain = readSample('GameQuest.Ts', 'Server/QuestMission/Domain/quest-domain.ts');
  const playerQuestProvisioner = readSample(
    'GameQuest.Ts',
    'Server/QuestMission/Infrastructure/ZLink/player-quest-spot-provisioner.ts'
  );
  const playerQuestSpot = readSample(
    'GameQuest.Ts',
    'Server/QuestMission/Infrastructure/ZLink/Spots/PlayerQuestSpot/player-quest-spot.ts'
  );
  const playerQuestSpotHandlers = readSample(
    'GameQuest.Ts',
    'Server/QuestMission/Infrastructure/ZLink/Spots/PlayerQuestSpot/player-quest-spot-handlers.ts'
  );
  const gameplayRouteHandler = readSample(
    'GameQuest.Ts',
    'Server/QuestMission/Infrastructure/ZLink/gameplay-event-route-handler.ts'
  );
  const ownerRouteHandlers = readSample(
    'GameQuest.Ts',
    'Server/QuestMission/Infrastructure/ZLink/quest-owner-route-handlers.ts'
  );
  const questStore = readSample('GameQuest.Ts', 'Server/Shared/Store/quest-progress-store.ts');
  const serverMain = readSample('GameQuest.Ts', 'Server/main.ts');
  const runSample = fs.readFileSync(path.join(samplesRoot, 'GameQuest.Ts', 'run_sample.sh'), 'utf8');
  const runSamplePs1 = fs.readFileSync(path.join(samplesRoot, 'GameQuest.Ts', 'run_sample.ps1'), 'utf8');

  assert.match(clientMain, /BrowserHttpClientFactory\.create\(config\.apiAHttpUrl\)/);
  assert.match(clientMain, /zlinkStreamConnectorFactory\.create/);
  assert.match(clientScenario, /apiAStream\.request\(killMonsterReq/);
  assert.match(clientScenario, /apiBReconnectStream\.request\(joinSessionReq\('player-alice'\)/);
  assert.match(clientScenario, /waitForStreamProjection\(apiBReconnectStream, 'player-alice'/);
  assert.match(clientScenario, /apiAStream\.request\(joinSessionReq/);
  assert.match(clientScenario, /waitFor<QuestCompletedNotify>/);
  assert.doesNotMatch(clientScenario, /requestToChannel|SampleNames\.questMissionRouteChannel|SAMPLE_ENDPOINT|support::request_line/);
  assert.match(apiModule, /\.addRouteMeshChannel\(SampleNames\.questMissionRouteChannel\)/);
  assert.doesNotMatch(apiModule, /\.connect\(/);
  assert.match(apiModule, /\.addStreamNode\(SampleNames\.playerStreamNode\)/);
  assert.match(apiServer, /http\.createServer/);
  assert.doesNotMatch(apiServer, /\/combat\/kill|\/quest\/progress/);
  assert.match(apiServer, /GameplayStateStore/);
  assert.match(apiServer, /GameQuestSelfCheckStore/);
  assert.match(gameplayService, /publishAndNotify/);
  assert.match(gameplayDomain, /monsterKilled/);
  assert.match(gameplayPublisher, /\.sendToNode\(SampleNames\.questMissionRouteChannel/);
  assert.match(gameplayPublisher, /\.submit\(\)/);
  assert.match(apiModule, /zlinkFramework\(\)/);
  assert.match(apiModule, /\.enableRouter\(actorSpotEndpoint, apiRid\)/);
  assert.match(questModule, /QuestEventProcessor/);
  assert.match(apiModule, /\.addSpotMesh\(SampleNames\.playerQuestSpotMesh\)/);
  assert.match(apiModule, /\.addEntrySpot\(GameQuestEntrySpot\)/);
  assert.match(questModule, /\.addSpotFactory\(PlayerQuestSpot\)/);
  assert.match(questModule, /\.addHandlerGroup\('quest-owner'\)/);
  assert.match(questDomain, /decide\(event: GameplayEventEnvelope/);
  assert.match(playerQuestProvisioner, /this\.spots\.getOrCreate\(PlayerQuestSpot/);
  assert.match(playerQuestProvisioner, /\.requestToSpot\(spot, request\)/);
  assert.match(gameplayRouteHandler, /@zlinkSendHandler\('quest-owner', PacketNames\.gameplayMsg\)/);
  assert.match(gameplayRouteHandler, /playerQuests\.send\(/);
  assert.doesNotMatch(gameplayRouteHandler, /processor\.process/);
  assert.match(ownerRouteHandlers, /playerQuests\.request/);
  assert.doesNotMatch(ownerRouteHandlers, /store\.(readProjection|syncProgress|deleteProjection|rebuildProjection)/);
  assert.match(playerQuestSpotHandlers, /zlinkSpotPacketHandler/);
  assert.match(playerQuestSpotHandlers, /processor\.process\(decodeGameplayPayload\(message\.payload\), aggregate\)/);
  assert.match(playerQuestSpotHandlers, /processor\.rehydrate\(request\.playerId\)/);
  assert.match(playerQuestSpotHandlers, /processor\.syncProgress\(request, aggregate\)/);
  assert.match(playerQuestSpotHandlers, /store\.rebuildProjection\(request\.playerId, request\.questId, this\.events\.read\(request\.playerId\)\)/);
  assert.match(questProcessor, /syncProgress\(request: SyncQuestProgressReq, aggregate: PlayerQuestAggregate\)/);
  assert.match(questOwnerRouter, /routeRid\(playerId: string\)/);
  assert.match(playerQuestProvisioner, /questMissionSpotRid\(playerId\)/);
  assert.match(playerQuestProvisioner, /ZLINK_SPOT_MANAGER/);
  assert.match(playerQuestSpot, /private aggregate: PlayerQuestAggregate \| undefined/);
  assert.match(playerQuestSpot, /ensureAggregate\(load: \(\) => PlayerQuestAggregate\)/);
  assert.match(playerQuestSpotHandlers, /processor\.rehydrate\(message\.playerId\)/);
  assert.match(playerQuestSpotHandlers, /spot\.replaceAggregate\(result\.aggregate\)/);
  assert.match(questProcessor, /PlayerQuestAggregate\.from\(stored\)/);
  assert.match(questStore, /recorded: boolean/);
  assert.match(questDomain, /eventType: 'QuestProgressed'/);
  assert.match(questDomain, /eventType: 'QuestCompleted'/);
  assert.match(questDomain, /eventType: 'QuestRewardGranted'/);
  assert.match(questStore, /encodePayload/);
  assert.match(clientScenario, /closeOwnerA\.closed \|\| closeOwnerB\.closed/);
  assert.match(serverMain, /playerQuests\.deactivate\(playerId\)/);
  assert.match(questDomain, /class PlayerQuestAggregate/);
  assert.match(questDomain, /conditionDecision/);
  assert.match(questDomain, /orderedDecision/);
  assert.match(questDomain, /QuestReconciled/);
  assert.match(clientScenario, /enter-ruins-too-early/);
  assert.match(clientScenario, /bobReconcileCompleted/);
  assert.match(questStore, /class GameplayStateStore/);
  assert.match(questStore, /class QuestEventStore/);
  assert.match(questStore, /class QuestReadModelStore/);
  assert.match(gameplayDomain, /Collected item count must be a positive integer/);
  assert.match(clientScenario, /invalid-negative-count/);
  assert.match(questDomain, /QuestStatuses/);
  assert.match(serverMain, /NestFactory\.createApplicationContext/);
  assert.match(serverMain, /role !== 'api-a' && role !== 'api-b' && role !== 'mission-a' && role !== 'mission-b'/);
  assert.match(serverMain, /createGameApiModule\(config, role\)/);
  assert.match(runSample, /start_role mission-a/);
  assert.match(runSample, /start_role mission-b/);
  assert.match(runSample, /start_role api-a/);
  assert.match(runSample, /start_role api-b/);
  assert.match(runSample, /wait_transport_endpoint mission-a "\$\{GAMEQUEST_MISSION_A_ROUTE\}"/);
  assert.match(runSample, /GAMEQUEST_MISSION_A_ROUTE="ipc:\/\//);
  assert.match(runSample, /wait_http api-a "\$\{GAMEQUEST_API_A_HTTP\}"/);
  assert.match(runSample, /GAMEQUEST_API_A_HTTP/);
  assert.match(runSample, /GAMEQUEST_API_B_HTTP/);
  assert.match(runSample, /GAMEQUEST_API_A_STREAM/);
  assert.match(runSample, /GAMEQUEST_API_B_STREAM/);
  assert.match(runSample, /GAMEQUEST_LOG_DIR/);
  assert.match(runSample, /grep -Rq "packet=GameplayMsg" "\$\{GAMEQUEST_LOG_DIR\}"/);
  assert.match(runSample, /grep -Rq "surface=spotActor\.\*packet=QuestCompletedNotify" "\$\{GAMEQUEST_LOG_DIR\}"/);
  assert.match(runSamplePs1, /@\("mission-a", "mission-b", "api-a", "api-b"\)/);
  assert.match(runSamplePs1, /ForEach-Object \{ Start-Role \$_/);
  assert.match(runSamplePs1, /Wait-Http -Url \$env:GAMEQUEST_API_A_HTTP/);
  assert.match(runSamplePs1, /Wait-Http -Url \$env:GAMEQUEST_API_B_HTTP/);
  assert.match(runSamplePs1, /if \(\$LASTEXITCODE -ne 0\) \{ throw "GameQuest client scenario failed" \}/);
  assert.match(runSamplePs1, /GAMEQUEST_LOG_DIR/);
  assert.match(runSamplePs1, /@\("create"/);
  assert.match(runSamplePs1, /"redis-cli", "PING"/);
  assert.doesNotMatch(runSamplePs1, /ready\.length >= 2/);
  assert.match(runSample, /GAMEQUEST_API_A_STREAM="ws:\/\/127\.0\.0\.1/);
  assert.match(runSample, /GAMEQUEST_MISSION_A_ROUTE="ipc:\/\/\$\{WORK_DIR\}/);
  assert.doesNotMatch(serverMain, /SAMPLE_ENDPOINT/);
});

test('ShoppingMall TypeScript sample uses framework channel topology', () => {
  const clientScenario = readSample('ShoppingMall.Ts', 'Client/shoppingmall-client-scenario.ts');
  const clientMain = readSample('ShoppingMall.Ts', 'Client/main.ts');
  const commerceApiModule = readSample('ShoppingMall.Ts', 'Server/CommerceApi/commerce-api-module.ts');
  const commerceApiServer = readSample('ShoppingMall.Ts', 'Server/CommerceApi/commerce-api-server.ts');
  const startOrderUseCase = readSample(
    'ShoppingMall.Ts',
    'Server/CommerceApi/Application/start-order-use-case.ts'
  );
  const workflowRouter = readSample(
    'ShoppingMall.Ts',
    'Server/CommerceApi/Infrastructure/ZLink/zlink-order-workflow-router.ts'
  );
  const messageContracts = readSample('ShoppingMall.Ts', 'Shared/Contracts/messages.ts');
  const workflowService = readSample(
    'ShoppingMall.Ts',
    'Server/OrderWorkflow/Application/OrderWorkflow/order-workflow-service.ts'
  );
  const orderDomain = readSample(
    'ShoppingMall.Ts',
    'Server/OrderWorkflow/Domain/ShoppingMall/order-domain.ts'
  );
  const orderWorkflowSpot = readSample(
    'ShoppingMall.Ts',
    'Server/OrderWorkflow/Infrastructure/ZLink/Spots/OrderWorkflowSpot/order-workflow-spot.ts'
  );
  const startOrderSpotHandler = readSample(
    'ShoppingMall.Ts',
    'Server/OrderWorkflow/Infrastructure/ZLink/Spots/OrderWorkflowSpot/Handlers/start-order-workflow-handler.ts'
  );
  const orderEvents = readSample('ShoppingMall.Ts', 'Server/Shared/Domain/order-events.ts');
  const workflowModule = readSample('ShoppingMall.Ts', 'Server/OrderWorkflow/shoppingmall-workflow-module.ts');
  const orderStore = readSample('ShoppingMall.Ts', 'Server/Shared/Store/order-store.ts');
  const serverMain = readSample('ShoppingMall.Ts', 'Server/main.ts');
  const runSample = fs.readFileSync(path.join(samplesRoot, 'ShoppingMall.Ts', 'run_sample.sh'), 'utf8');
  const runSamplePs1 = fs.readFileSync(path.join(samplesRoot, 'ShoppingMall.Ts', 'run_sample.ps1'), 'utf8');

  assert.match(clientMain, /ZLinkHttpClient\.create\(config\.apiAHttpUrl\)/);
  assert.match(clientMain, /ZLinkHttpClient\.create\(config\.apiBHttpUrl\)/);
  assert.match(clientScenario, /\.post\('\/orders\/start'\)/);
  assert.match(clientScenario, /\.get\(`\/orders\/\$\{orderId\}`\)/);
  assert.match(clientScenario, /shoppingmall-payment-failure=completed/);
  assert.match(clientScenario, /\.post\('\/self-check\/assert'\)/);
  assert.doesNotMatch(clientScenario, /requestToChannel|SampleNames\.orderWorkflowRouteChannel|SAMPLE_ENDPOINT|support::request_line/);
  assert.match(commerceApiModule, /zlinkFramework\(\)/);
  assert.match(commerceApiModule, /\.addClientServerChannel\(SampleNames\.orderWorkflowChannel\)/);
  assert.match(commerceApiModule, /\.enableClient\(\)/);
  for (const file of sampleSourceFiles(path.join(samplesRoot, 'ShoppingMall.Ts', 'Server', 'CommerceApi'))) {
    assert.doesNotMatch(fs.readFileSync(file, 'utf8'), /OrderWorkflow\//);
  }
  assert.match(commerceApiServer, /http\.createServer/);
  assert.match(commerceApiServer, /\/orders\/start/);
  assert.match(commerceApiServer, /StartOrderUseCase/);
  assert.match(startOrderUseCase, /store\.reserveOrder\(request\)/);
  assert.doesNotMatch(startOrderUseCase, /PacketNames|\.packetName\(/);
  assert.match(workflowRouter, /private request<TResponse>/);
  assert.match(workflowRouter, /start\(request: StartOrderWorkflowReq\)/);
  assert.doesNotMatch(workflowRouter, /\.packetName\(/);
  assert.match(messageContracts, /@ZLinkPacket\(PacketNames\.startOrderWorkflowReq\)/);
  assert.match(workflowRouter, /requestToChannel\(SampleNames\.orderWorkflowChannel, payload\)/);
  assert.match(workflowModule, /zlinkFramework\(\)/);
  assert.match(workflowModule, /\.addClientServerChannel\(SampleNames\.orderWorkflowChannel\)/);
  assert.match(workflowModule, /\.enableServer\(workflowChannelEndpointForRole\(role, config\)\)/);
  assert.match(workflowModule, /\.addSpotMesh\(SampleNames\.orderWorkflowSpotMesh\)/);
  assert.match(workflowModule, /\.addSpotFactory\(OrderWorkflowSpot\)/);
  assert.match(workflowModule, /OrderWorkflowService/);
  assert.match(workflowModule, /\.addHandlerGroup\('workflow'\)/);
  assert.match(orderWorkflowSpot, /class OrderWorkflowSpot implements ZLinkSpot/);
  assert.match(startOrderSpotHandler, /ZLinkSpotRequestHandler<OrderWorkflowSpot/);
  assert.match(startOrderSpotHandler, /PacketNames\.startOrderWorkflowReq/);
  assert.match(workflowService, /start\(request: StartOrderWorkflowReq/);
  assert.match(workflowService, /continue\(request: \{ orderId: string \}/);
  assert.match(orderDomain, /class OrderAggregate/);
  assert.match(orderEvents, /interface StoredOrderEvent/);
  assert.match(orderEvents, /payload: readonly number\[\]/);
  assert.match(orderStore, /SHOPPINGMALL_WORK_DIR/);
  assert.match(orderStore, /class ExpectedVersionConflict/);
  assert.match(orderStore, /interrupted after inventory effect/);
  assert.match(orderStore, /overlap writer rejected/);
  assert.match(orderStore, /payload: \[\.\.\.Buffer\.from/);
  assert.match(serverMain, /NestFactory\.createApplicationContext/);
  assert.match(serverMain, /SampleNames\.workflowA/);
  assert.match(serverMain, /SampleNames\.apiB/);
  assert.match(runSample, /SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT/);
  assert.match(runSample, /SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT/);
  assert.match(runSample, /SHOPPINGMALL_API_A_HTTP/);
  assert.match(runSample, /SHOPPINGMALL_API_B_HTTP/);
  assert.match(runSample, /start_role workflow-a/);
  assert.match(runSample, /start_role workflow-b/);
  assert.match(runSample, /start_role api-a/);
  assert.match(runSample, /start_role api-b/);
  assert.match(runSample, /wait_http api-a "\$\{SHOPPINGMALL_API_A_HTTP\}"/);
  assert.match(runSample, /wait_http api-b "\$\{SHOPPINGMALL_API_B_HTTP\}"/);
  assert.match(runSamplePs1, /@\("workflow-a", "workflow-b", "api-a", "api-b"\)/);
  assert.match(runSamplePs1, /ForEach-Object \{ Start-Role \$_/);
  assert.match(runSamplePs1, /Wait-Http -Url \$env:SHOPPINGMALL_API_A_HTTP/);
  assert.match(runSamplePs1, /Wait-Http -Url \$env:SHOPPINGMALL_API_B_HTTP/);
  assert.match(commerceApiModule, /SHOPPINGMALL_LOG_DIR/);
  assert.match(workflowModule, /SHOPPINGMALL_LOG_DIR/);
  assert.match(runSample, /SHOPPINGMALL_LOG_DIR/);
  assert.match(runSamplePs1, /SHOPPINGMALL_LOG_DIR/);
  assert.match(runSamplePs1, /"redis-cli", "ping"/);
  assert.match(runSamplePs1, /@\("create"/);
  assert.match(runSample, /tcp:\/\/127\.0\.0\.1/);
  assert.doesNotMatch(serverMain, /SAMPLE_ENDPOINT/);
});

test('common-spec TypeScript clients do not import server modules', () => {
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

  assert.deepEqual(missing, []);
  assert.deepEqual(hiddenServerRuntime, []);
});

test('TicTacToe TypeScript sample builds and exposes basic TypeScript roles', () => {
  const packageJson = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'package.json'), 'utf8');
  const tsconfig = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'tsconfig.json'), 'utf8');
  const client = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Client', 'main.ts'), 'utf8');
  const api = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Api', 'main.ts'), 'utf8');
  const play = fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'main.ts'), 'utf8');
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

test('TicTacToe TypeScript sample implements the common game state contract', () => {
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
    [joinHandler, 'entrySpot.join(actor, actor, request.roomId)'],
    [moveHandler, 'spot.placeMark(actor, request.cell)'],
    [gameSpot, 'gameStateNotify(state)'],
    [playActor, 'this.context.boundSession'],
    [playSession, 'this.context.actors.bindOrGet(actorRef)'],
    [client, 'payload.state.status === GameStatus.InProgress'],
    [client, 'stateOf(client1FinalMove).status === GameStatus.Won']
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

test('Bingo TypeScript sample uses route mesh peers and location store registration where supported', () => {
  const api = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Api', 'main.ts'), 'utf8');
  const apiModule = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Api', 'bingo-api-module.ts'), 'utf8');
  const play = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Play', 'main.ts'), 'utf8');
  const playModule = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Play', 'bingo-play-module.ts'), 'utf8');
  const session = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Session', 'main.ts'), 'utf8');
  const sessionModule = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Session', 'bingo-session-module.ts'), 'utf8');
  const locationStore = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Configuration', 'location-store.ts'), 'utf8');
  const required = [
    [locationStore, 'ZLinkRedisLocationStore'],
    [locationStore, 'redisEndpoint'],
    [locationStore, 'redisKeyPrefix'],
    [apiModule, '.addLocationStore(createBingoLocationStore(config))'],
    [apiModule, 'bingoLocationOptions()'],
    [apiModule, '.addRouteMeshChannel(SampleNames.playChannel'],
    [apiModule, '.enableClient()'],
    [apiModule, '.addClientServerChannel(SampleNames.apiChannel'],
    [apiModule, '.enableServer(config.apiEndpoint)'],
    [playModule, '.addLocationStore(createBingoLocationStore(config))'],
    [playModule, 'bingoLocationOptions()'],
    [playModule, '.addRouteMeshChannel(SampleNames.playChannel'],
    [playModule, '.enableRouter(config.playRouteEndpoint)'],
    [sessionModule, '.addLocationStore(createBingoLocationStore(endpoints))'],
    [sessionModule, 'bingoLocationOptions()']
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
    [apiModule, '.useDiscovery()'],
    [apiModule, '.addRegistryEndpoint('],
    [playModule, '.useDiscovery()'],
    [playModule, '.addRegistryEndpoint('],
    [sessionModule, '.useDiscovery()'],
    [sessionModule, '.addRegistryEndpoint('],
    [apiModule, '.useInMemoryLocationStores()'],
    [playModule, '.useInMemoryLocationStores()'],
    [sessionModule, '.useInMemoryLocationStores()'],
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
  const drawIndex = roomSpot.indexOf('new BingoNumberDrawnNotify(');
  const finishedBranchIndex = roomSpot.indexOf('if (drawn.finished)');
  const endedIndex = roomSpot.indexOf('new BingoGameEndedNotify(');

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
    ['Bingo.Ts', 'Server/Session/main.ts', 'BINGO_SESSION_B_ENDPOINT']
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
    'samples/Bingo.Ts/Client/drain-match-probe.ts',
    'samples/Bingo.Ts/run_sample.ps1',
    'samples/Bingo.Ts/run_sample.sh',
    'samples/DeliveryDispatch.Ts/Client/deliverydispatch-client-scenario.ts',
    'samples/DeliveryDispatch.Ts/Server/DispatchCenter/dispatch-worker.ts',
    'samples/DeliveryDispatch.Ts/Server/Probe/probe.ts',
    'samples/GameQuest.Ts/Client/gamequest-client-scenario.ts',
    'samples/GameQuest.Ts/Server/GameApi/gamequest-session.ts',
    'samples/GameQuest.Ts/Server/GameApi/Infrastructure/ZLink/gameplay-event-publisher.ts',
    'samples/SupportChat.Ts/Client/supportchat-client-scenario.ts',
    'samples/SupportChat.Ts/Server/Probe/main.ts',
    'samples/SupportChat.Ts/Server/Support/notification-delivery-log.ts',
    'samples/SupportChat.Ts/Server/runtime-support.ts',
    'samples/ShoppingMall.Ts/Client/shoppingmall-client-scenario.ts',
    'samples/ShoppingMall.Ts/Server/CommerceApi/Infrastructure/ZLink/zlink-order-workflow-router.ts'
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

test('node top-level sample runner only retries transient port binding failures', () => {
  const runSamples = fs.readFileSync(path.join(samplesRoot, 'run_samples.sh'), 'utf8');
  const violations = [
    /\bretry sample\b/,
    /\brun_sample\s*\(/
  ]
    .filter((pattern) => pattern.test(runSamples))
    .map((pattern) => pattern.source);

  assert.deepEqual(violations, []);
  assert.match(runSamples, /BIND_RETRY_PATTERN=.*EADDRINUSE/);
  assert.match(runSamples, /BIND_RETRY_PATTERN=.*Address already in use/);
  assert.match(runSamples, /BIND_RETRY_PATTERN=.*errno=98/);
  assert.match(runSamples, /for attempt in 1 2 3/);
  assert.match(runSamples, /if ! grep -Eq "\$\{BIND_RETRY_PATTERN\}" "\$\{output\}"; then[\s\S]*return "\$\{status\}"/);
  assert.match(runSamples, /node sample transient bind failure; retrying/);
});

test('node RegistryMessaging e2e endpoints do not hide local routing failures with retry loops', () => {
  const registryMessagingRoot = path.join(workspaceRoot, 'e2e', 'RegistryMessaging');
  const endpointFiles = [
    'Server/Provider/Endpoints/provider-endpoints.ts',
    'Server/Consumer/Endpoints/consumer-endpoints.ts',
    'Server/Workflow/Endpoints/workflow-endpoints.ts'
  ];
  const violations = [];
  for (const relative of endpointFiles) {
    const content = fs.readFileSync(path.join(registryMessagingRoot, relative), 'utf8');
    for (const pattern of [
      /\bWithRetry\b/,
      /\bretryUntil\b/,
      /Timed out waiting for .*route/,
      /while\s*\(\s*Date\.now\(\)\s*<\s*deadline\s*\)/
    ]) {
      if (pattern.test(content)) {
        violations.push(`${relative}:${pattern.source}`);
      }
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
    'client1.request(new AuthenticateReq({ accessToken: BingoSamplePlayers.player1 }))',
    'client2.request(new AuthenticateReq({ accessToken: BingoSamplePlayers.player2 }))',
    '2. player-1 matches first',
    "client1.request(new MatchBingoReq({ mode: 'two-player' }))",
    'const client1MatchRes = await client1SelfJoinNotify',
    'client1MatchRes.roomId.length > 0',
    '4-6. player-2 joins the same room',
    '.waitFor<PlayerJoinedNotify>',
    '.waitFor<StateEnvelope>(PacketNames.gameStartedNotify)',
    '.waitFor<StateEnvelope>(PacketNames.gameStartedNotify)',
    'client2MatchResTask',
    "client2.request(new MatchBingoReq({ mode: 'two-player' }))",
    'await client2MatchResTask',
    '7. Both clients submit deterministic cards',
    '.request(new SubmitBingoCardReq',
    '.request(new SubmitBingoCardReq',
    'stateOf(client1Card).players.length === 2',
    '8. Number drawing is server-driven',
    'requireSameDraw(client1Draw.payload, client2Draw.payload, drawTask.drawSeq)',
    '9. Both clients receive the final finished state',
    'client1EndedTask',
    'ended.status === BingoRoomStatus.Finished'
  ]);

  assertOrdered('TicTacToe.Ts/Client/tictactoe-client-scenario.ts', ticTacToeClient, [
    '1. Create the room through API',
    ".body(createGameHttpReq('match-ready'))",
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
  const bingoBrowserCodec = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Shared', 'Contracts', 'protobuf-browser-codec.ts'), 'utf8');
  const bingoFrameworkCodec = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Shared', 'Contracts', 'protobuf-framework-codec.ts'), 'utf8');
  const bingoProto = fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Shared', 'Contracts', 'bingo_messages.proto'), 'utf8');
  const required = [
    [ticTacToeClient, 'zlinkStreamConnectorFactory.create'],
    [ticTacToePlay, '.addStreamNode(SampleNames.playStream'],
    [bingoClient, 'bingoProtobuf'],
    [bingoSessionModule, '.use(bingoFrameworkProtobuf)'],
    [bingoSessionModule, '.codecs()'],
    [bingoBrowserCodec, 'createZlinkStreamProtobufEnvelopeCodec'],
    [bingoFrameworkCodec, 'createZlinkProtobufEnvelopeCodec'],
    [bingoCodec, 'BingoGeneratedProtobufCodec.encode'],
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
  for (const sample of ['Bingo.Ts', 'TicTacToe.Ts']) {
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
      if (/payload\.getString\(|(?<!ZLink)Message\.from\(|Buffer\.from\(/.test(content)
          && !isAllowedSampleRawBoundaryFile(relative)) {
        violations.push(`${relative}:raw-codec-helper`);
      }
      if (/writeVarint|readVarint|schemaTable|manualSchema|wireType/.test(content)
          && relative !== 'Bingo.Ts/Shared/Contracts/bingo-messages.generated.ts') {
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

test('TicTacToe uses manual handler registration and other samples keep automatic registration', () => {
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
  const nestPackage = sampleSourceFiles(path.join(workspaceRoot, 'packages', 'nestjs', 'src'))
    .map((file) => fs.readFileSync(file, 'utf8'))
    .join('\n');
  const required = [
    [nestPackage, 'export function zlinkRequestHandler'],
    [nestPackage, 'export function zlinkSpotTimerHandler'],
    [apiModule, '.addRequestHandler(PacketNames.authenticatePlayerReq, AuthenticatePlayerHandler)'],
    [playModule, '.addRequestHandler(PacketNames.createGameReq, CreateGameHandler)'],
    [playModule, 'CreateGameHandler'],
    [playModule, 'PlayActorJoinGameHandler'],
    [playModule, 'PlayActorPlaceMarkHandler'],
    [fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'EntrySpot', 'play-entry-spot.ts'), 'utf8'),
      'this.context.handlers.addActorPacket(PlayActorJoinGameHandler, PlayActor)'],
    [fs.readFileSync(path.join(samplesRoot, 'TicTacToe.Ts', 'Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'TicTacToeGameSpot', 'tictactoe-game-spot.ts'), 'utf8'),
      'this.context.handlers.addActorPacket(PlayActorPlaceMarkHandler, PlayActor)'],
    [ticTacToeTimerHandler, 'class TicTacToeGameTimerHandler'],
    [bingoTimerHandler, 'class BingoRoomTimerHandler'],
    [bingoTimerHandler, '@zlinkSpotTimerHandler({'],
    [fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Api', 'bingo-api-module.ts'), 'utf8'), '.addHandlerGroup(\'api\')'],
    [bingoPlayModule, '.addHandlerGroup(\'play\')'],
    [fs.readFileSync(path.join(samplesRoot, 'DeliveryDispatch.Ts', 'Server', 'Session', 'customer-status-handler.ts'), 'utf8'),
      '@zlinkEntrySpotActorSendHandler({'],
    [fs.readFileSync(path.join(samplesRoot, 'DeliveryDispatch.Ts', 'Server', 'Session', 'customer-status-handler.ts'), 'utf8'),
      'packetName: PacketNames.deliveryStatusUpdated'],
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
    ['Bingo.Ts/Server/Session/main.ts', fs.readFileSync(path.join(samplesRoot, 'Bingo.Ts', 'Server', 'Session', 'main.ts'), 'utf8')]
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
  for (const sample of requiredSamples.filter((name) => name !== 'TicTacToe.Ts')) {
    for (const file of sampleSourceFiles(path.join(samplesRoot, sample))) {
      const content = fs.readFileSync(file, 'utf8');
      if (/\.add(?:Request|Send|Publish)Handler\(/.test(content)
        || /\.addSubscribe\(/.test(content)
        || /\.actor(?:Request|Send)\(/.test(content)
        || /\.packet\(/.test(content)) {
        violations.push(`${relativePath(samplesRoot, file)}:manual-handler-registration`);
      }
    }
  }

  assert.deepEqual(missing, []);
  assert.deepEqual(violations, []);
});

test('only TicTacToe uses manual server-to-server connections', () => {
  const ticTacToeServer = sampleSourceFiles(path.join(samplesRoot, 'TicTacToe.Ts', 'Server'))
    .map((file) => fs.readFileSync(file, 'utf8'))
    .join('\n');
  assert.match(ticTacToeServer, /\.addRequestHandler\(/);
  assert.match(ticTacToeServer, /\.connectRouter\(/);

  const violations = [];
  for (const sample of requiredSamples.filter((name) => name !== 'TicTacToe.Ts')) {
    for (const file of sampleSourceFiles(path.join(samplesRoot, sample, 'Server'))) {
      const content = fs.readFileSync(file, 'utf8');
      if (/\.connect\(/.test(content)
        || /\.connectRouter\(/.test(content)
        || /\.connectPeerPub\(/.test(content)
        || /\.enableClient\(\s*[^)]/.test(content)
        || /\.enableSubscriber\(\s*[^)]/.test(content)
        || /\.enablePubSub\([^,\n]+,[^,\n]+,[^)]+\)/.test(content)
        || /ZLinkHttpClient\.create\(/.test(content)) {
        violations.push(relativePath(samplesRoot, file));
      }
    }
  }
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
    [roomSpot, 'onActorJoin(actorId: string'],
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
      userSpot: ['Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'BingoRoomSpot', 'bingo-room-spot.ts']
    },
    {
      sample: 'TicTacToe.Ts',
      actor: ['Server', 'Play', 'Infrastructure', 'ZLink', 'Actors', 'play-actor.ts'],
      entrySpot: ['Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'EntrySpot', 'play-entry-spot.ts'],
      userSpot: ['Server', 'Play', 'Infrastructure', 'ZLink', 'Spots', 'TicTacToeGameSpot', 'tictactoe-game-spot.ts']
    }
  ];
  const missing = [];

  for (const sample of cases) {
    const actor = fs.readFileSync(path.join(samplesRoot, sample.sample, ...sample.actor), 'utf8');
    const entrySpot = fs.readFileSync(path.join(samplesRoot, sample.sample, ...sample.entrySpot), 'utf8');
    const userSpot = fs.readFileSync(path.join(samplesRoot, sample.sample, ...sample.userSpot), 'utf8');
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
      ['runner', runSample, 'scripts/browser-e2e/run-sample.mjs']
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
    const roleRunner = sample === 'DeliveryDispatch.Ts' || sample === 'GameQuest.Ts' || sample === 'ShoppingMall.Ts';
    const clientCommand = sample === 'ShoppingMall.Ts'
      ? 'node "${SCRIPT_DIR}/dist/Client/main.js"'
      : 'scripts/browser-e2e/run-sample.mjs';
    const shellRequired = roleRunner
      ? [
          'start_role',
          sample === 'DeliveryDispatch.Ts'
            ? 'wait_port'
            : sample === 'GameQuest.Ts'
              ? 'wait_transport_endpoint'
              : 'wait_tcp_endpoint',
          'trap cleanup EXIT',
          clientCommand
        ]
      : sample === 'SupportChat.Ts'
        ? [
            'start_server',
            'wait_tcp',
            'trap cleanup EXIT',
            clientCommand
          ]
      : [
          'start_server',
          'wait_port',
          'trap cleanup EXIT',
          clientCommand
        ];
    for (const text of shellRequired) {
      if (!runSample.includes(text)) {
        missing.push(`${sample}:${text}`);
      }
    }
    const browserPsCommand = 'scripts/browser-e2e/run-sample.mjs';
    const psRequired = sample === 'DeliveryDispatch.Ts'
      ? [
          'Start-Role',
          'Wait-Http',
          'DELIVERYDISPATCH_COURIER_STREAM',
          'DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE',
          'DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE',
          browserPsCommand
        ]
      : sample === 'ShoppingMall.Ts'
      ? [
          'Start-Role',
          'Wait-Topology',
          'SHOPPINGMALL_API_A_HTTP',
          'SHOPPINGMALL_API_B_HTTP',
          'SHOPPINGMALL_WORKFLOW_A_ENDPOINT',
          'SHOPPINGMALL_WORKFLOW_B_ENDPOINT',
          'dist/Client/main.js'
        ]
      : sample === 'GameQuest.Ts'
        ? [
            'Start-Role',
            'Wait-Topology',
            'GAMEQUEST_API_A_HTTP',
            'GAMEQUEST_API_B_HTTP',
            'GAMEQUEST_MISSION_A_ROUTE',
            'GAMEQUEST_MISSION_B_ROUTE',
            browserPsCommand
          ]
      : sample === 'SupportChat.Ts'
        ? [
            'Start-Server',
            'Wait-Tcp',
            browserPsCommand
          ]
      : [
          'Start-Server',
          'Wait-Port',
          browserPsCommand
        ];
    for (const text of psRequired) {
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

test('node sample runners isolate Redis and application ports without Docker volumes', () => {
  const violations = [];
  const redisHelper = fs.readFileSync(path.join(workspaceRoot, 'e2e/redis-container.sh'), 'utf8');
  for (const [label, pattern] of [
    ['redis-helper:publish-deadline', /redis_container_endpoint\(\)[\s\S]*deadline=/],
    ['redis-helper:publish-wait', /redis_container_endpoint\(\)[\s\S]*while \(\( SECONDS < deadline \)\)/],
    ['redis-helper:published-host-port', /redis_container_endpoint\(\)[\s\S]*HostPort/],
    ['redis-helper:reachable-host-port', /redis_container_endpoint\(\)[\s\S]*\/dev\/tcp\/127\.0\.0\.1/]
  ]) {
    if (!pattern.test(redisHelper)) {
      violations.push(label);
    }
  }
  for (const sample of topologySamples) {
    const shell = fs.readFileSync(path.join(samplesRoot, sample, 'run_sample.sh'), 'utf8');
    const powershell = fs.readFileSync(path.join(samplesRoot, sample, 'run_sample.ps1'), 'utf8');

    for (const [label, content, pattern] of [
      ['sh:dedicated-redis-helper', shell, /source .*e2e\/redis-container\.sh/],
      ['sh:dedicated-redis-create', shell, /start_redis_container/],
      ['sh:redis-cleanup', shell, /docker rm -fv/],
      ['ps1:dynamic-app-ports', powershell, /TcpListener/],
      ['ps1:docker-timeout', powershell, /Invoke-Docker|WaitForExit\(/],
      ['ps1:redis-tmpfs', powershell, /--tmpfs["',\s]+\/data/],
      ['ps1:dynamic-redis-port', powershell, /127\.0\.0\.1::6379/],
      ['ps1:redis-running', powershell, /State\.Running/],
      ['ps1:redis-pong', powershell, /redis-cli[\s\S]*PONG/],
      ['ps1:redis-cleanup', powershell, /rm["',\s]+-f["',\s]+-v/]
    ]) {
      if (!pattern.test(content)) {
        violations.push(`${sample}:${label}`);
      }
    }
    if (/127\.0\.0\.1:\d{4,5}/.test(powershell)) {
      violations.push(`${sample}:ps1:fixed-application-port`);
    }
  }
  for (const sample of ['DeliveryDispatch.Ts', 'GameQuest.Ts', 'ShoppingMall.Ts', 'SupportChat.Ts']) {
    const shell = fs.readFileSync(path.join(samplesRoot, sample, 'run_sample.sh'), 'utf8');
    if (!/print_failure_logs/.test(shell) || !/tail -n 80/.test(shell)) {
      violations.push(`${sample}:sh:hidden-role-failure`);
    }
  }

  assert.deepEqual(violations, []);
});

test('framework aggregate runners never remove Redis containers or processes owned by another run', () => {
  const shellRunner = fs.readFileSync(path.join(samplesRoot, 'run_samples.sh'), 'utf8');
  const powershellRunner = fs.readFileSync(path.join(samplesRoot, 'run_samples.ps1'), 'utf8');
  const e2eRunner = fs.readFileSync(path.join(workspaceRoot, 'e2e/run_e2e_all.sh'), 'utf8');
  const redisHelper = fs.readFileSync(path.join(workspaceRoot, 'e2e/redis-container.sh'), 'utf8');
  const frameworkRoot = path.resolve(workspaceRoot, '..', '..');

  for (const [label, content] of [
    ['samples:sh', shellRunner],
    ['samples:ps1', powershellRunner],
    ['e2e:sh', e2eRunner],
    ['redis-helper:sh', redisHelper],
    ['dotnet:samples', fs.readFileSync(path.join(frameworkRoot, 'languages/dotnet/samples/run_samples.sh'), 'utf8')],
    ['dotnet:e2e', fs.readFileSync(path.join(frameworkRoot, 'languages/dotnet/e2e/run_e2e_all.sh'), 'utf8')],
    ['java:samples', fs.readFileSync(path.join(frameworkRoot, 'languages/java/samples/run_samples.sh'), 'utf8')],
    ['java:e2e', fs.readFileSync(path.join(frameworkRoot, 'languages/java/e2e/run_e2e_all.sh'), 'utf8')],
    ['kotlin:e2e', fs.readFileSync(path.join(frameworkRoot, 'languages/java/e2e-kotlin/run_e2e_all.sh'), 'utf8')],
    ['cpp:samples', fs.readFileSync(path.join(frameworkRoot, 'languages/cpp/samples/run_samples.sh'), 'utf8')],
    ['cpp:e2e', fs.readFileSync(path.join(frameworkRoot, 'languages/cpp/e2e/run_e2e_all.sh'), 'utf8')]
  ]) {
    assert.doesNotMatch(content, /zlink_redis_cleanup_scope|docker ps -a|pkill\s/,
      `${label} must only clean resources created by its own sample or E2E run`);
  }
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

test('node run_samples.sh executes every sample self-check', () => {
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
});

test('node cross-language smoke covers bidirectional channel fanout route stream drain and store paths', () => {
  const smoke = fs.readFileSync(path.join(workspaceRoot, 'cross-language', 'node_dotnet_smoke.js'), 'utf8');
  const required = [
    'requestToChannel',
    'sendToChannel',
    ".publish('profiles'",
    'nodePublisherToDotnetFanoutSubscriber',
    'dotnetPublisherToNodeFanoutSubscriber',
    'nodeRouteClientToDotnetRouteServer',
    'dotnetRouteClientToNodeRouteServer',
    'nodeConnectorToDotnetStreamServer',
    'dotnetConnectorToNodeStreamServer',
    'nodeConnectorObservesDotnetSessionClosing',
    'dotnetConnectorObservesNodeSessionClosing',
    'nodeDotnetRedisLocationRows'
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
  // The Bingo runner invokes this second public stream client after Play A enters drain.
  add(path.join(samplesRoot, 'Bingo.Ts', 'Client', 'drain-match-probe.ts'));

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
    /import ['"]([^'"]+)['"]/g,
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
