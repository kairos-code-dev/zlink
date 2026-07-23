const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const workspaceRoot = path.resolve(__dirname, '..', '..');
const nodeDocRoot = path.resolve(workspaceRoot, '..', '..', 'doc', 'framework', 'node');
const specPath = path.join(
  nodeDocRoot,
  '..',
  'spec',
  'server',
  'languages',
  'node',
  '02-handler-interfaces.ko.md'
);
const declarationsRoot = path.join(workspaceRoot, 'packages', 'framework', 'dist', 'contracts');
const internalLocationCodecHelpers = new Set([
  // §10 keeps these declarations only as a labeled, non-normative snapshot.
  'IZLinkLocationRuntimeQuery',
  'IZLinkLocationStore',
  'IZLinkPeerLocationResolver',
  'ZLinkSessionPacketDispatcher',
  'tryParseZLinkLocationAutoConnectType',
  'tryParseZLinkLocationRole',
  'zlinkLocationAutoConnectTypeName',
  'zlinkLocationRoleName'
]);

test('framework contract declarations cover handler interface catalog exports', () => {
  const spec = fs.readFileSync(specPath, 'utf8');
  const declarations = readTree(declarationsRoot);
  const missing = [];

  for (const name of exportedCatalogNames(frameworkCatalog(spec)).filter((name) => !internalLocationCodecHelpers.has(name))) {
    const declarationPattern = new RegExp(`\\b(?:interface|type|enum|function)\\s+${name}\\b`);
    if (!declarationPattern.test(declarations)) {
      missing.push(name);
    }
  }

  assert.deepEqual(missing.sort(), []);
});

test('framework runtime exports decorator factories and enums from the catalog', () => {
  const framework = require('../../packages/framework/dist');
  const spec = fs.readFileSync(specPath, 'utf8');
  const missing = [];

  for (const name of runtimeCatalogNames(frameworkCatalog(spec)).filter((name) => !internalLocationCodecHelpers.has(name))) {
    if (!(name in framework)) {
      missing.push(name);
    }
  }

  assert.deepEqual(missing.sort(), []);
});

test('framework public root does not expose direct runtime start hosts', () => {
  const framework = require('../../packages/framework/dist');
  const hiddenNames = [
    'ZLinkFrameworkRuntimeHost',
    'ZLinkRegistryRuntime',
    'ZLinkStreamBindingRuntime'
  ];

  const exposed = hiddenNames.filter((name) => name in framework);

  assert.deepEqual(exposed, []);
});

test('stream connector public root does not expose raw frame or header codecs', () => {
  const connector = require('../../packages/stream-connector/dist');
  const exposed = ['ZlinkStreamFrameCodec', 'ZlinkStreamHeaderCodec']
    .filter((name) => name in connector);

  assert.deepEqual(exposed, []);
});

test('worker options expose the formal scheduler limits', () => {
  const declarations = readTree(declarationsRoot);
  const workerOptions = declarationBody(declarations, 'ZLinkWorkerOptions');

  assert.equal(workerOptions.includes('maxThreads'), true);
  assert.equal(workerOptions.includes('maxQueueLength'), true);
  assert.equal(workerOptions.includes('minThreads'), true);
  assert.equal(workerOptions.includes('idleTimeoutMs'), true);
});

test('location and relocation stores have separate public registration surfaces', () => {
  const declarations = readTree(declarationsRoot);
  const frameworkOptions = declarationBody(declarations, 'ZLinkFrameworkOptions');
  const relocationStore = declarationBody(declarations, 'ZLinkRelocationStore');

  assert.equal(frameworkOptions.includes('addLocationStore'), true);
  assert.equal(frameworkOptions.includes('addRelocationStore'), true);
  assert.equal(relocationStore.includes('putRelocation'), true);
  assert.equal(relocationStore.includes('getRelocation'), true);
  assert.equal(relocationStore.includes('renewRelocation'), true);
  assert.equal(relocationStore.includes('deleteRelocation'), true);
  assert.equal(frameworkOptions.includes('addRedis'), false);
  assert.equal(frameworkOptions.includes('addStores'), false);
});

test('diagnostics options do not expose inert native diagnostics configuration', () => {
  const declarations = readTree(declarationsRoot);
  const diagnosticsOptions = declarationBody(declarations, 'ZLinkDiagnosticsOptions');

  assert.equal(diagnosticsOptions.includes('includeNativeDiagnostics'), false);
});

test('monitoring options expose only common-spec socket location and Spot sources', () => {
  const contracts = fs.readFileSync(
    path.join(workspaceRoot, 'packages', 'framework', 'src', 'contracts', 'Eventing', 'Contracts.ts'),
    'utf8'
  );
  const spec = fs.readFileSync(specPath, 'utf8');

  assert.doesNotMatch(contracts, /\bregistry\?:\s*ZLinkPollingMonitoringRegistration/);
  assert.doesNotMatch(spec, /\bregistry\?:\s*ZLinkPollingMonitoringRegistration/);
});

test('framework configuration surface does not expose codec callback options', () => {
  const text = [
    readTree(declarationsRoot),
    fs.readFileSync(path.join(workspaceRoot, 'packages', 'framework', 'src', 'contracts', 'Configuration', 'Builders.ts'), 'utf8'),
    fs.readFileSync(path.join(workspaceRoot, 'packages', 'framework', 'src', 'contracts', 'Configuration', 'Registration.ts'), 'utf8'),
    fs.readFileSync(path.join(workspaceRoot, 'packages', 'nestjs', 'src', 'index.ts'), 'utf8')
  ].join('\n');
  const forbidden = [
    [/codecs\s*\(\s*configure/, 'codecs(configure) builder callback'],
    [/readonly\s+codecs\?:\s*\([^=]/, 'registration codecs callback property'],
    [/codecs\s*:\s*\([^=]/, 'module codecs callback property']
  ];
  const offenders = [];

  for (const [pattern, reason] of forbidden) {
    if (pattern.test(text)) {
      offenders.push(reason);
    }
  }

  assert.deepEqual(offenders.sort(), []);
});

test('NestJS module options expose only builder-created opaque configuration', () => {
  const declarations = readTree(path.join(workspaceRoot, 'packages', 'nestjs', 'dist'));
  const moduleOptions = declarationBody(declarations, 'ZLinkModuleOptions');
  const forbidden = [
    'clientServerChannels',
    'fanoutChannels',
    'routerMeshes',
    'spotNodes',
    'streams',
    'channels',
    'routeChannels',
    'streamNodes'
  ];
  const exposed = forbidden.filter((name) => moduleOptions.includes(name));

  assert.deepEqual(exposed, []);
});

test('NestJS public declarations do not export object-shaped module option types', () => {
  const declarations = readTree(path.join(workspaceRoot, 'packages', 'nestjs', 'dist'));
  const forbiddenExports = [
    'ZLinkNestClientServerChannelOptions',
    'ZLinkNestFanoutChannelOptions',
    'ZLinkNestRouterMeshOptions'
  ];
  const exposed = forbiddenExports.filter((name) =>
    new RegExp(`\\bexport\\s+interface\\s+${name}\\b`).test(declarations)
  );

  assert.deepEqual(exposed, []);
});

test('framework package exports only the public root contract', () => {
  const packageJson = JSON.parse(
    fs.readFileSync(path.join(workspaceRoot, 'packages', 'framework', 'package.json'), 'utf8'));

  assert.deepEqual(Object.keys(packageJson.exports).sort(), ['.']);
  assert.equal(packageJson.exports['.'].default, './dist/index.js');
  assert.equal(packageJson.exports['.'].types, './dist/index.d.ts');
});

test('NestJS package declarations stay inside declared public package boundaries', () => {
  const packageJson = JSON.parse(
    fs.readFileSync(path.join(workspaceRoot, 'packages', 'nestjs', 'package.json'), 'utf8'));
  const declarations = readTree(path.join(workspaceRoot, 'packages', 'nestjs', 'dist'));

  assert.deepEqual(Object.keys(packageJson.exports).sort(), ['.']);
  assert.deepEqual(packageJson.files, ['dist']);
  assert.equal(packageJson.exports['.'].default, './dist/index.js');
  assert.equal(packageJson.exports['.'].types, './dist/index.d.ts');
  assert.doesNotMatch(declarations, /@zlink-systems\/framework\/nest-integration/);
});

test('spot actor lifecycle handler registration API is not public', () => {
  const declarations = readTree(declarationsRoot);
  const workspaceText = [
    declarations,
    readTree(path.join(workspaceRoot, 'samples')),
    readTree(nodeDocRoot)
  ].join('\n');
  const removedNames = [
    'addActorJoin',
    'addPostActorJoined',
    'addActorLeft',
    'SpotActorJoinHandler',
    'PostActorJoinedHandler',
    'ActorLeftHandler',
    'ZLinkSpotActorJoinHandler',
    'ZLinkSpotPostActorJoinedHandler',
    'ZLinkSpotActorLeftHandler'
  ];

  const remaining = removedNames.filter((name) => workspaceText.includes(name));

  assert.deepEqual(remaining, []);
});

test('entry spot public surface exposes create lifecycle and actor join admission but no spot create callback', () => {
  const declarations = readTree(declarationsRoot);
  const entrySpot = declarationBody(declarations, 'ZLinkEntrySpot');
  const spotActorLifecycle = declarationBody(declarations, 'ZLinkSpotActorLifecycle');
  const entryContext = declarationBody(declarations, 'ZLinkEntrySpotContext');

  assert.equal(interfaceExtends(entrySpot, 'ZLinkSpot'), false);
  assert.equal(interfaceExtends(entrySpot, 'ZLinkSpotActorLifecycle'), true);
  assert.equal(entrySpot.includes('onCreate?'), false);
  assert.equal(spotActorLifecycle.includes('onActorJoin'), true);
  assert.equal(entrySpot.includes('onCreateActor'), true);
  assert.equal(spotActorLifecycle.includes('onJoinedActor'), true);
  assert.equal(spotActorLifecycle.includes('onLeaveActor'), true);
  assert.equal(entryContext.includes('leaveActor'), false);
  assert.equal(entryContext.includes('destroyActor'), true);
  assert.equal(declarationBody(declarations, 'ZLinkSpotContext').includes('destroyActor'), false);
  assert.equal(entryContext.includes('close('), false);
});

test('actor join and one-way calls expose only their target terminators', () => {
  const actorContracts = fs.readFileSync(
    path.join(workspaceRoot, 'packages', 'framework', 'src', 'contracts', 'Actors', 'ZLinkActorFactory.ts'),
    'utf8');
  const boundSessionContracts = fs.readFileSync(
    path.join(workspaceRoot, 'packages', 'framework', 'src', 'contracts', 'Streams', 'BoundSessionContracts.ts'),
    'utf8');

  const actorJoinCall = declarationBody(actorContracts, 'ZLinkActorJoinCall');
  const actorJoinSpotCall = declarationBody(actorContracts, 'ZLinkActorJoinSpotCall');
  const actorJoinEntrySpotCall = declarationBody(actorContracts, 'ZLinkActorJoinEntrySpotCall');

  assert.equal(actorJoinCall.includes('submit<TReply'), true);
  assert.match(actorJoinCall, /yield<TReply = unknown>\(signal\?: AbortSignal\): Promise<ZLinkActorJoinResult<TReply>>/);
  assert.equal(actorContracts.includes('ZLinkActorYieldJoinCall'), false);
  assert.equal(interfaceExtends(actorJoinSpotCall, 'ZLinkActorJoinCall'), true);
  assert.equal(interfaceExtends(actorJoinEntrySpotCall, 'ZLinkActorJoinCall'), true);
  const boundSessionSendCall = declarationBody(boundSessionContracts, 'ZLinkBoundSessionSendCall');
  assert.match(boundSessionSendCall, /submit\(signal\?: AbortSignal\): Promise<ZLinkSubmitResult>/);
  assert.equal(boundSessionSendCall.includes('yield('), false);
});

test('spot manager create surface exposes ZLinkMessage and typed request overloads only', () => {
  const declarations = readTree(declarationsRoot);
  const spotManager = declarationBody(declarations, 'ZLinkSpotManager');

  assert.match(spotManager, /create<TSpot extends ZLinkSpot>\(\s*meshName: string,\s*spotType: Type<TSpot>,\s*signal\?: AbortSignal\s*\): Promise<ZLinkSpotCreateResult>/);
  assert.match(spotManager, /create<TSpot extends ZLinkSpot>\(\s*meshName: string,\s*spotType: Type<TSpot>,\s*request: ZLinkMessage,\s*signal\?: AbortSignal\s*\): Promise<ZLinkSpotCreateResult>/);
  assert.match(spotManager, /create<TSpot extends ZLinkSpot, TRequest>\(\s*meshName: string,\s*spotType: Type<TSpot>,\s*request: TRequest,\s*signal\?: AbortSignal\s*\): Promise<ZLinkSpotCreateResult>/);
  assert.match(spotManager, /getOrCreate<TSpot extends ZLinkSpot>\(\s*meshName: string,\s*spotType: Type<TSpot>,\s*spotRid: RoutingId,\s*signal\?: AbortSignal\s*\): Promise<ZLinkSpotCreateResult>/);
  assert.match(spotManager, /getOrCreate<TSpot extends ZLinkSpot>\(\s*meshName: string,\s*spotType: Type<TSpot>,\s*spotRid: RoutingId,\s*request: ZLinkMessage,\s*signal\?: AbortSignal\s*\): Promise<ZLinkSpotCreateResult>/);
  assert.match(spotManager, /getOrCreate<TSpot extends ZLinkSpot, TRequest>\(\s*meshName: string,\s*spotType: Type<TSpot>,\s*spotRid: RoutingId,\s*request: TRequest,\s*signal\?: AbortSignal\s*\): Promise<ZLinkSpotCreateResult>/);
  const oldOptionalAnyRequest = ['request?:', 'unknown'].join(' ');
  assert.equal(spotManager.includes(oldOptionalAnyRequest), false);
  assert.equal(spotManager.includes(`${oldOptionalAnyRequest} | ZLinkMessage`), false);
});

test('location wire enums retain numeric values while Node-facing result enums use strings', () => {
  const framework = require('../../packages/framework/dist');
  const expectedLocationEnums = {
    ZLinkLocationAutoConnectType: {
      Invalid: 0,
      RouteMesh: 1,
      Fanout: 2
    },
    ZLinkLocationRole: {
      Invalid: 0,
      Spot: 2,
      Router: 3,
      Dealer: 4,
      Pub: 5,
      Sub: 6
    },
    ZLinkRouteKind: {
      Invalid: 0,
      ActorSession: 1,
      SpotName: 2,
      FrameworkRoute: 3
    },
    ZLinkLocationKind: {
      Invalid: 0,
      Peer: 1,
      Spot: 2,
      Actor: 3,
      Route: 4
    },
    ZLinkLocationWriteIntent: {
      NewClaim: 1,
      Renew: 2,
      Takeover: 3
    },
    ZLinkLocationWriteStatus: {
      Stored: 'stored',
      IgnoredStale: 'ignoredStale',
      RejectedConflict: 'rejectedConflict'
    },
    ZLinkLocationChangeType: {
      Upserted: 'upserted',
      Removed: 'removed',
      Expired: 'expired'
    },
    ZLinkLocationTopologyState: {
      Discovered: 1,
      Connecting: 2,
      Ready: 3,
      Lost: 4,
      Error: 5,
      Stopped: 6
    },
    ZLinkSpotKind: {
      Invalid: 'invalid',
      Entry: 'entry',
      User: 'user'
    }
  };

  for (const [enumName, expected] of Object.entries(expectedLocationEnums)) {
    assert.deepEqual(pickEnumValues(framework[enumName], Object.keys(expected)), expected, enumName);
  }
  assert.deepEqual(
    Object.keys(framework.ZLinkLocationAutoConnectType)
      .filter((name) => Number.isNaN(Number(name)))
      .sort(),
    Object.keys(expectedLocationEnums.ZLinkLocationAutoConnectType).sort()
  );

  assert.equal(framework.zlinkLocationAutoConnectTypeName, undefined);
  assert.equal(framework.zlinkLocationRoleName, undefined);
});

test('formal RouteMesh declarations exclude removed topology builders', () => {
  const frameworkDeclarations = readTree(declarationsRoot);
  const nestDeclarations = readTree(path.join(workspaceRoot, 'packages', 'nestjs', 'dist'));
  const removedNames = [
    'ZLinkClientServerChannelBuilder',
    'ZLinkRouteMeshChannelBuilder',
    'ZLinkNestClientServerChannelBuilder',
    'addClientServerChannel'
  ];

  for (const name of removedNames) {
    assert.equal(frameworkDeclarations.includes(name), false, `framework declaration retained ${name}`);
    assert.equal(nestDeclarations.includes(name), false, `Nest declaration retained ${name}`);
  }
});

test('framework error kind values and retriable defaults match the shared table', () => {
  const framework = require('../../packages/framework/dist');
  const expected = [
    ['ActorRouteNotFound', 'actorRouteNotFound', 0, false],
    ['ActorCreateFailed', 'actorCreateFailed', 1, false],
    ['ActorAlreadyExists', 'actorAlreadyExists', 2, false],
    ['ActorTypeMismatch', 'actorTypeMismatch', 3, false],
    ['SpotCreateFailed', 'spotCreateFailed', 4, false],
    ['SpotRouteNotFound', 'spotRouteNotFound', 5, false],
    ['SpotTypeMismatch', 'spotTypeMismatch', 6, false],
    ['ActorSessionNotBound', 'actorSessionNotBound', 7, false],
    ['HandlerNotFound', 'handlerNotFound', 8, false],
    ['RouteHandlerNotFound', 'routeHandlerNotFound', 9, false],
    ['ActorDispatchHandlerNotFound', 'actorDispatchHandlerNotFound', 10, false],
    ['PayloadDecodeFailed', 'payloadDecodeFailed', 11, false],
    ['RouteNotConnected', 'routeNotConnected', 12, true],
    ['RequestTargetNotFound', 'requestTargetNotFound', 13, false],
    ['RequestRejected', 'requestRejected', 14, false],
    ['RequestProtocolError', 'requestProtocolError', 15, false],
    ['RequestFailed', 'requestFailed', 16, false],
    ['WorkerQueueFull', 'workerQueueFull', 17, false],
    ['WorkerTimedOut', 'workerTimedOut', 18, false],
    ['WorkerFailed', 'workerFailed', 19, false],
    ['ActorLocationStale', 'actorLocationStale', 20, true],
    ['ActorCreateRejected', 'actorCreateRejected', 21, false]
  ];

  assert.equal(Object.keys(framework.ZLinkFrameworkErrorKind).length, expected.length);
  for (const [name, stringValue, numericValue, retriable] of expected) {
    const kind = framework.ZLinkFrameworkErrorKind[name];
    assert.equal(kind, stringValue, name);
    assert.equal(framework.ZLINK_FRAMEWORK_ERROR_KIND_VALUES[kind], numericValue, name);
    assert.equal(framework.isZLinkFrameworkErrorRetriableByDefault(kind), retriable, name);
  }
});

test('location contract declarations fix store resolver runtime query watch and row shapes', () => {
  const declarations = readTree(declarationsRoot);
  const locationStore = declarationBody(declarations, 'ZLinkLocationStore');
  const spotLocation = declarationBody(declarations, 'ZLinkSpotLocation');
  const actorLocation = declarationBody(declarations, 'ZLinkActorLocation');
  const actorKey = declarationBody(declarations, 'ZLinkActorLocationKey');
  const actorFilter = declarationBody(declarations, 'ZLinkActorLocationFilter');
  const changed = declarationBody(declarations, 'ZLinkLocationChanged');
  const runtimeQuery = declarationBody(declarations, 'ZLinkLocationRuntimeQuery');

  assert.match(interfaceHeader(declarations, 'ZLinkLocationStore'), /extends\s+ZLinkMeshNodeLocationStore,\s*ZLinkSpotLocationStore,\s*ZLinkActorLocationStore,\s*ZLinkOwnerLeaseStore,\s*ZLinkActorTransferStore/);
  assert.doesNotMatch(interfaceHeader(declarations, 'ZLinkLocationStore'), /ZLink(?:Peer|Route)LocationStore/);
  assert.match(locationStore, /removeAllByOwner\(ownerId: string, signal\?: AbortSignal\): Promise<bigint>/);
  assert.equal(/\bremove(?:Peer|Spot|Actor|Route)?ByOwner\b/.test(locationStore), false);

  assert.match(declarationBody(declarations, 'ZLinkPeerLocationResolver'), /listLivePeers\(filter: ZLinkPeerLocationFilter, signal\?: AbortSignal\): Promise<readonly ZLinkPeerLocation\[]>/);
  assert.match(declarationBody(declarations, 'ZLinkSpotHandleResolver'), /resolveSpotHandle\(meshName: string, spotRid: RoutingId, signal\?: AbortSignal\): Promise<SpotHandle \| undefined>/);
  assert.match(declarationBody(declarations, 'ZLinkActorSpotHandleResolver'), /resolveActorSpotHandle\(meshName: string, actorId: string, signal\?: AbortSignal\): Promise<SpotHandle \| undefined>/);
  assert.equal(declarations.includes('interface SpotRef'), false);
  assert.equal(declarations.includes('IZLink' + 'SpotAddressResolver'), false);
  assert.equal(declarations.includes('ZLink' + 'SpotAddress'), false);
  assert.equal(declarations.includes('IZLinkRouteLocationResolver'), false);
  assert.equal(declarations.includes('IZLinkActorRefResolver'), false);

  assert.match(runtimeQuery, /listPeerLocations\(filter: ZLinkPeerLocationFilter, signal\?: AbortSignal\): Promise<readonly ZLinkPeerLocation\[]>/);
  assert.match(runtimeQuery, /listSpotLocations\(\s*filter: ZLinkSpotLocationFilter,\s*page\?: ZLinkPageRequest,\s*signal\?: AbortSignal\s*\): Promise<ZLinkLocationPage<ZLinkSpotLocation>>/);
  assert.match(runtimeQuery, /listActorLocations\(\s*filter: ZLinkActorLocationFilter,\s*page\?: ZLinkPageRequest,\s*signal\?: AbortSignal\s*\): Promise<ZLinkLocationPage<ZLinkActorLocation>>/);
  assert.match(runtimeQuery, /listRouteLocations\(\s*filter: ZLinkRouteLocationFilter,\s*page\?: ZLinkPageRequest,\s*signal\?: AbortSignal\s*\): Promise<ZLinkLocationPage<ZLinkRouteLocation>>/);
  assert.equal(/\blist(?:Peers|Spots|Actors|Routes)\(/.test(runtimeQuery), false);

  for (const field of [
    'meshName', 'spotRid', 'spotGeneration', 'ownerNodeRid',
    'ownerNodeGeneration', 'spotKind', 'spotType', 'ownerId', 'updatedAt'
  ]) {
    assert.match(spotLocation, new RegExp(`readonly ${field}:`));
  }
  assert.doesNotMatch(spotLocation, /readonly (?:nodeRid|routeEndpoint|generation):/);

  assert.match(changed, /readonly key: ZLinkLocationKey/);
  assert.equal(changed.includes('locationKey'), false);
  assert.equal(changed.includes('string'), false);
  assert.match(declarations, /export type ZLinkLocationKey = \{\s*readonly kind: ZLinkLocationKind\.Peer;\s*readonly key: ZLinkPeerLocationKey;\s*\} \| \{\s*readonly kind: ZLinkLocationKind\.Spot;\s*readonly key: ZLinkSpotLocationKey;\s*\} \| \{\s*readonly kind: ZLinkLocationKind\.Actor;\s*readonly key: ZLinkActorLocationKey;\s*\} \| \{\s*readonly kind: ZLinkLocationKind\.Route;\s*readonly key: ZLinkRouteLocationKey;\s*\};/);

  assert.match(actorLocation, /readonly actorId: string/);
  assert.match(actorLocation, /readonly meshName: string/);
  assert.match(actorLocation, /readonly actorType: string/);
  assert.match(actorLocation, /readonly actorRef: ActorRef/);
  assert.match(actorLocation, /readonly ownerNodeRid: RoutingId/);
  assert.match(actorLocation, /readonly ownerNodeGeneration: bigint/);
  assert.match(actorLocation, /readonly spotRid: RoutingId/);
  assert.match(actorLocation, /readonly spotGeneration: bigint/);
  assert.match(actorLocation, /readonly spotKind: ZLinkSpotKind/);
  assert.match(actorLocation, /readonly membershipEpoch: bigint/);
  assert.doesNotMatch(actorLocation, /readonly (?:nodeRid|locationKind|spotMeshName|generation):/);
  assert.equal(/readonly (?:actorType|actorRef|ownerNodeRid|ownerNodeGeneration|spotRid|spotGeneration|spotKind|membershipEpoch)\?:/.test(actorLocation), false);
  assert.match(actorKey, /readonly meshName: string/);
  assert.match(actorKey, /readonly actorId: string/);
  assert.equal(actorKey.includes('actorType'), false);
  assert.match(actorFilter, /readonly locationKind\?: ZLinkSpotKind/);
});

test('actor convenience declarations expose directory snapshot and bind-or-get shapes', () => {
  const declarations = readTree(declarationsRoot);
  const actorClient = declarationBody(declarations, 'ZLinkActorClient');
  const actorSendCall = declarationBody(declarations, 'ZLinkActorSendCall');
  const actorRequestCall = declarationBody(declarations, 'ZLinkActorRequestCall');
  const directory = declarationBody(declarations, 'ZLinkActorDirectory');
  const placement = declarationBody(declarations, 'ZLinkActorPlacement');
  const sessionActors = declarationBody(declarations, 'ZLinkSessionActors');
  const snapshot = declarationBody(declarations, 'ZLinkActorRefSnapshot');

  assert.match(actorClient, /sendToActor\(meshName: string, actor: ActorRef, message: unknown\): ZLinkActorSendCall/);
  assert.match(actorClient, /requestToActor\(meshName: string, actor: ActorRef, request: unknown\): ZLinkActorRequestCall/);
  assert.equal(actorClient.includes('actorId: string'), false);
  assert.match(actorSendCall, /metadata\(key: string, value: string\): this/);
  assert.match(actorSendCall, /submit\(signal\?: AbortSignal\): Promise<ZLinkSubmitResult>/);
  assert.equal(actorSendCall.includes('packetName('), false);
  assert.match(actorRequestCall, /metadata\(key: string, value: string\): this/);
  assert.equal(actorRequestCall.includes('packetName('), false);
  assert.match(actorRequestCall, /timeout\(timeoutMs: number\): this/);
  assert.match(actorRequestCall, /submit<TReply>\(signal\?: AbortSignal\): Promise<TReply>/);
  assert.match(actorRequestCall, /yield<TReply>\(signal\?: AbortSignal\): Promise<TReply>/);
  assert.match(directory, /find\(meshName: string, actorId: string, signal\?: AbortSignal\): Promise<ActorRef \| undefined>/);
  assert.match(directory, /ensure\(\s*meshName: string,\s*actorId: string,\s*createRequest: unknown,\s*placement\?: ZLinkActorPlacement,\s*signal\?: AbortSignal\s*\): Promise<ActorRef>/);
  assert.match(placement, /readonly preferredNodeRid\?: RoutingId/);
  assert.match(placement, /readonly routeMesh\?: string/);
  assert.match(snapshot, /readonly nodeRid: RoutingId/);
  assert.match(snapshot, /readonly actorId: string/);
  assert.match(snapshot, /readonly generation: bigint/);
  assert.match(declarations, /export declare function zlinkActorRefSnapshotFrom\(actorRef: ActorRef\): ZLinkActorRefSnapshot/);
  assert.match(declarations, /export declare function zlinkActorRefSnapshotToActorRef\(snapshot: ZLinkActorRefSnapshot\): ActorRef/);
  assert.match(sessionActors, /bindOrGet\(actor: ActorRef, signal\?: AbortSignal\): Promise<ZLinkSessionActor>/);
});

test('one-way call declarations expose async admission results only', () => {
  const declarations = readTree(declarationsRoot);
  const sendCall = declarationBody(declarations, 'ZLinkSendCall');
  const fanoutPublishCall = declarationBody(declarations, 'ZLinkFanoutPublishCall');
  const publishCall = declarationBody(declarations, 'ZLinkPublishCall');
  const actorSendCall = declarationBody(declarations, 'ZLinkActorSendCall');
  const boundSessionSendCall = declarationBody(declarations, 'ZLinkBoundSessionSendCall');
  const sessionSendCall = declarationBody(declarations, 'ZLinkSessionSendCall');
  const sessionReplyCall = declarationBody(declarations, 'ZLinkSessionReplyCall');
  const sessionActor = declarationBody(declarations, 'ZLinkSessionActor');

  assert.match(sendCall, /submit\(signal\?: AbortSignal\): Promise<ZLinkSubmitResult>/);
  assert.match(fanoutPublishCall, /submit\(signal\?: AbortSignal\): Promise<ZLinkSubmitResult>/);
  assert.match(publishCall, /submit\(signal\?: AbortSignal\): Promise<ZLinkPublishResult>/);
  assert.match(boundSessionSendCall, /submit\(signal\?: AbortSignal\): Promise<ZLinkSubmitResult>/);
  assert.match(sessionSendCall, /submit\(signal\?: AbortSignal\): Promise<ZLinkSubmitResult>/);
  assert.match(sessionReplyCall, /submit\(signal\?: AbortSignal\): Promise<ZLinkSubmitResult>/);
  assert.match(sessionActor, /relay\(payload: ZLinkMessage, signal\?: AbortSignal\): Promise<ZLinkSubmitResult>/);
  for (const call of [sendCall, fanoutPublishCall, publishCall, actorSendCall,
    boundSessionSendCall, sessionSendCall, sessionReplyCall]) {
    assert.doesNotMatch(call, new RegExp(['try', 'Submit'].join('')));
  }
});

test('route client surface scopes node routing by MeshName and resolves channels and Spots globally', () => {
  const declarations = readTree(declarationsRoot);
  const routeClient = declarationBody(declarations, 'ZLinkRouteClient');

  assert.match(routeClient, /sendToNode\(meshName: string, targetNodeRid: RoutingId, message: unknown\): ZLinkSendCall/);
  assert.match(routeClient, /requestToNode\(meshName: string, targetNodeRid: RoutingId, request: unknown\): ZLinkRequestCall/);
  assert.match(routeClient, /sendToChannel\(channelName: string, message: unknown\): ZLinkSendCall/);
  assert.match(routeClient, /requestToChannel\(channelName: string, request: unknown\): ZLinkRequestCall/);
  assert.match(routeClient, /sendToSpot\(spot: SpotHandle, message: unknown\): ZLinkSendCall/);
  assert.match(routeClient, /requestToSpot\(spot: SpotHandle, request: unknown\): ZLinkRequestCall/);
});

test('old public contract names from the redesign rename table do not re-enter node surfaces', () => {
  const forbidden = [
    'IZLinkSpotLocationResolver',
    'ZLinkSpotLocationResolver',
    'IZLinkActorLocationResolver',
    'ZLinkActorLocationResolver',
    'IZLinkRouteLocationResolver',
    'ZLinkRouteLocationResolver',
    'IZLinkActorRefResolver',
    'ZLinkActorRefResolver',
    'ZLinkResolveFreshness',
    'resolveFreshness',
    'ZLinkLocationCanonicalNames',
    'ZLinkSpotLocationRidResolver',
    'SpotLocationRidResolver',
    'PositiveCache',
    'positiveCache',
    'ActorIdConflict',
    'actorIdConflict',
    'locationKey'
  ];
  const allowlist = new Set([
    'test/contract/contract-surface.test.js'
  ]);
  const matches = [];

  for (const file of readTextFiles([
    path.join(workspaceRoot, 'packages', 'framework'),
    path.join(workspaceRoot, 'test'),
    path.join(workspaceRoot, 'e2e'),
    path.join(workspaceRoot, 'samples')
  ])) {
    const relativePath = path.relative(workspaceRoot, file);
    if (allowlist.has(relativePath)) {
      continue;
    }
    const text = fs.readFileSync(file, 'utf8');
    for (const name of forbidden) {
      if (text.includes(name)) {
        matches.push(`${relativePath}: ${name}`);
      }
    }
  }

  assert.deepEqual(matches.sort(), []);
});

function exportedCatalogNames(spec) {
  return uniqueMatches(spec, /^export\s+(?:interface|type|enum|function)\s+([A-Za-z][A-Za-z0-9_]*)/gm);
}

function frameworkCatalog(spec) {
  return spec.split(/^### \d+\.\d+ @zlink-systems\/nestjs:/m)[0];
}

function runtimeCatalogNames(spec) {
  return uniqueMatches(spec, /^export\s+(?:enum|function)\s+([A-Za-z][A-Za-z0-9_]*)/gm);
}

function uniqueMatches(text, pattern) {
  return [...new Set([...text.matchAll(pattern)].map((match) => match[1]))].sort();
}

function declarationBody(text, name) {
  const match = text.match(new RegExp(`export interface ${name}(?:<[^>{]+>)?(?: [^{]+)? \\{([\\s\\S]*?)\\n\\}`));
  assert.ok(match, `missing declaration for ${name}`);
  return match[0];
}

function interfaceHeader(text, name) {
  const match = text.match(new RegExp(`export interface ${name}(?:<[^>{]+>)?(?: [^{]+)? \\{`));
  assert.ok(match, `missing declaration header for ${name}`);
  return match[0];
}

function interfaceExtends(declaration, baseName) {
  const header = declaration.slice(0, declaration.indexOf('{'));
  return new RegExp(`\\bextends\\b[^{]*\\b${baseName}\\b`).test(header);
}

function pickEnumValues(enumObject, names) {
  assert.ok(enumObject, 'missing enum object');
  return Object.fromEntries(names.map((name) => [name, enumObject[name]]));
}

test('framework public root excludes internal registration implementation', () => {
  const framework = require('../../packages/framework/dist');
  const declarations = fs.readFileSync(path.join(declarationsRoot, '..', 'index.d.ts'), 'utf8') +
    fs.readFileSync(path.join(declarationsRoot, 'index.d.ts'), 'utf8') +
    fs.readFileSync(path.join(declarationsRoot, 'Configuration', 'index.d.ts'), 'utf8');
  const forbidden = [
    'createFrameworkRegistration',
    'createFrameworkOptions',
    'ZLinkFrameworkRegistration',
    'ZLinkFrameworkRegistrationOptions',
    'ZLinkSpotNodeRegistrationOptions',
    'ZLinkProviderResolver'
  ];

  for (const name of forbidden) {
    assert.equal(Object.hasOwn(framework, name), false, `${name} must not be a runtime export`);
    assert.equal(new RegExp(`export (?:declare )?(?:function|interface|type|class) ${name}\\b`).test(declarations), false,
      `${name} must not be a declaration export`);
  }
});

function readTree(root) {
  let text = '';
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const fullPath = path.join(root, entry.name);
    if (entry.isDirectory()) {
      text += readTree(fullPath);
      continue;
    }
    if (/\.(?:ts|js|md)$/.test(entry.name)) {
      text += fs.readFileSync(fullPath, 'utf8');
    }
  }
  return text;
}

function readTextFiles(roots) {
  const files = [];
  for (const root of roots) {
    collectTextFiles(root, files);
  }
  return files;
}

function collectTextFiles(root, files) {
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const fullPath = path.join(root, entry.name);
    if (entry.name === 'node_modules' || entry.name === 'dist' || entry.name === '.git') {
      continue;
    }
    if (entry.isDirectory()) {
      collectTextFiles(fullPath, files);
      continue;
    }
    if (/\.(?:ts|tsx|js|mjs|cjs|md|json|sh|ps1)$/.test(entry.name)) {
      files.push(fullPath);
    }
  }
}
