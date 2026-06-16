"use strict";
var __decorate = (this && this.__decorate) || function (decorators, target, key, desc) {
    var c = arguments.length, r = c < 3 ? target : desc === null ? desc = Object.getOwnPropertyDescriptor(target, key) : desc, d;
    if (typeof Reflect === "object" && typeof Reflect.decorate === "function") r = Reflect.decorate(decorators, target, key, desc);
    else for (var i = decorators.length - 1; i >= 0; i--) if (d = decorators[i]) r = (c < 3 ? d(r) : c > 3 ? d(target, key, r) : d(target, key)) || r;
    return c > 3 && r && Object.defineProperty(target, key, r), r;
};
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
var ZLinkModule_1, ZLinkRegistryModule_1, ZLinkRegistryQueryClientModule_1;
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkRegistryQueryClientModule = exports.ZLinkRegistryModule = exports.ZLinkModule = exports.ZLINK_REGISTRY_QUERY_CLIENT = exports.ZLINK_REGISTRY_QUERY = exports.ZLINK_REGISTRY_RUNTIME = exports.ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER = exports.ZLINK_ACTOR_MANAGER = exports.ZLINK_SPOT_PUBLISHER_CLIENT = exports.ZLINK_SPOT_OUTBOUND = exports.ZLINK_SPOT_MANAGER = exports.ZLINK_MESSAGE_METADATA_POLICY = exports.ZLINK_BOUND_SESSION_FACTORY = exports.ZLINK_FANOUT_CLIENT = exports.ZLINK_ROUTE_CLIENT = exports.ZLINK_CHANNEL_CLIENT = exports.ZLINK_FRAMEWORK_RUNTIME = exports.ZLINK_FRAMEWORK_REGISTRATION = exports.ZLINK_NEST_HANDLER_GROUP = void 0;
exports.zlinkFramework = zlinkFramework;
exports.zlinkRequestHandler = zlinkRequestHandler;
exports.zlinkSendHandler = zlinkSendHandler;
exports.zlinkPublishHandler = zlinkPublishHandler;
exports.zlinkDiscoverProviders = zlinkDiscoverProviders;
exports.zlinkSpotActorSendHandler = zlinkSpotActorSendHandler;
exports.zlinkSpotActorRequestHandler = zlinkSpotActorRequestHandler;
exports.zlinkEntrySpotActorSendHandler = zlinkEntrySpotActorSendHandler;
exports.zlinkEntrySpotActorRequestHandler = zlinkEntrySpotActorRequestHandler;
exports.zlinkSpotTimerHandler = zlinkSpotTimerHandler;
exports.zlinkHandler = zlinkHandler;
exports.createZLinkDynamicModule = createZLinkDynamicModule;
require("reflect-metadata");
const node_module_1 = require("node:module");
const node_fs_1 = __importDefault(require("node:fs"));
const node_path_1 = __importDefault(require("node:path"));
const common_1 = require("@nestjs/common");
const core_1 = require("@nestjs/core");
const framework = loadFramework();
exports.ZLINK_NEST_HANDLER_GROUP = Symbol.for('@zlink-systems/nestjs:handler-group');
exports.ZLINK_FRAMEWORK_REGISTRATION = Symbol.for('@zlink-systems/framework:registration');
exports.ZLINK_FRAMEWORK_RUNTIME = Symbol.for('@zlink-systems/framework:runtime');
exports.ZLINK_CHANNEL_CLIENT = Symbol.for('@zlink-systems/framework:channel-client');
exports.ZLINK_ROUTE_CLIENT = Symbol.for('@zlink-systems/framework:route-client');
exports.ZLINK_FANOUT_CLIENT = Symbol.for('@zlink-systems/framework:fanout-client');
exports.ZLINK_BOUND_SESSION_FACTORY = Symbol.for('@zlink-systems/framework:bound-session-factory');
exports.ZLINK_MESSAGE_METADATA_POLICY = Symbol.for('@zlink-systems/framework:message-metadata-policy');
exports.ZLINK_SPOT_MANAGER = Symbol.for('@zlink-systems/framework:spot-manager');
exports.ZLINK_SPOT_OUTBOUND = Symbol.for('@zlink-systems/framework:spot-outbound');
exports.ZLINK_SPOT_PUBLISHER_CLIENT = Symbol.for('@zlink-systems/framework:spot-publisher-client');
exports.ZLINK_ACTOR_MANAGER = Symbol.for('@zlink-systems/framework:actor-manager');
exports.ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER = Symbol.for('@zlink-systems/framework:spot-remote-address-resolver');
exports.ZLINK_REGISTRY_RUNTIME = Symbol.for('@zlink-systems/framework:registry-runtime');
exports.ZLINK_REGISTRY_QUERY = Symbol.for('@zlink-systems/framework:registry-query');
exports.ZLINK_REGISTRY_QUERY_CLIENT = Symbol.for('@zlink-systems/framework:registry-query-client');
const nestHandlerMetadataByToken = new Map();
const nestSpotActorHandlerMetadataByToken = new Map();
const nestSpotTimerHandlerMetadataByToken = new Map();
const nestSpotTimerHandlerTokens = new Set();
function zlinkFramework() {
    return new DefaultZLinkNestFrameworkOptionsBuilder();
}
function zlinkRequestHandler(groupName, packetName, options = {}) {
    return zlinkHandler(groupName, 'request', packetName, options);
}
function zlinkSendHandler(groupName, packetName, options = {}) {
    return zlinkHandler(groupName, 'send', packetName, options);
}
function zlinkPublishHandler(groupName, packetName, options = {}) {
    return zlinkHandler(groupName, 'publish', packetName, options);
}
function zlinkDiscoverProviders(rootDir, options = {}) {
    return [...loadDecoratedProviderModules(rootDir, options)];
}
function zlinkSpotActorRequestHandler(options) {
    return (target) => {
        (0, common_1.Injectable)()(target);
        appendNestSpotActorHandlerMetadata(target, {
            actor: options.actor,
            handlerType: target,
            kind: 'spotActorRequest',
            methodName: options.methodName ?? 'handle',
            packetName: options.packetName,
            spot: options.spot
        });
    };
}
function zlinkSpotActorSendHandler(options) {
    return (target) => {
        (0, common_1.Injectable)()(target);
        appendNestSpotActorHandlerMetadata(target, {
            actor: options.actor,
            handlerType: target,
            kind: 'spotActorSend',
            methodName: options.methodName ?? 'handle',
            packetName: options.packetName,
            spot: options.spot
        });
    };
}
function zlinkEntrySpotActorRequestHandler(options) {
    return (target) => {
        (0, common_1.Injectable)()(target);
        appendNestSpotActorHandlerMetadata(target, {
            actor: options.actor,
            entrySpot: options.entrySpot,
            handlerType: target,
            kind: 'entrySpotActorRequest',
            methodName: options.methodName ?? 'handle',
            packetName: options.packetName
        });
    };
}
function zlinkEntrySpotActorSendHandler(options) {
    return (target) => {
        (0, common_1.Injectable)()(target);
        appendNestSpotActorHandlerMetadata(target, {
            actor: options.actor,
            entrySpot: options.entrySpot,
            handlerType: target,
            kind: 'entrySpotActorSend',
            methodName: options.methodName ?? 'handle',
            packetName: options.packetName
        });
    };
}
function zlinkSpotTimerHandler(options = {}) {
    return (target) => {
        (0, common_1.Injectable)()(target);
        nestSpotTimerHandlerTokens.add(target);
        if (options.name !== undefined && options.periodMs !== undefined) {
            appendNestSpotTimerHandlerMetadata(target, {
                entrySpot: options.entrySpot,
                handlerType: target,
                name: options.name,
                options: options.options,
                periodMs: options.periodMs,
                spot: options.spot
            });
        }
    };
}
function zlinkHandler(groupName, kind, packetName, options = {}) {
    validateHandlerGroupName(groupName);
    return (target) => {
        (0, common_1.Injectable)()(target);
        appendNestHandlerMetadata(target, {
            groupName,
            kind,
            methodName: options.methodName ?? 'handle',
            packetName: packetName ?? inferPacketName(target, target)
        });
    };
}
class DefaultZLinkNestFrameworkOptionsBuilder {
    additionalOptions = {};
    clientServerChannels = {};
    fanoutChannels = {};
    routerMeshes = {};
    streams = {};
    spotNodes = {};
    actorFactories = {};
    options(options) {
        this.additionalOptions = { ...this.additionalOptions, ...options };
        return this;
    }
    actorFactory(actorType, factoryType) {
        this.actorFactories[actorType] = factoryType;
        return this;
    }
    clientServerChannel(name, configure) {
        const channel = new DefaultZLinkNestClientServerChannelBuilder();
        configure(channel);
        this.clientServerChannels[name] = channel.build();
        return this;
    }
    fanoutChannel(name, configure) {
        const channel = new DefaultZLinkNestFanoutChannelBuilder();
        configure(channel);
        this.fanoutChannels[name] = channel.build();
        return this;
    }
    routerMesh(name, configure) {
        const mesh = new DefaultZLinkNestRouterMeshBuilder();
        configure(mesh);
        this.routerMeshes[name] = mesh.build();
        return this;
    }
    spotNode(name, configure) {
        const spot = new DefaultZLinkNestSpotNodeBuilder();
        configure(spot);
        this.spotNodes[name] = spot.build();
        return this;
    }
    streamNode(name, configure) {
        const stream = new DefaultZLinkNestStreamNodeBuilder();
        configure(stream);
        this.streams[name] = stream.build();
        return this;
    }
    build() {
        return {
            ...this.additionalOptions,
            clientServerChannels: { ...this.clientServerChannels },
            fanoutChannels: { ...this.fanoutChannels },
            routerMeshes: { ...this.routerMeshes },
            streams: { ...this.streams, ...(this.additionalOptions.streams ?? {}) },
            spotNodes: { ...this.spotNodes, ...(this.additionalOptions.spotNodes ?? {}) },
            actorFactories: { ...this.actorFactories, ...(this.additionalOptions.actorFactories ?? {}) }
        };
    }
}
class DefaultZLinkNestClientServerChannelBuilder {
    options = {};
    server(bind) {
        this.options = { ...this.options, server: { bind } };
        return this;
    }
    client(endpoint) {
        this.options = { ...this.options, client: endpoint === undefined ? {} : { manualConnections: endpointList(endpoint) } };
        return this;
    }
    handlerGroup(groupName) {
        this.options = { ...this.options, handlerGroups: [...(this.options.handlerGroups ?? []), groupName] };
        return this;
    }
    build() {
        return this.options;
    }
}
class DefaultZLinkNestFanoutChannelBuilder {
    options = {};
    publisher(bind) {
        this.options = { ...this.options, publisher: { bind } };
        return this;
    }
    subscriber(endpoint) {
        this.options = { ...this.options, subscriber: endpoint === undefined ? {} : { manualConnections: endpointList(endpoint) } };
        return this;
    }
    handlerGroup(groupName) {
        this.options = { ...this.options, handlerGroups: [...(this.options.handlerGroups ?? []), groupName] };
        return this;
    }
    build() {
        return this.options;
    }
}
class DefaultZLinkNestRouterMeshBuilder {
    options = {};
    bind(endpoint) {
        this.options = { ...this.options, bind: endpoint };
        return this;
    }
    routingId(routingId) {
        this.options = { ...this.options, routingId };
        return this;
    }
    connect(endpoint) {
        this.options = { ...this.options, manualConnections: endpoint === undefined ? [] : endpointList(endpoint) };
        return this;
    }
    handlerGroup(groupName) {
        this.options = { ...this.options, handlerGroups: [...(this.options.handlerGroups ?? []), groupName] };
        return this;
    }
    build() {
        return this.options;
    }
}
class DefaultZLinkNestStreamNodeBuilder {
    options = {};
    bind(endpoint) {
        this.options = { ...this.options, bind: endpoint };
        return this;
    }
    attachActorGateway(spotNodeName) {
        this.options = { ...this.options, attachActorGateway: spotNodeName };
        return this;
    }
    registerSession(sessionType) {
        if (this.options.session !== undefined) {
            throw new framework.ZLinkConfigurationException('STREAM node cannot register more than one header stream session.');
        }
        this.options = { ...this.options, session: sessionType };
        return this;
    }
    build() {
        return this.options;
    }
}
class DefaultZLinkNestSpotNodeBuilder {
    options = {};
    router(bind, routingId, connect) {
        this.options = {
            ...this.options,
            router: {
                bind,
                routingId,
                manualConnections: connect === undefined ? undefined : endpointList(connect)
            }
        };
        return this;
    }
    entrySpot(entrySpotType) {
        this.options = { ...this.options, entrySpotType };
        return this;
    }
    spotFactory(spotType) {
        this.options = {
            ...this.options,
            spotFactories: [...(this.options.spotFactories ?? []), spotType]
        };
        return this;
    }
    build() {
        return this.options;
    }
}
function endpointList(endpoint) {
    return typeof endpoint === 'string' ? [endpoint] : [...endpoint];
}
function validateHandlerGroupName(groupName) {
    if (groupName.trim() === '') {
        throw new framework.ZLinkConfigurationException('ZLink handler group name must not be empty.');
    }
}
function inferPacketName(handlerType, handlerToken) {
    if (handlerType !== undefined) {
        return handlerType.name.endsWith('Handler')
            ? handlerType.name.slice(0, -'Handler'.length)
            : handlerType.name;
    }
    const tokenName = typeof handlerToken === 'symbol'
        ? handlerToken.description
        : String(handlerToken);
    if (tokenName === undefined || tokenName.trim() === '') {
        throw new framework.ZLinkConfigurationException('ZLink handler packetName is required for anonymous provider tokens.');
    }
    return tokenName;
}
function appendNestHandlerMetadata(handlerToken, metadata) {
    const current = readNestHandlerMetadata(handlerToken);
    nestHandlerMetadataByToken.set(handlerToken, [...current, metadata]);
    if (typeof handlerToken === 'function') {
        Object.defineProperty(handlerToken, exports.ZLINK_NEST_HANDLER_GROUP, {
            configurable: true,
            enumerable: false,
            value: [...current, metadata],
            writable: false
        });
    }
}
function readNestHandlerMetadata(handlerToken) {
    if (handlerToken === undefined) {
        return [];
    }
    return nestHandlerMetadataByToken.get(handlerToken)
        ?? (typeof handlerToken === 'function'
            ? handlerToken[exports.ZLINK_NEST_HANDLER_GROUP] ?? []
            : []);
}
function appendNestSpotActorHandlerMetadata(handlerToken, metadata) {
    const current = readNestSpotActorHandlerMetadata(handlerToken);
    nestSpotActorHandlerMetadataByToken.set(handlerToken, [...current, metadata]);
}
function readNestSpotActorHandlerMetadata(handlerToken) {
    if (handlerToken === undefined) {
        return [];
    }
    return nestSpotActorHandlerMetadataByToken.get(handlerToken) ?? [];
}
function appendNestSpotTimerHandlerMetadata(handlerToken, metadata) {
    const current = readNestSpotTimerHandlerMetadata(handlerToken);
    nestSpotTimerHandlerMetadataByToken.set(handlerToken, [...current, metadata]);
}
function readNestSpotTimerHandlerMetadata(handlerToken) {
    if (handlerToken === undefined) {
        return [];
    }
    return nestSpotTimerHandlerMetadataByToken.get(handlerToken) ?? [];
}
function hasNestSpotTimerHandlerMetadata(handlerToken) {
    return handlerToken !== undefined && nestSpotTimerHandlerTokens.has(handlerToken);
}
function loadDecoratedProviderModules(rootDir, options) {
    if (!node_fs_1.default.existsSync(rootDir)) {
        throw new framework.ZLinkConfigurationException(`ZLink provider discovery root does not exist: ${rootDir}`);
    }
    const providers = new Set();
    const stat = node_fs_1.default.statSync(rootDir);
    if (stat.isFile()) {
        addDecoratedProviderModuleExports(providers, rootDir);
        return providers;
    }
    for (const entry of node_fs_1.default.readdirSync(rootDir, { withFileTypes: true })) {
        const fullPath = node_path_1.default.join(rootDir, entry.name);
        if (entry.isDirectory()) {
            if (options.recursive === true) {
                for (const provider of loadDecoratedProviderModules(fullPath, options)) {
                    providers.add(provider);
                }
            }
            continue;
        }
        if (entry.isFile()) {
            addDecoratedProviderModuleExports(providers, fullPath);
        }
    }
    return providers;
}
function addDecoratedProviderModuleExports(providers, filePath) {
    if (!/\.(?:cjs|mjs|js)$/.test(filePath) || /\.d\.js$/.test(filePath)) {
        return;
    }
    const loaded = (0, node_module_1.createRequire)(__filename)(filePath);
    for (const value of Object.values(loaded)) {
        if (typeof value === 'function'
            && (readNestHandlerMetadata(value).length > 0
                || readNestSpotActorHandlerMetadata(value).length > 0
                || hasNestSpotTimerHandlerMetadata(value))) {
            providers.add(value);
        }
    }
}
let ZLinkModule = ZLinkModule_1 = class ZLinkModule {
    static forRoot(options = {}) {
        assertNoLegacyModuleOptions(options);
        if (hasNestHandlerDiscovery(options)) {
            return createDiscoveringZLinkDynamicModule(options);
        }
        return createZLinkDynamicModule(framework.createFrameworkRegistration(createRegistrationOptions(options)));
    }
    static forRootFactory(options) {
        const registrationProvider = {
            provide: exports.ZLINK_FRAMEWORK_REGISTRATION,
            inject: [...(options.inject ?? []), core_1.DiscoveryService, core_1.ModuleRef],
            useFactory: async (...args) => {
                const discovery = args[args.length - 2];
                const moduleRef = args[args.length - 1];
                const factoryArgs = args.slice(0, -2);
                const resolvedOptions = assertNoLegacyModuleOptions(await options.useFactory(...factoryArgs));
                return framework.createFrameworkRegistration(createDiscoveredOptions(resolvedOptions, discovery, moduleRef));
            }
        };
        return {
            module: ZLinkModule_1,
            imports: [...(options.imports ?? []), core_1.DiscoveryModule],
            providers: [
                registrationProvider,
                {
                    provide: exports.ZLINK_FRAMEWORK_RUNTIME,
                    inject: [exports.ZLINK_FRAMEWORK_REGISTRATION, core_1.ModuleRef, core_1.DiscoveryService],
                    useFactory: (registration, moduleRef, discovery) => createRuntimeHost(registration, moduleRef, discovery)
                },
                ...alwaysAvailableClientProviders(),
                ...conditionalClientProvidersForFactory()
            ],
            exports: [
                exports.ZLINK_FRAMEWORK_RUNTIME,
                ...alwaysAvailableClientTokens(),
                ...conditionalClientTokens()
            ]
        };
    }
};
exports.ZLinkModule = ZLinkModule;
exports.ZLinkModule = ZLinkModule = ZLinkModule_1 = __decorate([
    (0, common_1.Module)({})
], ZLinkModule);
let ZLinkRegistryModule = ZLinkRegistryModule_1 = class ZLinkRegistryModule {
    static forRoot(options) {
        const runtime = new framework.ZLinkRegistryRuntime({ registration: options });
        const query = new framework.DefaultZLinkRegistryQuery(runtime);
        const providers = [
            { provide: exports.ZLINK_REGISTRY_RUNTIME, useValue: runtime },
            { provide: exports.ZLINK_REGISTRY_QUERY, useValue: query }
        ];
        return {
            module: ZLinkRegistryModule_1,
            providers,
            exports: providers.map(providerToken)
        };
    }
    static forRootFactory(options) {
        return createRegistryDynamicModuleFromFactory({
            module: ZLinkRegistryModule_1,
            options,
            runtimeToken: exports.ZLINK_REGISTRY_RUNTIME,
            queryToken: exports.ZLINK_REGISTRY_QUERY,
            createRuntime: (registration) => new framework.ZLinkRegistryRuntime({ registration }),
            createQuery: (runtime) => new framework.DefaultZLinkRegistryQuery(runtime)
        });
    }
};
exports.ZLinkRegistryModule = ZLinkRegistryModule;
exports.ZLinkRegistryModule = ZLinkRegistryModule = ZLinkRegistryModule_1 = __decorate([
    (0, common_1.Module)({})
], ZLinkRegistryModule);
let ZLinkRegistryQueryClientModule = ZLinkRegistryQueryClientModule_1 = class ZLinkRegistryQueryClientModule {
    static forRoot(options) {
        return {
            module: ZLinkRegistryQueryClientModule_1,
            providers: [{
                    provide: exports.ZLINK_REGISTRY_QUERY_CLIENT,
                    useFactory: () => new framework.DefaultZLinkRegistryQueryClient({ registration: options })
                }],
            exports: [exports.ZLINK_REGISTRY_QUERY_CLIENT]
        };
    }
    static forRootFactory(options) {
        return {
            module: ZLinkRegistryQueryClientModule_1,
            imports: options.imports,
            providers: [{
                    provide: exports.ZLINK_REGISTRY_QUERY_CLIENT,
                    inject: options.inject === undefined ? undefined : [...options.inject],
                    useFactory: async (...args) => new framework.DefaultZLinkRegistryQueryClient({ registration: await options.useFactory(...args) })
                }],
            exports: [exports.ZLINK_REGISTRY_QUERY_CLIENT]
        };
    }
};
exports.ZLinkRegistryQueryClientModule = ZLinkRegistryQueryClientModule;
exports.ZLinkRegistryQueryClientModule = ZLinkRegistryQueryClientModule = ZLinkRegistryQueryClientModule_1 = __decorate([
    (0, common_1.Module)({})
], ZLinkRegistryQueryClientModule);
function createZLinkDynamicModule(registration) {
    const providers = [
        { provide: exports.ZLINK_FRAMEWORK_REGISTRATION, useValue: registration },
        {
            provide: exports.ZLINK_FRAMEWORK_RUNTIME,
            inject: [core_1.ModuleRef, core_1.DiscoveryService],
            useFactory: (moduleRef, discovery) => createRuntimeHost(registration, moduleRef, discovery)
        },
        ...alwaysAvailableClientProviders(registration),
        ...conditionalClientProviders(registration)
    ];
    return {
        module: ZLinkModule,
        imports: [core_1.DiscoveryModule],
        providers,
        exports: providers.map(providerToken)
    };
}
function createRegistryDynamicModuleFromFactory(options) {
    return {
        module: options.module,
        imports: options.options.imports,
        providers: [
            {
                provide: options.runtimeToken,
                inject: options.options.inject === undefined ? undefined : [...options.options.inject],
                useFactory: async (...args) => options.createRuntime(await options.options.useFactory(...args))
            },
            {
                provide: options.queryToken,
                inject: [options.runtimeToken],
                useFactory: options.createQuery
            }
        ],
        exports: [options.runtimeToken, options.queryToken]
    };
}
function createDiscoveringZLinkDynamicModule(options) {
    const registrationProvider = {
        provide: exports.ZLINK_FRAMEWORK_REGISTRATION,
        inject: [core_1.DiscoveryService, core_1.ModuleRef],
        useFactory: (discovery, moduleRef) => framework.createFrameworkRegistration(createDiscoveredOptions(options, discovery, moduleRef))
    };
    return {
        module: ZLinkModule,
        imports: [core_1.DiscoveryModule],
        providers: [
            registrationProvider,
            {
                provide: exports.ZLINK_FRAMEWORK_RUNTIME,
                inject: [exports.ZLINK_FRAMEWORK_REGISTRATION, core_1.ModuleRef, core_1.DiscoveryService],
                useFactory: (registration, moduleRef, discovery) => createRuntimeHost(registration, moduleRef, discovery)
            },
            ...alwaysAvailableClientProviders(),
            ...conditionalClientProvidersForFactory()
        ],
        exports: [
            exports.ZLINK_FRAMEWORK_RUNTIME,
            ...alwaysAvailableClientTokens(),
            ...conditionalClientTokens()
        ]
    };
}
function createDiscoveredOptions(options, discovery, moduleRef) {
    const registrationOptions = createRegistrationOptions(options);
    const channels = { ...(registrationOptions.channels ?? {}) };
    const routerMeshes = new Map();
    const providerRefs = discoverProviderRefs(discovery, moduleRef);
    const spotActorProviderRefs = discoverSpotActorProviderRefs(discovery, moduleRef);
    const spotTimerProviderRefs = discoverSpotTimerProviderRefs(discovery, moduleRef);
    const spotNodes = createDiscoveredSpotNodeOptions(registrationOptions.spotNodes, spotActorProviderRefs, spotTimerProviderRefs);
    for (const [channelName, channel] of Object.entries(options.clientServerChannels ?? {})) {
        const requestHandlers = createDiscoveredRequestHandlers(providerRefs, channel.handlerGroups, moduleRef);
        channels[channelName] = {
            ...channels[channelName],
            requestHandlers: channel.server === undefined
                ? channels[channelName]?.requestHandlers
                : [
                    ...(channels[channelName]?.requestHandlers ?? []),
                    ...requestHandlers
                ]
        };
    }
    for (const [channelName, channel] of Object.entries(options.fanoutChannels ?? {})) {
        const publishHandlers = createDiscoveredPublishHandlers(providerRefs, channel.handlerGroups, moduleRef);
        channels[channelName] = {
            ...channels[channelName],
            publishHandlers: [
                ...(channels[channelName]?.publishHandlers ?? []),
                ...publishHandlers
            ]
        };
    }
    for (const routeChannel of registrationOptions.routeChannels ?? []) {
        const normalized = typeof routeChannel === 'string'
            ? { routerChannelId: routeChannel }
            : { ...routeChannel };
        routerMeshes.set(normalized.routerChannelId, normalized);
    }
    for (const [routerMeshName, routerMesh] of Object.entries(options.routerMeshes ?? {})) {
        const existing = routerMeshes.get(routerMeshName) ?? { routerChannelId: routerMeshName };
        const requestHandlers = createDiscoveredRequestHandlers(providerRefs, routerMesh.handlerGroups, moduleRef);
        const sendHandlers = createDiscoveredSendHandlers(providerRefs, routerMesh.handlerGroups, moduleRef);
        routerMeshes.set(routerMeshName, {
            ...existing,
            requestHandlers: [
                ...(existing.requestHandlers ?? []),
                ...requestHandlers
            ],
            sendHandlers: [
                ...(existing.sendHandlers ?? []),
                ...sendHandlers
            ]
        });
    }
    return {
        ...registrationOptions,
        channels,
        routeChannels: [...routerMeshes.values()],
        spotNodes
    };
}
function createDiscoveredSpotNodeOptions(value, refs, timerRefs = []) {
    if (refs.length === 0 && timerRefs.length === 0) {
        return value;
    }
    const spotNodes = toMutableSpotNodeRecord(value);
    const spotNodeEntries = Object.entries(spotNodes);
    if (spotNodeEntries.length === 0) {
        throw new framework.ZLinkConfigurationException('ZLink SPOT actor handlers require a registered SpotNode.');
    }
    for (const ref of timerRefs) {
        if (ref.metadata.entrySpot !== undefined) {
            const entrySpotType = resolveNestType(ref.metadata.entrySpot, 'entrySpot');
            const matches = spotNodeEntries.filter(([, spotNode]) => spotNode.entrySpotType === entrySpotType);
            if (matches.length === 0) {
                throw new framework.ZLinkConfigurationException(`ZLink Entry Spot timer handler '${ref.handlerName}' targets an Entry Spot that is not registered on any SpotNode.`);
            }
            for (const [, spotNode] of matches) {
                spotNode.entrySpotTimerHandlers = [
                    ...(spotNode.entrySpotTimerHandlers ?? []),
                    {
                        entrySpotType,
                        handlerType: ref.handlerKey,
                        name: ref.metadata.name,
                        options: ref.metadata.options,
                        periodMs: ref.metadata.periodMs
                    }
                ];
            }
            continue;
        }
        const spotType = resolveNestType(ref.metadata.spot, 'spot');
        const matches = spotNodeEntries.filter(([, spotNode]) => (spotNode.spotFactories ?? []).includes(spotType));
        if (matches.length === 0) {
            throw new framework.ZLinkConfigurationException(`ZLink SPOT timer handler '${ref.handlerName}' targets a Spot type that is not registered on any SpotNode.`);
        }
        for (const [, spotNode] of matches) {
            spotNode.spotTimerHandlers = [
                ...(spotNode.spotTimerHandlers ?? []),
                {
                    handlerType: ref.handlerKey,
                    name: ref.metadata.name,
                    options: ref.metadata.options,
                    periodMs: ref.metadata.periodMs,
                    spotType
                }
            ];
        }
    }
    for (const ref of refs) {
        if (ref.metadata.kind === 'entrySpotActorSend' || ref.metadata.kind === 'entrySpotActorRequest') {
            const entrySpotType = resolveNestType(ref.metadata.entrySpot, 'entrySpot');
            const actorType = resolveNestType(ref.metadata.actor, 'actor');
            const matches = spotNodeEntries.filter(([, spotNode]) => spotNode.entrySpotType === entrySpotType);
            if (matches.length === 0) {
                throw new framework.ZLinkConfigurationException(`ZLink Entry Spot actor handler '${ref.handlerName}' targets an Entry Spot that is not registered on any SpotNode.`);
            }
            for (const [, spotNode] of matches) {
                const next = {
                    actorType,
                    entrySpotType,
                    handlerType: ref.handlerKey,
                    packetName: ref.metadata.packetName
                };
                if (ref.metadata.kind === 'entrySpotActorSend') {
                    assertUniqueEntrySpotActorHandler(spotNode.entrySpotActorSendHandlers, next);
                    spotNode.entrySpotActorSendHandlers = [
                        ...(spotNode.entrySpotActorSendHandlers ?? []),
                        next
                    ];
                }
                else {
                    assertUniqueEntrySpotActorHandler(spotNode.entrySpotActorRequestHandlers, next);
                    spotNode.entrySpotActorRequestHandlers = [
                        ...(spotNode.entrySpotActorRequestHandlers ?? []),
                        next
                    ];
                }
            }
            continue;
        }
        const spotType = resolveNestType(ref.metadata.spot, 'spot');
        const actorType = resolveNestType(ref.metadata.actor, 'actor');
        const matches = spotNodeEntries.filter(([, spotNode]) => (spotNode.spotFactories ?? []).includes(spotType));
        if (matches.length === 0) {
            throw new framework.ZLinkConfigurationException(`ZLink SPOT actor handler '${ref.handlerName}' targets a Spot type that is not registered on any SpotNode.`);
        }
        for (const [, spotNode] of matches) {
            const next = {
                actorType,
                handlerType: ref.handlerKey,
                packetName: ref.metadata.packetName,
                spotType
            };
            if (ref.metadata.kind === 'spotActorSend') {
                assertUniqueSpotActorHandler(spotNode.spotActorSendHandlers, next);
                spotNode.spotActorSendHandlers = [
                    ...(spotNode.spotActorSendHandlers ?? []),
                    next
                ];
            }
            else {
                assertUniqueSpotActorHandler(spotNode.spotActorRequestHandlers, next);
                spotNode.spotActorRequestHandlers = [
                    ...(spotNode.spotActorRequestHandlers ?? []),
                    next
                ];
            }
        }
    }
    return spotNodes;
}
function toMutableSpotNodeRecord(value) {
    if (value === undefined) {
        return {};
    }
    if (!Array.isArray(value)) {
        return Object.fromEntries(Object.entries(value).map(([name, spotNode]) => [name, { ...spotNode }]));
    }
    return Object.fromEntries(value.map((spotNode) => {
        if (typeof spotNode === 'string') {
            return [spotNode, {}];
        }
        const { name, ...options } = spotNode;
        return [name, { ...options }];
    }));
}
function resolveNestType(resolver, name) {
    if (resolver === undefined) {
        throw new framework.ZLinkConfigurationException(`ZLink SPOT actor handler ${name} type is required.`);
    }
    if (isClassType(resolver)) {
        return resolver;
    }
    const resolved = resolver();
    if (!isClassType(resolved)) {
        throw new framework.ZLinkConfigurationException(`ZLink SPOT actor handler ${name} type resolver must return a class.`);
    }
    return resolved;
}
function isClassType(value) {
    return typeof value === 'function' && /^class\s/.test(Function.prototype.toString.call(value));
}
function assertUniqueEntrySpotActorHandler(existing, next) {
    if ((existing ?? []).some((handler) => handler.entrySpotType === next.entrySpotType &&
        handler.actorType === next.actorType &&
        handler.packetName === next.packetName)) {
        throw new framework.ZLinkConfigurationException(`Duplicate Entry Spot actor handler '${next.entrySpotType.name}:${next.actorType.name}:${next.packetName}'.`);
    }
}
function assertUniqueSpotActorHandler(existing, next) {
    if ((existing ?? []).some((handler) => handler.spotType === next.spotType &&
        handler.actorType === next.actorType &&
        handler.packetName === next.packetName)) {
        throw new framework.ZLinkConfigurationException(`Duplicate SPOT actor handler '${next.spotType.name}:${next.actorType.name}:${next.packetName}'.`);
    }
}
function createRegistrationOptions(options) {
    const channels = {};
    const routeChannels = [];
    for (const [name, channel] of Object.entries(options.clientServerChannels ?? {})) {
        assertChannelNameAvailable(channels, name, 'ClientServerChannel');
        channels[name] = {
            client: channel.client,
            requestHandlers: channel.requestHandlers,
            server: channel.server
        };
    }
    for (const [name, channel] of Object.entries(options.fanoutChannels ?? {})) {
        assertChannelNameAvailable(channels, name, 'FanoutChannel');
        channels[name] = {
            publishHandlers: channel.publishHandlers,
            publisher: channel.publisher,
            subscriber: channel.subscriber
        };
    }
    for (const [name, channel] of Object.entries(options.dealerMeshChannels ?? {})) {
        assertChannelNameAvailable(channels, name, 'DealerMeshChannel');
        channels[name] = {
            dealerMesh: { ...channel }
        };
    }
    for (const [name, routerMesh] of Object.entries(options.routerMeshes ?? {})) {
        const { handlerGroups: _handlerGroups, ...routeChannel } = routerMesh;
        routeChannels.push({
            routerChannelId: name,
            ...routeChannel
        });
    }
    return {
        actorFactories: options.actorFactories,
        channels,
        discovery: options.discovery,
        registrySpotRemoteAddresses: options.registrySpotRemoteAddresses,
        routeChannels,
        spotFactories: options.spotFactories,
        spotNodes: options.spotNodes,
        spotPublisherClients: options.spotPublisherClients,
        spotRemoteAddressResolver: options.spotRemoteAddressResolver,
        streamNodes: options.streams
    };
}
function assertChannelNameAvailable(channels, name, kind) {
    if (channels[name] !== undefined) {
        throw new framework.ZLinkConfigurationException(`Channel '${name}' is already registered before ${kind}.`);
    }
}
function assertNoLegacyModuleOptions(options) {
    const legacy = options;
    if (legacy.channels !== undefined) {
        throw new framework.ZLinkConfigurationException('NestJS ZLinkModule uses clientServerChannels, fanoutChannels, dealerMeshChannels, and routerMeshes instead of channels.');
    }
    if (legacy.routeChannels !== undefined) {
        throw new framework.ZLinkConfigurationException('NestJS ZLinkModule uses routerMeshes instead of routeChannels.');
    }
    if (legacy.streamNodes !== undefined) {
        throw new framework.ZLinkConfigurationException('NestJS ZLinkModule uses streams instead of streamNodes.');
    }
    return options;
}
function createDiscoveredRequestHandlers(providerRefs, handlerGroups, moduleRef) {
    return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, 'request', (ref, metadata) => ({
        async handle(payload, context) {
            return await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
        }
    }));
}
function createDiscoveredSendHandlers(providerRefs, handlerGroups, moduleRef) {
    return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, 'send', (ref, metadata) => ({
        async handle(payload, context) {
            await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
        }
    }));
}
function createDiscoveredPublishHandlers(providerRefs, handlerGroups, moduleRef) {
    return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, 'publish', (ref, metadata) => ({
        async handle(payload, context) {
            await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
        }
    }));
}
function createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, kind, createHandler) {
    const descriptors = createDiscoveredHandlerDescriptors(providerRefs, handlerGroups, kind);
    return descriptors.map(({ ref, metadata }) => ({
        packetName: metadata.packetName,
        handler: createHandler(ref, metadata)
    }));
}
function createDiscoveredHandlerDescriptors(providerRefs, handlerGroups, kind) {
    if ((handlerGroups ?? []).length === 0) {
        return [];
    }
    const groups = new Set(handlerGroups);
    const seen = new Map();
    const selected = [];
    for (const ref of providerRefs) {
        const metadata = ref.metadata;
        if (metadata.kind !== kind || !groups.has(metadata.groupName)) {
            continue;
        }
        const key = `${metadata.kind}:${metadata.packetName}`;
        const previousType = seen.get(key);
        if (previousType === ref.handlerKey) {
            continue;
        }
        if (previousType !== undefined) {
            throw new framework.ZLinkConfigurationException(`Duplicate handler '${metadata.groupName}:${metadata.kind}:${metadata.packetName}'.`);
        }
        seen.set(key, ref.handlerKey);
        selected.push({ ref, metadata });
    }
    return selected;
}
function discoverProviderRefs(discovery, moduleRef) {
    const refs = [];
    const seen = new Set();
    for (const wrapper of discovery.getProviders()) {
        const token = wrapper.token;
        if (token === undefined) {
            continue;
        }
        const candidates = [wrapper.metatype, wrapper.instance?.constructor, token]
            .filter((value) => typeof value === 'function' || typeof value === 'string' || typeof value === 'symbol');
        for (const handlerKey of new Set(candidates)) {
            for (const metadata of readNestHandlerMetadata(handlerKey)) {
                const handlerName = handlerKeyName(handlerKey);
                const key = `${String(token)}:${handlerName}:${metadata.groupName}:${metadata.kind}:${metadata.packetName}`;
                if (seen.has(key)) {
                    continue;
                }
                seen.add(key);
                refs.push({
                    handlerKey,
                    handlerName,
                    token,
                    instance: wrapper.instance === undefined ? undefined : wrapper.instance,
                    metadata
                });
            }
        }
    }
    for (const [handlerKey, metadataList] of nestHandlerMetadataByToken) {
        if (!isInjectionToken(handlerKey)) {
            continue;
        }
        const instance = tryGetProviderInstance(moduleRef, handlerKey);
        if (instance === undefined) {
            continue;
        }
        const handlerName = handlerKeyName(handlerKey);
        for (const metadata of metadataList) {
            const key = `${String(handlerKey)}:${handlerName}:${metadata.groupName}:${metadata.kind}:${metadata.packetName}`;
            if (seen.has(key)) {
                continue;
            }
            seen.add(key);
            refs.push({
                handlerKey,
                handlerName,
                token: handlerKey,
                instance,
                metadata
            });
        }
    }
    return refs;
}
function discoverSpotActorProviderRefs(discovery, moduleRef) {
    const refs = [];
    const seen = new Set();
    for (const wrapper of discovery.getProviders()) {
        const token = wrapper.token;
        if (token === undefined) {
            continue;
        }
        const candidates = [wrapper.metatype, wrapper.instance?.constructor, token]
            .filter((value) => typeof value === 'function' || typeof value === 'string' || typeof value === 'symbol');
        for (const handlerKey of new Set(candidates)) {
            for (const metadata of readNestSpotActorHandlerMetadata(handlerKey)) {
                if (typeof handlerKey !== 'function') {
                    throw new framework.ZLinkConfigurationException('ZLink SPOT actor handler decorators must be applied to class providers.');
                }
                const handlerName = handlerKeyName(handlerKey);
                const key = `${String(token)}:${handlerName}:${metadata.kind}:${metadata.packetName}`;
                if (seen.has(key)) {
                    continue;
                }
                seen.add(key);
                refs.push({
                    handlerKey: handlerKey,
                    handlerName,
                    token,
                    metadata
                });
            }
        }
    }
    for (const [handlerKey, metadataList] of nestSpotActorHandlerMetadataByToken) {
        if (typeof handlerKey !== 'function') {
            continue;
        }
        if (tryGetProviderInstance(moduleRef, handlerKey) === undefined) {
            continue;
        }
        const handlerName = handlerKeyName(handlerKey);
        for (const metadata of metadataList) {
            const key = `${String(handlerKey)}:${handlerName}:${metadata.kind}:${metadata.packetName}`;
            if (seen.has(key)) {
                continue;
            }
            seen.add(key);
            refs.push({
                handlerKey: handlerKey,
                handlerName,
                token: handlerKey,
                metadata
            });
        }
    }
    return refs;
}
function discoverSpotTimerProviderRefs(discovery, moduleRef) {
    const refs = [];
    const seen = new Set();
    for (const wrapper of discovery.getProviders()) {
        const token = wrapper.token;
        if (token === undefined) {
            continue;
        }
        const candidates = [wrapper.metatype, wrapper.instance?.constructor, token]
            .filter((value) => typeof value === 'function' || typeof value === 'string' || typeof value === 'symbol');
        for (const handlerKey of new Set(candidates)) {
            for (const metadata of readNestSpotTimerHandlerMetadata(handlerKey)) {
                if (typeof handlerKey !== 'function') {
                    throw new framework.ZLinkConfigurationException('ZLink SPOT timer handler decorators must be applied to class providers.');
                }
                const handlerName = handlerKeyName(handlerKey);
                const key = `${String(token)}:${handlerName}:${metadata.name}`;
                if (seen.has(key)) {
                    continue;
                }
                seen.add(key);
                refs.push({
                    handlerKey: handlerKey,
                    handlerName,
                    token,
                    metadata
                });
            }
        }
    }
    for (const [handlerKey, metadataList] of nestSpotTimerHandlerMetadataByToken) {
        if (typeof handlerKey !== 'function') {
            continue;
        }
        if (tryGetProviderInstance(moduleRef, handlerKey) === undefined) {
            continue;
        }
        const handlerName = handlerKeyName(handlerKey);
        for (const metadata of metadataList) {
            const key = `${String(handlerKey)}:${handlerName}:${metadata.name}`;
            if (seen.has(key)) {
                continue;
            }
            seen.add(key);
            refs.push({
                handlerKey: handlerKey,
                handlerName,
                token: handlerKey,
                metadata
            });
        }
    }
    return refs;
}
function isInjectionToken(value) {
    return typeof value === 'function' || typeof value === 'string' || typeof value === 'symbol';
}
function tryGetProviderInstance(moduleRef, token) {
    try {
        return moduleRef.get(token, { strict: false });
    }
    catch {
        return undefined;
    }
}
async function invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context) {
    const instance = moduleRef.get(ref.token, { strict: false });
    const methodName = metadata.methodName ?? 'handle';
    const method = instance[methodName];
    if (typeof method !== 'function') {
        throw new framework.ZLinkConfigurationException(`Discovered handler ${ref.handlerName}.${methodName} is not callable.`);
    }
    return await method.call(instance, decodePayload(payload), context);
}
function handlerKeyName(handlerKey) {
    if (typeof handlerKey === 'function') {
        return handlerKey.name;
    }
    if (typeof handlerKey === 'symbol') {
        return handlerKey.description ?? handlerKey.toString();
    }
    return handlerKey;
}
function decodePayload(payload) {
    if (Buffer.isBuffer(payload) || payload instanceof Uint8Array) {
        return JSON.parse(Buffer.from(payload).toString());
    }
    if (typeof payload === 'string') {
        return JSON.parse(payload);
    }
    return payload;
}
function hasNestHandlerDiscovery(options) {
    return hasConfiguredSpotNodes(options.spotNodes) || [
        ...Object.values(options.clientServerChannels ?? {}),
        ...Object.values(options.fanoutChannels ?? {}),
        ...Object.values(options.routerMeshes ?? {})
    ].some((channel) => (channel.handlerGroups ?? []).length > 0);
}
function hasConfiguredSpotNodes(value) {
    if (value === undefined) {
        return false;
    }
    return Array.isArray(value) ? value.length > 0 : Object.keys(value).length > 0;
}
const ALWAYS_AVAILABLE_CLIENT_PROVIDER_SPECS = [
    {
        token: exports.ZLINK_CHANNEL_CLIENT,
        create: (registration, runtime) => new framework.DefaultZLinkChannelClient(registration, runtime.channelTransport)
    },
    {
        token: exports.ZLINK_FANOUT_CLIENT,
        create: (registration, runtime) => new framework.DefaultZLinkFanoutClient(registration, runtime.channelTransport)
    },
    {
        token: exports.ZLINK_ROUTE_CLIENT,
        create: (registration, runtime) => new framework.DefaultZLinkRouteClient(registration, runtime.routeTransport)
    },
    {
        token: exports.ZLINK_BOUND_SESSION_FACTORY,
        create: (_registration, runtime) => runtime.boundSessionFactory
    }
];
function alwaysAvailableClientProviders(registration) {
    return [
        ...ALWAYS_AVAILABLE_CLIENT_PROVIDER_SPECS.map((spec) => createAlwaysAvailableClientProvider(spec, registration)),
        { provide: exports.ZLINK_MESSAGE_METADATA_POLICY, useValue: Object.freeze({ forward: true }) }
    ];
}
function createAlwaysAvailableClientProvider(spec, registration) {
    if (registration !== undefined) {
        return {
            provide: spec.token,
            inject: [exports.ZLINK_FRAMEWORK_RUNTIME],
            useFactory: (runtime) => spec.create(registration, runtime)
        };
    }
    return {
        provide: spec.token,
        inject: [exports.ZLINK_FRAMEWORK_REGISTRATION, exports.ZLINK_FRAMEWORK_RUNTIME],
        useFactory: (resolved, runtime) => spec.create(resolved, runtime)
    };
}
function alwaysAvailableClientTokens() {
    return [
        exports.ZLINK_CHANNEL_CLIENT,
        exports.ZLINK_ROUTE_CLIENT,
        exports.ZLINK_FANOUT_CLIENT,
        exports.ZLINK_BOUND_SESSION_FACTORY,
        exports.ZLINK_MESSAGE_METADATA_POLICY
    ];
}
function conditionalClientProviders(registration) {
    const providers = CONDITIONAL_CLIENT_PROVIDER_SPECS
        .filter((spec) => spec.isEnabled(registration))
        .map((spec) => createConditionalClientProvider(spec, registration));
    if (framework.hasSpotRemoteAddressResolver(registration)) {
        providers.push(...spotRemoteAddressResolverProviders(registration));
    }
    return providers;
}
const CONDITIONAL_CLIENT_PROVIDER_SPECS = [
    {
        token: exports.ZLINK_SPOT_MANAGER,
        requiresRuntime: false,
        isEnabled: (registration) => framework.hasSpotNode(registration),
        create: (registration, _runtime, moduleRef, discovery) => createSpotManager(registration, moduleRef, discovery)
    },
    {
        token: exports.ZLINK_SPOT_OUTBOUND,
        requiresRuntime: true,
        isEnabled: (registration) => framework.hasSpotNode(registration),
        create: (registration, runtime, moduleRef, discovery) => createSpotOutbound(registration, requireRuntime(runtime), moduleRef, discovery)
    },
    {
        token: exports.ZLINK_SPOT_PUBLISHER_CLIENT,
        requiresRuntime: true,
        isEnabled: (registration) => framework.hasSpotPublisherClient(registration),
        create: (registration, runtime) => new framework.DefaultZLinkSpotPublisherClient(registration, requireRuntime(runtime).spotPublisherTransport)
    },
    {
        token: exports.ZLINK_ACTOR_MANAGER,
        requiresRuntime: true,
        isEnabled: (registration) => framework.hasActorManager(registration),
        create: (registration, runtime, moduleRef, discovery) => {
            const host = requireRuntime(runtime);
            const actorManager = new framework.DefaultZLinkActorManager({
                actorFactories: registration.actorFactories,
                ...host.createActorManagerOptions?.(),
                boundSessionFactory: host.boundSessionFactory.create.bind(host.boundSessionFactory),
                providerResolver: moduleRef === undefined ? undefined : createProviderResolver(moduleRef, discovery)
            });
            host.setActorManager?.(actorManager);
            return actorManager;
        }
    }
];
function conditionalClientProvidersForFactory() {
    return [
        ...CONDITIONAL_CLIENT_PROVIDER_SPECS.map(createConditionalClientProviderForFactory),
        {
            provide: exports.ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER,
            inject: [exports.ZLINK_FRAMEWORK_REGISTRATION, core_1.ModuleRef, core_1.DiscoveryService],
            useFactory: async (registration, moduleRef, discovery) => {
                if (!framework.hasSpotRemoteAddressResolver(registration)) {
                    return null;
                }
                return await createSpotRemoteAddressResolver(registration, moduleRef, discovery);
            }
        }
    ];
}
function createConditionalClientProviderForFactory(spec) {
    return {
        provide: spec.token,
        inject: spec.requiresRuntime
            ? [exports.ZLINK_FRAMEWORK_REGISTRATION, exports.ZLINK_FRAMEWORK_RUNTIME, core_1.ModuleRef, core_1.DiscoveryService]
            : [exports.ZLINK_FRAMEWORK_REGISTRATION, core_1.ModuleRef, core_1.DiscoveryService],
        useFactory: (registration, runtimeOrModuleRef, moduleRefOrDiscovery, maybeDiscovery) => {
            if (!spec.isEnabled(registration)) {
                return null;
            }
            const runtime = spec.requiresRuntime ? runtimeOrModuleRef : undefined;
            const moduleRef = spec.requiresRuntime ? moduleRefOrDiscovery : runtimeOrModuleRef;
            const discovery = spec.requiresRuntime ? maybeDiscovery : moduleRefOrDiscovery;
            return spec.create(registration, runtime, moduleRef, discovery);
        }
    };
}
function createConditionalClientProvider(spec, registration) {
    return {
        provide: spec.token,
        inject: spec.requiresRuntime ? [exports.ZLINK_FRAMEWORK_RUNTIME, core_1.ModuleRef, core_1.DiscoveryService] : [core_1.ModuleRef, core_1.DiscoveryService],
        useFactory: (runtimeOrModuleRef, moduleRefOrDiscovery, maybeDiscovery) => {
            const runtime = spec.requiresRuntime ? runtimeOrModuleRef : undefined;
            const moduleRef = spec.requiresRuntime ? moduleRefOrDiscovery : runtimeOrModuleRef;
            const discovery = spec.requiresRuntime ? maybeDiscovery : moduleRefOrDiscovery;
            return spec.create(registration, runtime, moduleRef, discovery);
        }
    };
}
function requireRuntime(runtime) {
    if (runtime === undefined) {
        throw new framework.ZLinkConfigurationException('ZLink runtime host is not available.');
    }
    return runtime;
}
function conditionalClientTokens() {
    return [
        exports.ZLINK_SPOT_MANAGER,
        exports.ZLINK_SPOT_OUTBOUND,
        exports.ZLINK_SPOT_PUBLISHER_CLIENT,
        exports.ZLINK_ACTOR_MANAGER,
        exports.ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER
    ];
}
function createRuntimeHost(registration, moduleRef, discovery) {
    const runtime = new framework.ZLinkFrameworkRuntimeHost({
        registration,
        providerResolver: createProviderResolver(moduleRef, discovery)
    });
    runtime.onModuleInit = async () => {
        await runtime.start();
    };
    runtime.onModuleDestroy = async () => {
        await runtime.stop();
    };
    return runtime;
}
function createProviderResolver(moduleRef, discovery) {
    return {
        get(type) {
            const discovered = findDiscoveredProviderInstance(discovery, type);
            if (discovered !== undefined) {
                return discovered;
            }
            try {
                return moduleRef.get(type, { strict: false });
            }
            catch {
                return undefined;
            }
        },
        async create(type) {
            const existing = this.get?.(type);
            if (existing !== undefined) {
                return existing;
            }
            return moduleRef.create(type);
        }
    };
}
function findDiscoveredProviderInstance(discovery, type) {
    for (const wrapper of discovery?.getProviders() ?? []) {
        if (wrapper.instance !== undefined
            && wrapper.instance !== null
            && (wrapper.token === type
                || wrapper.metatype === type
                || wrapper.instance.constructor === type)) {
            return wrapper.instance;
        }
    }
    return undefined;
}
function providerToken(provider) {
    return typeof provider === 'function' ? provider : provider.provide;
}
function createSpotManager(registration, moduleRef, discovery) {
    return new framework.DefaultZLinkSpotManager({
        spotFactories: [...registration.spotFactories],
        spotTimerHandlers: [...registration.spotNodes.values()]
            .flatMap((spotNode) => [...(spotNode.spotTimerHandlers ?? [])]),
        spotActorSendHandlers: [...registration.spotNodes.values()]
            .flatMap((spotNode) => [...(spotNode.spotActorSendHandlers ?? [])]),
        spotActorRequestHandlers: [...registration.spotNodes.values()]
            .flatMap((spotNode) => [...(spotNode.spotActorRequestHandlers ?? [])]),
        providerResolver: moduleRef === undefined ? undefined : createProviderResolver(moduleRef, discovery),
        workerRuntime: new framework.ZLinkSpotWorkerRuntime(registration.worker)
    });
}
async function createSpotOutbound(registration, runtime, moduleRef, discovery) {
    const resolver = framework.hasSpotRemoteAddressResolver(registration)
        ? await createSpotRemoteAddressResolver(registration, moduleRef, discovery)
        : undefined;
    return new framework.DefaultZLinkSpotOutbound(new framework.ZLinkSpotSerialExecutor(), undefined, undefined, resolver, runtime.routeTransport);
}
async function createSpotRemoteAddressResolver(registration, moduleRef, discovery) {
    if (registration.spotRemoteAddressResolverType !== undefined) {
        const providerResolver = moduleRef === undefined ? undefined : createProviderResolver(moduleRef, discovery);
        const resolverType = registration.spotRemoteAddressResolverType;
        const resolver = await providerResolver?.create?.(resolverType);
        if (resolver === undefined) {
            throw new framework.ZLinkConfigurationException('Spot remote address resolver provider is not available.');
        }
        return resolver;
    }
    if (registration.registrySpotRemoteAddresses !== undefined) {
        return new framework.ZLinkRegistrySpotRemoteAddressResolver({ registration });
    }
    throw new framework.ZLinkConfigurationException('Spot remote address resolver is not registered.');
}
function spotRemoteAddressResolverProviders(registration) {
    const resolverType = registration.spotRemoteAddressResolverType;
    if (resolverType !== undefined) {
        return [
            { provide: resolverType, useClass: resolverType },
            {
                provide: exports.ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER,
                inject: [resolverType],
                useFactory: (resolver) => resolver
            }
        ];
    }
    return [{
            provide: exports.ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER,
            inject: [core_1.ModuleRef, core_1.DiscoveryService],
            useFactory: (moduleRef, discovery) => createSpotRemoteAddressResolver(registration, moduleRef, discovery)
        }];
}
function loadFramework() {
    const requireFramework = (0, node_module_1.createRequire)(__filename);
    return requireFramework(node_path_1.default.resolve(__dirname, '../../framework/dist/internal'));
}
