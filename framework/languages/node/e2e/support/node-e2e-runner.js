#!/usr/bin/env node
const assert = require('node:assert/strict');
const fs = require('node:fs');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { fork } = require('node:child_process');
const { Inject } = require('@nestjs/common');

const nodeRoot = path.resolve(__dirname, '../..');
const nestjsPublic = require(path.join(nodeRoot, 'packages/nestjs/dist'));

const suites = new Map([
  ['RegistryMessaging', registryMessaging],
  ['PubSub', pubSub],
  ['RegistrationCodec', registrationCodec],
  ['Monitoring', monitoring],
  ['DiscoveryRegistryHa', discoveryRegistryHa],
  ['ResilienceLifecycle', resilienceLifecycle],
  ['SpotService', spotService]
]);

async function main() {
  const suiteName = process.argv[2];
  if (suiteName === '__registry_child') {
    await registryChildMain(JSON.parse(process.argv[3]));
    return;
  }
  if (suiteName === '__rl_provider_child') {
    await resilienceProviderChildMain(JSON.parse(process.argv[3]));
    return;
  }
  const suite = suites.get(suiteName);
  if (suite === undefined) {
    throw new Error(`Unknown Node e2e suite: ${suiteName}`);
  }

  await suite();
}

async function registryMessaging() {
  const apiEndpoint = uniqueEndpoint('rm-api');
  const nestjs = loadNest();
  const framework = loadFramework();
  await runRegistryMessagingDiscovery(nestjs, framework);
  await runRegistryMessagingSameRidFailover(nestjs, framework);
  await runRegistryMessagingCrossChannelDiscovery(nestjs, framework);
  await runRegistryMessagingScale(nestjs, framework);
  await runRegistryMessagingTargetRidRoute(nestjs);
  await runRegistryMessagingManualDistribution(nestjs);
  RMFlowObserver.events = [];
  EchoHandler.completed = [];
  const builder = nestjs.zlinkFramework();
  builder.configureDispatch()
    .setMessageFlowObserver(RMFlowObserver)
    .messageFlow(framework.ZLinkMessageFlowLogMode.ErrorsOnly);
  builder.addClientServerChannel('rm.api')
    .enableServer(apiEndpoint)
    .enableClient(apiEndpoint)
    .routingId('rm-provider-a')
    .addRequestHandler('rm.echo', EchoHandler)
    .addSendHandler('rm.audit', AuditHandler);
  const { app } = await startApp(
    nestModule('RegistryMessagingModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(builder.build())
      ],
      providers: [EchoHandler, AuditHandler, RMFlowObserver]
    })
  );

  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const reply = await client
      .requestToChannel('rm.api', { id: 7, text: 'hello' })
      .packetName('rm.echo')
      .timeout(3000)
      .submit();
    assert.deepEqual(reply, { id: 7, text: 'hello', handledBy: 'rm-provider-a' });

    const largeText = 'payload-'.repeat(16 * 1024);
    const largeReply = await client
      .requestToChannel('rm.api', { id: 15, text: largeText })
      .packetName('rm.echo')
      .timeout(3000)
      .submit();
    assert.equal(largeReply.id, 15);
    assert.equal(largeReply.text.length, largeText.length);
    assert.equal(largeReply.text, largeText);
    assert.equal(largeReply.handledBy, 'rm-provider-a');
    selfCheck('RM-C8-PAYLOAD-ROUNDTRIP');

    await client
      .sendToChannel('rm.api', { id: 8, text: 'audit' })
      .packetName('rm.audit')
      .submit();
    await waitFor(() => AuditHandler.messages.some((message) => message.id === 8));
    selfCheck('RM-MANUAL-REQUEST-SEND');
    marker('RM-A2');

    await assert.rejects(
      () => client
        .requestToChannel('rm.api', { id: 12, text: 'slow' })
        .packetName('rm.echo')
        .timeout(50)
        .submit(),
      /timed out|timeout|result 101/i
    );
    const afterTimeout = await client
      .requestToChannel('rm.api', { id: 13, text: 'after-timeout' })
      .packetName('rm.echo')
      .timeout(3000)
      .submit();
    assert.deepEqual(afterTimeout, { id: 13, text: 'after-timeout', handledBy: 'rm-provider-a' });
    await waitFor(() => EchoHandler.completed.some((message) => message.id === 12), 3000);
    const afterLateReply = await client
      .requestToChannel('rm.api', { id: 14, text: 'after-late-reply' })
      .packetName('rm.echo')
      .timeout(3000)
      .submit();
    assert.deepEqual(afterLateReply, { id: 14, text: 'after-late-reply', handledBy: 'rm-provider-a' });
    selfCheck('RM-MANUAL-TIMEOUT-LATE-REPLY');

    await assert.rejects(
      () => client
        .requestToChannel('rm.api', { id: 9, text: 'missing-request' })
        .packetName('rm.missing.request')
        .timeout(1000)
        .submit(),
      /No channel request handler is registered for 'rm\.api:rm\.missing\.request'/
    );
    await waitFor(() => RMFlowObserver.events.some((event) =>
      event.messageKind === framework.ZLinkDispatchMessageKind.Request &&
      event.errorReason === framework.ZLinkDispatchErrorReason.HandlerMissing &&
      event.errorAction === framework.ZLinkDispatchErrorAction.ReplyError &&
      event.packetName === 'rm.missing.request' &&
      event.channelName === 'rm.api'));

    await client
      .sendToChannel('rm.api', { id: 10, text: 'missing-send' })
      .packetName('rm.missing.send')
      .submit();
    await waitFor(() => RMFlowObserver.events.some((event) =>
      event.messageKind === framework.ZLinkDispatchMessageKind.Send &&
      event.errorReason === framework.ZLinkDispatchErrorReason.HandlerMissing &&
      event.errorAction === framework.ZLinkDispatchErrorAction.Drop &&
      event.packetName === 'rm.missing.send' &&
      event.channelName === 'rm.api'));

    const afterNegative = await client
      .requestToChannel('rm.api', { id: 11, text: 'after-negative' })
      .packetName('rm.echo')
      .timeout(3000)
      .submit();
    assert.deepEqual(afterNegative, { id: 11, text: 'after-negative', handledBy: 'rm-provider-a' });
    marker('RM-C5');
  } finally {
    await app.close();
  }
}

async function runRegistryMessagingDiscovery(nestjs, framework) {
  RMDiscoveryHandler.requests = [];
  RMDiscoveryHandler.commands = [];
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const providerAEndpoint = await reserveTcpEndpoint();
  const providerBEndpoint = await reserveTcpEndpoint();
  const apps = [];

  try {
    const registry = await startApp(
      nestModule('RegistryMessagingRegistryModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 71
          })
        ]
      })
    );
    apps.push(registry.app);
    await startRegistryMessagingDiscoveryProvider(
      nestjs,
      registryRouterEndpoint,
      providerAEndpoint,
      'api-a',
      apps
    );
    await startRegistryMessagingDiscoveryProvider(
      nestjs,
      registryRouterEndpoint,
      providerBEndpoint,
      'api-b',
      apps
    );
    const consumer = await startApp(
      nestModule('RegistryMessagingConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(registryRouterEndpoint)
            .addClientServerChannel('rm.discovery')
              .enableClient()
            .build())
        ]
      })
    );
    apps.push(consumer.app);

    const query = registry.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    const readyProviders = await waitForRegistryMessagingReadyProviders(query, framework, [providerAEndpoint, providerBEndpoint]);

    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const reply = await submitUntilReachable(() =>
      client
        .requestToChannel('rm.discovery', { id: 1, text: 'registry-discovery' })
        .packetName('rm.discovery.echo')
        .timeout(1000)
        .submit());
    assert.deepEqual(reply, {
      id: 1,
      text: 'registry-discovery',
      handledBy: RMDiscoveryHandler.requests[0]?.handledBy
    });
    assert.ok(readyProviders.some((provider) => provider.routingId === reply.handledBy));
    assert.deepEqual(RMDiscoveryHandler.requests, [{
      id: 1,
      text: 'registry-discovery',
      handledBy: reply.handledBy
    }]);
    marker('RM-A1');

    await client
      .sendToChannel('rm.discovery', { id: 2, text: 'registry-command' })
      .packetName('rm.discovery.command')
      .submit();
    await waitFor(() => RMDiscoveryHandler.commands.length === 1);
    assert.deepEqual(RMDiscoveryHandler.commands, [{
      id: 2,
      text: 'registry-command',
      handledBy: RMDiscoveryHandler.commands[0]?.handledBy
    }]);
    assert.ok(readyProviders.some((provider) => provider.routingId === RMDiscoveryHandler.commands[0].handledBy));
    marker('RM-C1');

    await assert.rejects(
      () => client
        .requestToChannel('rm.discovery', { id: 3, text: 'slow' })
        .packetName('rm.discovery.echo')
        .timeout(50)
        .submit(),
      /timed out|timeout|result 101/i
    );
    const afterTimeout = await client
      .requestToChannel('rm.discovery', { id: 4, text: 'after-timeout' })
      .packetName('rm.discovery.echo')
      .timeout(1000)
      .submit();
    assert.deepEqual(afterTimeout, {
      id: 4,
      text: 'after-timeout',
      handledBy: RMDiscoveryHandler.requests.find((request) => request.id === 4)?.handledBy
    });
    assert.ok(readyProviders.some((provider) => provider.routingId === afterTimeout.handledBy));
    await waitFor(() => RMDiscoveryHandler.requests.some((request) => request.id === 3), 3000);
    const afterLateReply = await client
      .requestToChannel('rm.discovery', { id: 5, text: 'after-late-reply' })
      .packetName('rm.discovery.echo')
      .timeout(1000)
      .submit();
    assert.deepEqual(afterLateReply, {
      id: 5,
      text: 'after-late-reply',
      handledBy: RMDiscoveryHandler.requests.find((request) => request.id === 5)?.handledBy
    });
    assert.ok(readyProviders.some((provider) => provider.routingId === afterLateReply.handledBy));
    marker('RM-C4');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function waitForRegistryMessagingReadyProviders(query, framework, endpoints) {
  const expected = new Set(endpoints);
  const deadline = Date.now() + 5000;
  let lastTopology = [];
  while (Date.now() < deadline) {
    lastTopology = await query.topology({ channelName: 'rm.discovery' });
    const readyEntries = lastTopology.filter((entry) =>
      entry.autoConnectType === framework.ZLinkAutoConnectType.ClientServer &&
      entry.serviceKind === framework.ZLinkServiceKind.Socket &&
      entry.serviceRole === framework.ZLinkServiceRole.Router &&
      entry.state === framework.ZLinkTopologyState.Ready);
    if ([...expected].every((endpoint) => readyEntries.some((entry) => entry.endpoint === endpoint))) {
      return [...expected].map((endpoint) => {
        const entry = readyEntries.find((candidate) => candidate.endpoint === endpoint);
        return {
          endpoint,
          routingId: entry.routingId
        };
      });
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`RM-A1 providers were not ready in registry topology: ${stringifyDiagnostic(lastTopology)}`);
}

async function startRegistryMessagingDiscoveryProvider(nestjs, registryRouterEndpoint, providerEndpoint, routingId, apps) {
  const handler = createRMDiscoveryHandler(routingId);
  const started = await startApp(
    nestModule(`RegistryMessagingProvider${routingId}Module`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .useDiscovery()
            .addRegistryEndpoint(registryRouterEndpoint)
          .addClientServerChannel('rm.discovery')
            .enableServer(providerEndpoint)
            .routingId(routingId)
            .addRequestHandler('rm.discovery.echo', handler)
            .addSendHandler('rm.discovery.command', handler)
          .build())
      ],
      providers: [handler]
    })
  );
  apps.push(started.app);
}

async function runRegistryMessagingSameRidFailover(nestjs, framework) {
  RMFailoverHandler.requests = [];
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const providerV1Endpoint = await reserveTcpEndpoint();
  const providerV2Endpoint = await reserveTcpEndpoint();
  const apps = [];
  let providerV1;
  let providerV2;

  try {
    const registry = await startApp(
      nestModule('RegistryMessagingFailoverRegistryModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 72
          })
        ]
      })
    );
    apps.push(registry.app);
    providerV1 = await startRegistryMessagingFailoverProvider(
      nestjs,
      registryRouterEndpoint,
      providerV1Endpoint,
      'provider-v1'
    );
    apps.push(providerV1.app);
    const consumer = await startApp(
      nestModule('RegistryMessagingFailoverConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(registryRouterEndpoint)
            .addClientServerChannel('rm.failover')
              .enableClient()
            .build())
        ]
      })
    );
    apps.push(consumer.app);

    const query = registry.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryMessagingSingleReadyProvider(query, framework, 'rm.failover', 'api-a', providerV1Endpoint);
    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const before = await submitUntilReachable(() =>
      client
        .requestToChannel('rm.failover', { requestId: 'rm-a4-v1' })
        .packetName('rm.failover.probe')
        .timeout(1000)
        .submit());
    assert.deepEqual(before, { requestId: 'rm-a4-v1', handledBy: 'provider-v1' });

    await providerV1.app.close();
    apps.splice(apps.indexOf(providerV1.app), 1);
    providerV2 = await startRegistryMessagingFailoverProvider(
      nestjs,
      registryRouterEndpoint,
      providerV2Endpoint,
      'provider-v2'
    );
    apps.push(providerV2.app);
    await waitForRegistryMessagingSingleReadyProvider(query, framework, 'rm.failover', 'api-a', providerV2Endpoint);

    const providerV1Count = RMFailoverHandler.requests.filter((request) => request.handledBy === 'provider-v1').length;
    for (let i = 0; i < 20; i += 1) {
      const requestId = `rm-a4-v2-${i}`;
      const reply = await client
        .requestToChannel('rm.failover', { requestId })
        .packetName('rm.failover.probe')
        .timeout(1000)
        .submit();
      assert.deepEqual(reply, { requestId, handledBy: 'provider-v2' });
    }
    assert.equal(RMFailoverHandler.requests.filter((request) => request.handledBy === 'provider-v1').length, providerV1Count);
    marker('RM-A4');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startRegistryMessagingFailoverProvider(nestjs, registryRouterEndpoint, providerEndpoint, providerId) {
  const handler = createRMFailoverHandler(providerId);
  return startApp(
    nestModule(`RegistryMessagingFailover${providerId}Module`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .useDiscovery()
            .addRegistryEndpoint(registryRouterEndpoint)
          .addClientServerChannel('rm.failover')
            .enableServer(providerEndpoint)
            .routingId('api-a')
            .addRequestHandler('rm.failover.probe', handler)
          .build())
      ],
      providers: [handler]
    })
  );
}

async function waitForRegistryMessagingSingleReadyProvider(query, framework, channelName, routingId, endpoint) {
  const deadline = Date.now() + 5000;
  let lastTopology = [];
  while (Date.now() < deadline) {
    lastTopology = await query.topology({
      autoConnectType: framework.ZLinkAutoConnectType.ClientServer,
      serviceKind: framework.ZLinkServiceKind.Socket,
      serviceRole: framework.ZLinkServiceRole.Router,
      channelName,
      routingId,
      state: framework.ZLinkTopologyState.Ready
    });
    if (lastTopology.length === 1 && lastTopology[0].routingId === routingId && lastTopology[0].endpoint === endpoint) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`RM-A4 provider did not converge: ${stringifyDiagnostic(lastTopology)}`);
}

async function runRegistryMessagingCrossChannelDiscovery(nestjs, framework) {
  RMCrossChannelHandler.requests = [];
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const apiEndpoint = await reserveTcpEndpoint();
  const workflowEndpoint = await reserveTcpEndpoint();
  const apps = [];

  try {
    const registry = await startApp(
      nestModule('RegistryMessagingCrossChannelRegistryModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 73
          })
        ]
      })
    );
    apps.push(registry.app);
    await startRegistryMessagingCrossChannelProvider(
      nestjs,
      registryRouterEndpoint,
      'rm.cross.api',
      apiEndpoint,
      'api-provider',
      apps
    );
    await startRegistryMessagingCrossChannelProvider(
      nestjs,
      registryRouterEndpoint,
      'rm.cross.workflow',
      workflowEndpoint,
      'workflow-provider',
      apps
    );
    const consumer = await startApp(
      nestModule('RegistryMessagingCrossChannelConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(registryRouterEndpoint)
            .addClientServerChannel('rm.cross.api')
              .enableClient()
            .addClientServerChannel('rm.cross.workflow')
              .enableClient()
            .build())
        ]
      })
    );
    apps.push(consumer.app);

    const query = registry.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rm.cross.api', [apiEndpoint]);
    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rm.cross.workflow', [workflowEndpoint]);

    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const apiReply = await submitUntilReachable(() =>
      client
        .requestToChannel('rm.cross.api', { requestId: 'rm-a6-api' })
        .packetName('rm.cross.probe')
        .timeout(1000)
        .submit());
    assert.deepEqual(apiReply, {
      requestId: 'rm-a6-api',
      handledBy: 'api-provider',
      channelName: 'rm.cross.api'
    });
    const workflowReply = await submitUntilReachable(() =>
      client
        .requestToChannel('rm.cross.workflow', { requestId: 'rm-a6-workflow' })
        .packetName('rm.cross.probe')
        .timeout(1000)
        .submit());
    assert.deepEqual(workflowReply, {
      requestId: 'rm-a6-workflow',
      handledBy: 'workflow-provider',
      channelName: 'rm.cross.workflow'
    });
    assert.deepEqual(RMCrossChannelHandler.requests, [
      { requestId: 'rm-a6-api', handledBy: 'api-provider', channelName: 'rm.cross.api' },
      { requestId: 'rm-a6-workflow', handledBy: 'workflow-provider', channelName: 'rm.cross.workflow' }
    ]);
    marker('RM-A6');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startRegistryMessagingCrossChannelProvider(nestjs, registryRouterEndpoint, channelName, endpoint, providerId, apps) {
  const handler = createRMCrossChannelHandler(providerId);
  const started = await startApp(
    nestModule(`RegistryMessagingCrossChannel${providerId}Module`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .useDiscovery()
            .addRegistryEndpoint(registryRouterEndpoint)
          .addClientServerChannel(channelName)
            .enableServer(endpoint)
            .routingId(providerId)
            .addRequestHandler('rm.cross.probe', handler)
          .build())
      ],
      providers: [handler]
    })
  );
  apps.push(started.app);
}

async function waitForRegistryMessagingChannelEndpoints(query, framework, channelName, endpoints) {
  const expected = new Set(endpoints);
  const deadline = Date.now() + 5000;
  let lastTopology = [];
  while (Date.now() < deadline) {
    lastTopology = await query.topology({
      autoConnectType: framework.ZLinkAutoConnectType.ClientServer,
      serviceKind: framework.ZLinkServiceKind.Socket,
      serviceRole: framework.ZLinkServiceRole.Router,
      channelName,
      state: framework.ZLinkTopologyState.Ready
    });
    if (lastTopology.length === expected.size && [...expected].every((endpoint) =>
      lastTopology.some((entry) => entry.endpoint === endpoint && entry.channelName === channelName))) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`RM-A6 channel ${channelName} did not converge independently: ${stringifyDiagnostic(lastTopology)}`);
}

async function runRegistryMessagingScale(nestjs, framework) {
  RMScaleHandler.requests = [];
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const providerAEndpoint = await reserveTcpEndpoint();
  const providerBEndpoint = await reserveTcpEndpoint();
  const apps = [];
  let providerB;

  try {
    const registry = await startApp(
      nestModule('RegistryMessagingScaleRegistryModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 74
          })
        ]
      })
    );
    apps.push(registry.app);
    await startRegistryMessagingScaleProvider(nestjs, registryRouterEndpoint, providerAEndpoint, 'provider-a', apps);
    const consumer = await startApp(
      nestModule('RegistryMessagingScaleConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(registryRouterEndpoint)
            .addClientServerChannel('rm.scale')
              .enableClient()
            .build())
        ]
      })
    );
    apps.push(consumer.app);

    const query = registry.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rm.scale', [providerAEndpoint]);
    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const before = await submitUntilReachable(() =>
      client
        .requestToChannel('rm.scale', { requestId: 'rm-b1-before' })
        .packetName('rm.scale.probe')
        .timeout(1000)
        .submit());
    assert.deepEqual(before, { requestId: 'rm-b1-before', handledBy: 'provider-a' });

    providerB = await startRegistryMessagingScaleProvider(nestjs, registryRouterEndpoint, providerBEndpoint, 'provider-b', apps);
    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rm.scale', [providerAEndpoint, providerBEndpoint]);
    await waitForRegistryMessagingScaleProviders(client, ['provider-a', 'provider-b'], 'rm-b1-scale-out');
    marker('RM-B1');

    await providerB.app.close();
    apps.splice(apps.indexOf(providerB.app), 1);
    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rm.scale', [providerAEndpoint]);
    const providerBCount = RMScaleHandler.requests.filter((request) => request.handledBy === 'provider-b').length;
    for (let i = 0; i < 20; i += 1) {
      const requestId = `rm-b2-after-scale-in-${i}`;
      const reply = await client
        .requestToChannel('rm.scale', { requestId })
        .packetName('rm.scale.probe')
        .timeout(1000)
        .submit();
      assert.deepEqual(reply, { requestId, handledBy: 'provider-a' });
    }
    assert.equal(RMScaleHandler.requests.filter((request) => request.handledBy === 'provider-b').length, providerBCount);
    marker('RM-B2');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startRegistryMessagingScaleProvider(nestjs, registryRouterEndpoint, endpoint, providerId, apps) {
  const handler = createRMScaleHandler(providerId);
  const started = await startApp(
    nestModule(`RegistryMessagingScale${providerId}Module`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .useDiscovery()
            .addRegistryEndpoint(registryRouterEndpoint)
          .addClientServerChannel('rm.scale')
            .enableServer(endpoint)
            .routingId(providerId)
            .addRequestHandler('rm.scale.probe', handler)
          .build())
      ],
      providers: [handler]
    })
  );
  apps.push(started.app);
  return started;
}

async function waitForRegistryMessagingScaleProviders(client, providerIds, prefix) {
  const expected = new Set(providerIds);
  const observed = new Set();
  for (let i = 0; i < 80 && observed.size < expected.size; i += 1) {
    const requestId = `${prefix}-${i}`;
    const reply = await submitUntilReachable(() =>
      client
        .requestToChannel('rm.scale', { requestId })
        .packetName('rm.scale.probe')
        .timeout(1000)
        .submit());
    assert.equal(reply.requestId, requestId);
    assert.ok(expected.has(reply.handledBy), `unexpected scale provider: ${reply.handledBy}`);
    observed.add(reply.handledBy);
  }
  assert.deepEqual([...observed].sort(), [...expected].sort());
}

async function runRegistryMessagingTargetRidRoute(nestjs) {
  RMRouteHandler.requests = [];
  const providerAEndpoint = await reserveTcpEndpoint();
  const providerBEndpoint = await reserveTcpEndpoint();
  const consumerEndpoint = await reserveTcpEndpoint();
  const apps = [];

  try {
    await startRegistryMessagingRouteProvider(nestjs, providerAEndpoint, 'api-a', apps);
    await startRegistryMessagingRouteProvider(nestjs, providerBEndpoint, 'api-b', apps);
    const consumer = await startApp(
      nestModule('RegistryMessagingRouteConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .addRouteMesh('rm.route')
              .enableRouter(consumerEndpoint)
              .routingId('consumer')
              .connect([providerAEndpoint, providerBEndpoint])
            .build())
        ]
      })
    );
    apps.push(consumer.app);

    const routeClient = consumer.app.get(nestjs.ZLINK_ROUTE_CLIENT, { strict: false });
    const reply = await submitUntilReachable(() =>
      routeClient
        .request('rm.route', 'api-b', { requestId: 'rm-c2-api-b' })
        .packetName('rm.route.probe')
        .timeout(1000)
        .submit());
    assert.deepEqual(reply, {
      requestId: 'rm-c2-api-b',
      handledBy: 'api-b',
      sourceNodeRid: 'consumer'
    });
    assert.deepEqual(RMRouteHandler.requests, [{
      requestId: 'rm-c2-api-b',
      handledBy: 'api-b',
      sourceNodeRid: 'consumer'
    }]);
    await assert.rejects(
      () => routeClient
        .request('rm.route', 'missing-rid', { requestId: 'rm-c2-missing' })
        .packetName('rm.route.probe')
        .timeout(100)
        .submit(),
      /Host unreachable|timed out|timeout|result 101/i
    );
    assert.equal(RMRouteHandler.requests.some((request) => request.requestId === 'rm-c2-missing'), false);
    marker('RM-C2');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startRegistryMessagingRouteProvider(nestjs, endpoint, routingId, apps) {
  const handler = createRMRouteHandler(routingId);
  const started = await startApp(
    nestModule(`RegistryMessagingRoute${routingId}Module`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .addRouteMesh('rm.route')
            .enableRouter(endpoint)
            .routingId(routingId)
            .addRequestHandler('rm.route.probe', handler)
          .build())
      ],
      providers: [handler]
    })
  );
  apps.push(started.app);
}

async function runRegistryMessagingManualDistribution(nestjs) {
  RMManualDistributionHandler.requests = [];
  const providerAEndpoint = await reserveTcpEndpoint();
  const providerBEndpoint = await reserveTcpEndpoint();
  const apps = [];

  try {
    await startRegistryMessagingManualProvider(nestjs, providerAEndpoint, 'api-a', apps);
    await startRegistryMessagingManualProvider(nestjs, providerBEndpoint, 'api-b', apps);
    const consumer = await startApp(
      nestModule('RegistryMessagingManualDistributionConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .addClientServerChannel('rm.manual-distribution')
              .enableClient([providerAEndpoint, providerBEndpoint])
            .build())
        ]
      })
    );
    apps.push(consumer.app);

    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const seenProviders = new Set();
    for (let i = 0; i < 60 && seenProviders.size < 2; i += 1) {
      const reply = await submitUntilReachable(() =>
        client
          .requestToChannel('rm.manual-distribution', { requestId: `rm-c3-warmup-${i}` })
          .packetName('rm.manual-distribution.probe')
          .timeout(1000)
          .submit());
      seenProviders.add(reply.handledBy);
    }
    assert.deepEqual([...seenProviders].sort(), ['api-a', 'api-b']);

    RMManualDistributionHandler.requests = [];
    const observed = new Map();
    for (let i = 0; i < 90; i += 1) {
      const requestId = `rm-c3-${i}`;
      const reply = await submitUntilReachable(() =>
        client
          .requestToChannel('rm.manual-distribution', { requestId })
          .packetName('rm.manual-distribution.probe')
          .timeout(1000)
          .submit());
      observed.set(requestId, reply.handledBy);
    }

    assert.equal(observed.size, 90);
    assert.equal(RMManualDistributionHandler.requests.length, 90);
    assertManualDistributionHandledOnce(observed);
    const counts = countBy([...observed.values()]);
    assert.ok(counts['api-a'] >= 10, `api-a should handle enough requests: ${stringifyDiagnostic(counts)}`);
    assert.ok(counts['api-b'] >= 10, `api-b should handle enough requests: ${stringifyDiagnostic(counts)}`);
    marker('RM-C3');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startRegistryMessagingManualProvider(nestjs, providerEndpoint, providerId, apps) {
  const handler = createRMManualDistributionHandler(providerId);
  const started = await startApp(
    nestModule(`RegistryMessagingManualDistribution${providerId}Module`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .addClientServerChannel('rm.manual-distribution')
            .enableServer(providerEndpoint)
            .addRequestHandler('rm.manual-distribution.probe', handler)
          .build())
      ],
      providers: [handler]
    })
  );
  apps.push(started.app);
}

function assertManualDistributionHandledOnce(observed) {
  for (const [requestId, handledBy] of observed) {
    const handledCount = RMManualDistributionHandler.requests.filter((request) =>
      request.requestId === requestId && request.handledBy === handledBy).length;
    assert.equal(handledCount, 1, `${requestId} should be handled once by ${handledBy}`);
  }
}

async function pubSub() {
  PubSubAlphaHandler.events = [];
  PubSubBetaHandler.events = [];
  PubSubGammaHandler.events = [];
  PubSubLateHandler.events = [];
  PubSubSlowHandler.started = [];
  PubSubSlowHandler.events = [];
  PubSubFlowObserver.events = [];
  const eventsEndpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  let publisher = await startApp(
    nestModule('PubSubPublisherModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .addFanoutChannel('ps.events')
            .enablePublisher(eventsEndpoint)
          .build())
      ]
    })
  );
  const subscribers = [];

  try {
    const alphaSubscriber = await startPubSubSubscriber('PubSubAlphaModule', PubSubAlphaHandler, eventsEndpoint);
    let betaSubscriber = await startPubSubSubscriber('PubSubBetaModule', PubSubBetaHandler, eventsEndpoint);
    const gammaSubscriber = await startPubSubSubscriber('PubSubGammaModule', PubSubGammaHandler, eventsEndpoint);
    subscribers.push(alphaSubscriber, betaSubscriber, gammaSubscriber);

    let fanout = publisher.app.get(nestjs.ZLINK_FANOUT_CLIENT, { strict: false });
    await publishUntil(fanout, 'all', -1, () =>
      PubSubAlphaHandler.events.some((event) => event.topic === 'all')
      && PubSubBetaHandler.events.some((event) => event.topic === 'all')
      && PubSubGammaHandler.events.some((event) => event.topic === 'all'));
    PubSubAlphaHandler.events = [];
    PubSubBetaHandler.events = [];
    PubSubGammaHandler.events = [];

    for (let seq = 1; seq <= 5; seq += 1) {
      await publishEvent(fanout, 'all', seq);
    }
    await waitFor(() => commonSequence([PubSubAlphaHandler.events, PubSubBetaHandler.events, PubSubGammaHandler.events], 'all', [1, 2, 3, 4, 5]));
    marker('PS-A1');

    PubSubAlphaHandler.events = [];
    PubSubBetaHandler.events = [];
    PubSubGammaHandler.events = [];
    await publishEvent(fanout, 'alpha', 10);
    await publishEvent(fanout, 'beta', 11);
    await publishEvent(fanout, 'gamma', 12);
    await waitFor(() =>
      hasOnlyTopics(PubSubAlphaHandler.events, ['alpha'])
      && hasOnlyTopics(PubSubBetaHandler.events, ['beta'])
      && hasOnlyTopics(PubSubGammaHandler.events, ['alpha', 'gamma'])
      && PubSubAlphaHandler.events.length === 1
      && PubSubBetaHandler.events.length === 1
      && PubSubGammaHandler.events.length === 2);
    marker('PS-A2');

    PubSubLateHandler.events = [];
    await publishEvent(fanout, 'all', 30);
    await waitFor(() => PubSubAlphaHandler.events.some((event) => event.seq === 30));
    subscribers.push(await startPubSubSubscriber('PubSubLateModule', PubSubLateHandler, eventsEndpoint));
    await publishUntil(fanout, 'all', 31, () =>
      PubSubLateHandler.events.some((event) => event.topic === 'all' && event.seq === 31));
    assert.equal(PubSubLateHandler.events.some((event) => event.seq === 30), false);
    marker('PS-A3');

    PubSubAlphaHandler.events = [];
    PubSubBetaHandler.events = [];
    PubSubGammaHandler.events = [];
    await betaSubscriber.app.close();
    subscribers.splice(subscribers.indexOf(betaSubscriber), 1);
    await publishUntil(fanout, 'all', 40, () =>
      PubSubAlphaHandler.events.some((event) => event.seq === 40)
      && PubSubGammaHandler.events.some((event) => event.seq === 40));
    assert.equal(PubSubBetaHandler.events.some((event) => event.seq === 40), false);
    betaSubscriber = await startPubSubSubscriber('PubSubBetaRestartedModule', PubSubBetaHandler, eventsEndpoint);
    subscribers.push(betaSubscriber);
    await publishUntil(fanout, 'all', 41, () =>
      PubSubAlphaHandler.events.some((event) => event.seq === 41)
      && PubSubBetaHandler.events.some((event) => event.seq === 41)
      && PubSubGammaHandler.events.some((event) => event.seq === 41));
    assert.equal(PubSubBetaHandler.events.some((event) => event.seq === 40), false);
    marker('PS-A4');

    PubSubAlphaHandler.events = [];
    PubSubBetaHandler.events = [];
    PubSubGammaHandler.events = [];
    PubSubSlowHandler.started = [];
    PubSubSlowHandler.events = [];
    subscribers.push(await startPubSubSubscriber('PubSubSlowModule', PubSubSlowHandler, eventsEndpoint));
    await publishEvent(fanout, 'all', 50);
    await waitFor(() => PubSubSlowHandler.started.some((event) => event.seq === 50));
    await publishEvent(fanout, 'all', 51);
    await waitFor(() =>
      PubSubAlphaHandler.events.some((event) => event.seq === 51)
      && PubSubBetaHandler.events.some((event) => event.seq === 51)
      && PubSubGammaHandler.events.some((event) => event.seq === 51)
      && !PubSubSlowHandler.events.some((event) => event.seq === 50),
      400);
    marker('PS-B1');

    PubSubAlphaHandler.events = [];
    PubSubBetaHandler.events = [];
    PubSubGammaHandler.events = [];
    await publisher.app.close();
    publisher = await startApp(
      nestModule('PubSubPublisherRestartedModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .addFanoutChannel('ps.events')
              .enablePublisher(eventsEndpoint)
            .build())
        ]
      })
    );
    fanout = publisher.app.get(nestjs.ZLINK_FANOUT_CLIENT, { strict: false });
    await publishUntil(fanout, 'all', 60, () =>
      PubSubAlphaHandler.events.some((event) => event.seq === 60)
      && PubSubBetaHandler.events.some((event) => event.seq === 60)
      && PubSubGammaHandler.events.some((event) => event.seq === 60));
    marker('PS-B2');

    PubSubAlphaHandler.events = [];
    PubSubBetaHandler.events = [];
    PubSubGammaHandler.events = [];
    PubSubFlowObserver.events = [];
    await fanout
      .publishToChannel('ps.events', 'all', { seq: 98 })
      .packetName('ps.missing')
      .submit();
    await waitFor(() => PubSubFlowObserver.events.some((event) =>
      event.packetName === 'ps.missing'
      && event.topic === 'all'
      && event.outcome === 'error'
      && event.errorReason === 'handlerMissing'
      && event.errorAction === 'drop'));
    await publishUntil(fanout, 'all', 99, () =>
      commonSequence([PubSubAlphaHandler.events, PubSubBetaHandler.events, PubSubGammaHandler.events], 'all', [99]));
    marker('PS-C1');
  } finally {
    for (const subscriber of subscribers.reverse()) {
      await subscriber.app.close();
    }
    await publisher.app.close();
  }
}

async function registrationCodec() {
  applyRcDecorators();
  RcDecoratedRequestHandler.requests = [];
  RcDecoratedSendHandler.messages = [];
  RcManualRequestHandler.requests = [];
  RcManualSendHandler.messages = [];
  RcFilterOrder.events = [];
  const apiEndpoint = uniqueEndpoint('rc-api');
  const nestjs = loadNest();
  await runRegistrationCodecAutoDiscovery(nestjs);
  await runRegistrationCodecProtobuf(nestjs);
  await runRegistrationCodecMessagePack(nestjs);
  const { app } = await startApp(
    nestModule('RegistrationCodecModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .codecs().addJson()
          .options({ filters: [RcFirstFilter, RcSecondFilter] })
          .addClientServerChannel('rc.api')
            .enableServer(apiEndpoint)
            .enableClient(apiEndpoint)
            .addHandlerGroup('rc-decorated')
            .addRequestHandler('rc.echo.manual', RcManualRequestHandler)
            .addSendHandler('rc.audit.manual', RcManualSendHandler)
          .build())
      ],
      providers: [
        RcDecoratedRequestHandler,
        RcDecoratedSendHandler,
        RcManualRequestHandler,
        RcManualSendHandler,
        RcFirstFilter,
        RcSecondFilter
      ]
    })
  );

  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const reply = await client
      .requestToChannel('rc.api', { id: 21, codec: 'json', ok: true })
      .packetName('rc.echo.manual')
      .submit();
    assert.equal(reply.id, 21);
    assert.equal(reply.codec, 'json');
    assert.equal(reply.ok, true);
    assert.equal(reply.contentType, 'application/json');
    assert.equal(RcManualRequestHandler.requests.length, 1);
    assert.equal(RcManualRequestHandler.requests[0].contentType, 'application/json');
    await client
      .sendToChannel('rc.api', { id: 22, codec: 'json', ok: true })
      .packetName('rc.audit.manual')
      .submit();
    await waitFor(() => RcManualSendHandler.messages.some((message) =>
      message.id === 22 &&
      message.contentType === 'application/json'));
    marker('RC-B1');
    marker('RC-A3');

    const decoratedReply = await client
      .requestToChannel('rc.api', { id: 23, codec: 'json', ok: true })
      .packetName('rc.echo.decorated')
      .submit();
    assert.equal(decoratedReply.id, 23);
    assert.equal(decoratedReply.codec, 'json');
    assert.equal(decoratedReply.ok, true);
    assert.equal(RcDecoratedRequestHandler.requests.length, 1);
    await client
      .sendToChannel('rc.api', { id: 24, codec: 'json', ok: true })
      .packetName('rc.audit.decorated')
      .submit();
    await waitFor(() => RcDecoratedSendHandler.messages.some((message) => message.id === 24));
    marker('RC-A2');

    RcFilterOrder.events = [];
    const filteredReply = await client
      .requestToChannel('rc.api', { id: 25, codec: 'json', ok: true })
      .packetName('rc.echo.manual')
      .submit();
    assert.equal(filteredReply.id, 25);
    assert.deepEqual(RcFilterOrder.events, ['first:before', 'second:before', 'handler', 'second:after', 'first:after']);
    marker('RC-A5');

    await assertInvalidRegistration(nestjs);
    marker('RC-A6');
    selfCheck('RC-JSON-REQUEST');
  } finally {
    await app.close();
  }
}

async function runRegistrationCodecProtobuf(nestjs) {
  RcCodecHandler.calls = [];
  RcCodecSendHandler.messages = [];
  const endpoint = uniqueEndpoint('rc-protobuf');
  const protobuf = loadFrameworkProtobuf();
  const { app } = await startApp(
    nestModule('RegistrationCodecProtobufModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .codecs().use(protobuf.zlinkProtobufCodec())
          .addClientServerChannel('rc.protobuf')
            .enableServer(endpoint)
            .enableClient(endpoint)
            .addRequestHandler('rc.codec.protobuf', RcCodecHandler)
            .addSendHandler('rc.codec.protobuf.audit', RcCodecSendHandler)
          .build())
      ],
      providers: [RcCodecHandler, RcCodecSendHandler]
    })
  );

  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const reply = await client
      .requestToChannel('rc.protobuf', { id: 31, codec: 'protobuf', ok: true })
      .packetName('rc.codec.protobuf')
      .submit();
    assert.deepEqual(reply, { id: 31, codec: 'protobuf', ok: true, contentType: protobuf.ZLINK_PROTOBUF_CONTENT_TYPE });
    assert.deepEqual(RcCodecHandler.calls, [{
      id: 31,
      codec: 'protobuf',
      contentType: protobuf.ZLINK_PROTOBUF_CONTENT_TYPE
    }]);
    await client
      .sendToChannel('rc.protobuf', { id: 33, codec: 'protobuf', ok: true })
      .packetName('rc.codec.protobuf.audit')
      .submit();
    await waitFor(() => RcCodecSendHandler.messages.some((message) =>
      message.id === 33 &&
      message.codec === 'protobuf' &&
      message.contentType === protobuf.ZLINK_PROTOBUF_CONTENT_TYPE));
    marker('RC-B2');
  } finally {
    await app.close();
  }
}

async function runRegistrationCodecMessagePack(nestjs) {
  RcCodecHandler.calls = [];
  RcCodecSendHandler.messages = [];
  const endpoint = uniqueEndpoint('rc-msgpack');
  const msgpack = loadFrameworkMessagePack();
  const { app } = await startApp(
    nestModule('RegistrationCodecMessagePackModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .codecs().use(msgpack.zlinkMessagePackCodec())
          .addClientServerChannel('rc.msgpack')
            .enableServer(endpoint)
            .enableClient(endpoint)
            .addRequestHandler('rc.codec.msgpack', RcCodecHandler)
            .addSendHandler('rc.codec.msgpack.audit', RcCodecSendHandler)
          .build())
      ],
      providers: [RcCodecHandler, RcCodecSendHandler]
    })
  );

  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const reply = await client
      .requestToChannel('rc.msgpack', { id: 32, codec: 'messagepack', ok: true })
      .packetName('rc.codec.msgpack')
      .submit();
    assert.deepEqual(reply, { id: 32, codec: 'messagepack', ok: true, contentType: msgpack.ZLINK_MESSAGEPACK_CONTENT_TYPE });
    assert.deepEqual(RcCodecHandler.calls, [{
      id: 32,
      codec: 'messagepack',
      contentType: msgpack.ZLINK_MESSAGEPACK_CONTENT_TYPE
    }]);
    await client
      .sendToChannel('rc.msgpack', { id: 34, codec: 'messagepack', ok: true })
      .packetName('rc.codec.msgpack.audit')
      .submit();
    await waitFor(() => RcCodecSendHandler.messages.some((message) =>
      message.id === 34 &&
      message.codec === 'messagepack' &&
      message.contentType === msgpack.ZLINK_MESSAGEPACK_CONTENT_TYPE));
    marker('RC-B3');
  } finally {
    await app.close();
  }
}

async function monitoring() {
  const logFile = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'zlink-node-mon-')), 'flow.log');
  const apiEndpoint = uniqueEndpoint('mon-api');
  const nestjs = loadNest();
  const framework = loadFramework();
  const builder = nestjs.zlinkFramework();
  builder.configureDispatch()
    .messageFlow(framework.ZLinkMessageFlowLogMode.Verbose)
    .traceLogFile(logFile);
  builder.addClientServerChannel('mon.api')
    .enableServer(apiEndpoint)
    .enableClient(apiEndpoint)
    .addRequestHandler('mon.echo', EchoHandler);
  const { app } = await startApp(
    nestModule('MonitoringModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(builder.build())
      ],
      providers: [EchoHandler]
    })
  );

  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    await client.requestToChannel('mon.api', { ok: true }).packetName('mon.echo').submit();
    await waitFor(() => fs.existsSync(logFile) && fs.readFileSync(logFile, 'utf8').includes('mon.echo'));
    selfCheck('MON-DISPATCH-TRACE');
  } finally {
    await app.close();
  }
}

async function discoveryRegistryHa() {
  const nestjs = loadNest();
  const framework = loadFramework();
  await runDiscoveryRegistryHaSingle(nestjs, framework);
  await runDiscoveryRegistryHaEmbedded(nestjs, framework);
  await runDiscoveryRegistryHaEmbeddedMixedCluster(nestjs, framework);
  await runDiscoveryRegistryHaPeerTwo(nestjs, framework);
  await runDiscoveryRegistryHaPeerThree(nestjs, framework);
  await runDiscoveryRegistryHaLateStart(nestjs, framework);
  await runDiscoveryRegistryHaRegistryStop(nestjs, framework);
  await runDiscoveryRegistryHaPeerFlapping(nestjs, framework);
  await runDiscoveryRegistryHaKilledRegistry(nestjs, framework);
  await runDiscoveryRegistryHaRegistryRecovery(nestjs, framework);
  await runDiscoveryRegistryHaFullOutageRecovery(nestjs, framework);
}

async function runDiscoveryRegistryHaEmbeddedMixedCluster(nestjs, framework) {
  DRHandler.requests = [];
  const reg1Pub = await reserveTcpEndpoint();
  const reg1Router = await reserveTcpEndpoint();
  const reg2Pub = await reserveTcpEndpoint();
  const reg2Router = await reserveTcpEndpoint();
  const reg3Pub = await reserveTcpEndpoint();
  const reg3Router = await reserveTcpEndpoint();
  const providerEndpoint = await reserveTcpEndpoint();
  const handler = createDRHandler('dr-mixed-embedded-provider');
  const apps = [];

  try {
    const embedded = await startApp(
      nestModule('DiscoveryRegistryHaEmbeddedMixedReg1Module', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: reg1Pub,
            routerEndpoint: reg1Router,
            registryId: 82,
            broadcastIntervalMs: 100,
            peers: [reg2Pub, reg3Pub]
          }),
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(reg1Router)
            .addClientServerChannel('dr.mixed')
              .enableServer(providerEndpoint)
              .routingId('dr-mixed-embedded-provider')
              .addRequestHandler('dr.probe', handler)
            .build())
        ],
        providers: [handler]
      })
    );
    apps.push(embedded.app);
    const reg2 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaEmbeddedMixedReg2Module', {
      pubEndpoint: reg2Pub,
      routerEndpoint: reg2Router,
      registryId: 83,
      broadcastIntervalMs: 100,
      peers: [reg1Pub, reg3Pub]
    });
    const reg3 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaEmbeddedMixedReg3Module', {
      pubEndpoint: reg3Pub,
      routerEndpoint: reg3Router,
      registryId: 84,
      broadcastIntervalMs: 100,
      peers: [reg1Pub, reg2Pub]
    });
    apps.push(reg2.app, reg3.app);

    const reg1Query = embedded.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    const reg2Query = reg2.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    const reg3Query = reg3.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryPeerConnection(reg1Query, 2);
    await waitForRegistryPeerConnection(reg2Query, 2);
    await waitForRegistryPeerConnection(reg3Query, 2);
    await waitForDiscoveryTopologySnapshot(reg1Query, framework, 'dr.mixed', [providerEndpoint]);
    await waitForDiscoveryMemberPeers(reg2Query, framework, 'dr.mixed', [providerEndpoint]);
    await waitForDiscoveryMemberPeers(reg3Query, framework, 'dr.mixed', [providerEndpoint]);

    const consumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaEmbeddedMixedConsumerModule', reg3Router, 'dr.mixed');
    apps.push(consumer.app);
    await assertDiscoveryMessaging(consumer.app, nestjs, 'dr.mixed', 'dr-d3-mixed', ['dr-mixed-embedded-provider']);
    marker('DR-D3');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function runDiscoveryRegistryHaEmbedded(nestjs, framework) {
  DRHandler.requests = [];
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const providerEndpoint = await reserveTcpEndpoint();
  const handler = createDRHandler('dr-embedded-provider');
  const apps = [];

  try {
    const embedded = await startApp(
      nestModule('DiscoveryRegistryHaEmbeddedModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 81,
            broadcastIntervalMs: 100
          }),
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(registryRouterEndpoint)
            .addClientServerChannel('dr.embedded')
              .enableServer(providerEndpoint)
              .routingId('dr-embedded-provider')
              .addRequestHandler('dr.probe', handler)
            .build())
        ],
        providers: [handler]
      })
    );
    apps.push(embedded.app);

    const query = embedded.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForDiscoveryEndpoints(query, framework, 'dr.embedded', [providerEndpoint]);
    const consumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaEmbeddedConsumerModule', registryRouterEndpoint, 'dr.embedded');
    apps.push(consumer.app);
    await assertDiscoveryMessaging(consumer.app, nestjs, 'dr.embedded', 'dr-d1-embedded', ['dr-embedded-provider']);
    marker('DR-D1');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function runDiscoveryRegistryHaSingle(nestjs, framework) {
  DRHandler.requests = [];
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const providerAEndpoint = await reserveTcpEndpoint();
  const providerBEndpoint = await reserveTcpEndpoint();
  const apps = [];

  try {
    const registry = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaSingleRegistryModule', {
      pubEndpoint: registryPubEndpoint,
      routerEndpoint: registryRouterEndpoint,
      registryId: 61
    });
    apps.push(registry.app);
    await startDiscoveryProvider(nestjs, registryRouterEndpoint, 'dr.single', providerAEndpoint, 'dr-a', apps);
    await startDiscoveryProvider(nestjs, registryRouterEndpoint, 'dr.single', providerBEndpoint, 'dr-b', apps);
    const consumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaSingleConsumerModule', registryRouterEndpoint, 'dr.single');
    apps.push(consumer.app);

    const query = registry.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForDiscoveryEndpoints(query, framework, 'dr.single', [providerAEndpoint, providerBEndpoint]);
    await assertDiscoveryMessaging(consumer.app, nestjs, 'dr.single', 'dr-a1-baseline', ['dr-a', 'dr-b']);
    marker('DR-A1');
    marker('DR-D2');

    const remoteQuery = await startApp(
      nestModule('DiscoveryRegistryHaRemoteQueryModule', {
        imports: [
          nestjs.ZLinkRegistryQueryClientModule.forRoot({
            endpoint: registryRouterEndpoint
          })
        ]
      })
    );
    apps.push(remoteQuery.app);
    const queryClient = remoteQuery.app.get(nestjs.ZLINK_REGISTRY_QUERY_CLIENT, { strict: false });
    const localTopology = await waitForDiscoveryTopologySnapshot(query, framework, 'dr.single', [providerAEndpoint, providerBEndpoint]);
    const remoteTopology = await waitForDiscoveryTopologySnapshot(queryClient, framework, 'dr.single', [providerAEndpoint, providerBEndpoint]);
    assert.deepEqual(normalizeDiscoveryTopology(remoteTopology), normalizeDiscoveryTopology(localTopology));
    marker('DR-D4');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function runDiscoveryRegistryHaPeerTwo(nestjs, framework) {
  DRHandler.requests = [];
  const reg1Pub = await reserveTcpEndpoint();
  const reg1Router = await reserveTcpEndpoint();
  const reg2Pub = await reserveTcpEndpoint();
  const reg2Router = await reserveTcpEndpoint();
  const providerEndpoint = await reserveTcpEndpoint();
  const apps = [];

  try {
    const reg1 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaPeerTwoReg1Module', {
      pubEndpoint: reg1Pub,
      routerEndpoint: reg1Router,
      registryId: 62,
      broadcastIntervalMs: 100,
      peers: [reg2Pub]
    });
    const reg2 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaPeerTwoReg2Module', {
      pubEndpoint: reg2Pub,
      routerEndpoint: reg2Router,
      registryId: 63,
      broadcastIntervalMs: 100,
      peers: [reg1Pub]
    });
    apps.push(reg1.app, reg2.app);
    await startDiscoveryProvider(nestjs, reg1Router, 'dr.peer2', providerEndpoint, 'dr-peer2-provider', apps);
    const consumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaPeerTwoConsumerModule', reg2Router, 'dr.peer2');
    apps.push(consumer.app);

    const query = reg2.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryPeerConnection(reg1.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false }), 1);
    await waitForRegistryPeerConnection(query, 1);
    await waitForDiscoveryMemberPeers(query, framework, 'dr.peer2', [providerEndpoint]);
    await assertDiscoveryMessaging(consumer.app, nestjs, 'dr.peer2', 'dr-a2-peer2', ['dr-peer2-provider']);
    marker('DR-A2');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function runDiscoveryRegistryHaPeerThree(nestjs, framework) {
  DRHandler.requests = [];
  const regs = [
    { pub: await reserveTcpEndpoint(), router: await reserveTcpEndpoint(), id: 64 },
    { pub: await reserveTcpEndpoint(), router: await reserveTcpEndpoint(), id: 65 },
    { pub: await reserveTcpEndpoint(), router: await reserveTcpEndpoint(), id: 66 }
  ];
  const providerAEndpoint = await reserveTcpEndpoint();
  const providerBEndpoint = await reserveTcpEndpoint();
  const apps = [];

  try {
    for (const reg of regs) {
      const started = await startDiscoveryRegistry(nestjs, `DiscoveryRegistryHaPeerThreeReg${reg.id}Module`, {
        pubEndpoint: reg.pub,
        routerEndpoint: reg.router,
        registryId: reg.id,
        broadcastIntervalMs: 100,
        peers: regs.filter((peer) => peer !== reg).map((peer) => peer.pub)
      });
      apps.push(started.app);
    }
    await startDiscoveryProvider(nestjs, regs[0].router, 'dr.peer3', providerAEndpoint, 'dr-peer3-a', apps);
    await startDiscoveryProvider(nestjs, regs[2].router, 'dr.peer3', providerBEndpoint, 'dr-peer3-b', apps);

    for (const regApp of apps.slice(0, 3)) {
      const query = regApp.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
      await waitForRegistryPeerConnection(query, 2);
      await waitForDiscoveryMemberPeers(query, framework, 'dr.peer3', [providerAEndpoint, providerBEndpoint]);
    }

    for (const [index, reg] of regs.entries()) {
      const consumer = await startDiscoveryConsumer(nestjs, `DiscoveryRegistryHaPeerThreeConsumer${index + 1}Module`, reg.router, 'dr.peer3');
      apps.push(consumer.app);
      await assertDiscoveryMessaging(consumer.app, nestjs, 'dr.peer3', `dr-a3-peer3-reg${index + 1}`, ['dr-peer3-a', 'dr-peer3-b']);
    }
    marker('DR-A3');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function runDiscoveryRegistryHaLateStart(nestjs, framework) {
  DRHandler.requests = [];
  const reg1Pub = await reserveTcpEndpoint();
  const reg1Router = await reserveTcpEndpoint();
  const reg2Pub = await reserveTcpEndpoint();
  const reg2Router = await reserveTcpEndpoint();
  const reg3Pub = await reserveTcpEndpoint();
  const reg3Router = await reserveTcpEndpoint();
  const providerEndpoint = await reserveTcpEndpoint();
  const apps = [];
  let reg2;
  let reg3;

  try {
    const reg1 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaLateStartReg1Module', {
      pubEndpoint: reg1Pub,
      routerEndpoint: reg1Router,
      registryId: 67,
      broadcastIntervalMs: 100,
      peers: [reg2Pub, reg3Pub]
    });
    apps.push(reg1.app);
    await startDiscoveryProvider(nestjs, reg1Router, 'dr.late', providerEndpoint, 'dr-late-provider', apps);

    const activeConsumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaLateStartActiveConsumerModule', reg1Router, 'dr.late');
    apps.push(activeConsumer.app);
    await waitForDiscoveryTopologySnapshot(reg1.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false }), framework, 'dr.late', [providerEndpoint]);
    await assertDiscoveryMessaging(activeConsumer.app, nestjs, 'dr.late', 'dr-b1-before-late-registry', ['dr-late-provider']);

    const lateConsumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaLateStartConsumerModule', reg2Router, 'dr.late');
    apps.push(lateConsumer.app);
    reg2 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaLateStartReg2Module', {
      pubEndpoint: reg2Pub,
      routerEndpoint: reg2Router,
      registryId: 68,
      broadcastIntervalMs: 100,
      peers: [reg1Pub, reg3Pub]
    });
    apps.push(reg2.app);

    const reg1Query = reg1.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    const reg2Query = reg2.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryPeerConnection(reg1Query, 1);
    await waitForRegistryPeerConnection(reg2Query, 1);
    await waitForDiscoveryMemberPeers(reg2Query, framework, 'dr.late', [providerEndpoint]);
    await assertDiscoveryMessaging(lateConsumer.app, nestjs, 'dr.late', 'dr-b1-after-late-registry', ['dr-late-provider']);

    const laterConsumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaLateStartConsumer3Module', reg3Router, 'dr.late');
    apps.push(laterConsumer.app);
    reg3 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaLateStartReg3Module', {
      pubEndpoint: reg3Pub,
      routerEndpoint: reg3Router,
      registryId: 71,
      broadcastIntervalMs: 100,
      peers: [reg1Pub, reg2Pub]
    });
    apps.push(reg3.app);

    const reg3Query = reg3.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryPeerConnection(reg1Query, 2);
    await waitForRegistryPeerConnection(reg2Query, 2);
    await waitForRegistryPeerConnection(reg3Query, 2);
    await waitForDiscoveryMemberPeers(reg3Query, framework, 'dr.late', [providerEndpoint]);
    await assertDiscoveryMessaging(laterConsumer.app, nestjs, 'dr.late', 'dr-b1-after-third-registry', ['dr-late-provider']);
    marker('DR-B1');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function runDiscoveryRegistryHaRegistryStop(nestjs, framework) {
  DRHandler.requests = [];
  const reg1Pub = await reserveTcpEndpoint();
  const reg1Router = await reserveTcpEndpoint();
  const reg2Pub = await reserveTcpEndpoint();
  const reg2Router = await reserveTcpEndpoint();
  const providerEndpoint = await reserveTcpEndpoint();
  const apps = [];
  let reg2;

  try {
    const reg1 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaStopReg1Module', {
      pubEndpoint: reg1Pub,
      routerEndpoint: reg1Router,
      registryId: 69,
      broadcastIntervalMs: 100,
      peers: [reg2Pub]
    });
    reg2 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaStopReg2Module', {
      pubEndpoint: reg2Pub,
      routerEndpoint: reg2Router,
      registryId: 70,
      broadcastIntervalMs: 100,
      peers: [reg1Pub]
    });
    apps.push(reg1.app, reg2.app);
    await startDiscoveryProviderWithRegistries(
      nestjs,
      [reg1Router, reg2Router],
      'dr.stop',
      providerEndpoint,
      'dr-stop-provider',
      apps
    );
    const consumer = await startDiscoveryConsumerWithRegistries(
      nestjs,
      'DiscoveryRegistryHaStopConsumerModule',
      [reg1Router, reg2Router],
      'dr.stop'
    );
    apps.push(consumer.app);

    const reg1Query = reg1.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    const reg2Query = reg2.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryPeerConnection(reg1Query, 1);
    await waitForRegistryPeerConnection(reg2Query, 1);
    await waitForDiscoveryTopologySnapshot(reg1Query, framework, 'dr.stop', [providerEndpoint]);
    await waitForDiscoveryTopologySnapshot(reg2Query, framework, 'dr.stop', [providerEndpoint]);
    await assertDiscoveryMessaging(consumer.app, nestjs, 'dr.stop', 'dr-b2-before-stop', ['dr-stop-provider']);

    await reg2.app.close();
    apps.splice(apps.indexOf(reg2.app), 1);
    await waitForDiscoveryTopologySnapshot(reg1Query, framework, 'dr.stop', [providerEndpoint]);
    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    for (let i = 0; i < 10; i += 1) {
      const requestId = `dr-b2-after-stop-${i}`;
      const reply = await client
        .requestToChannel('dr.stop', { requestId })
        .packetName('dr.probe')
        .timeout(1000)
        .submit();
      assert.deepEqual(reply, { requestId, handledBy: 'dr-stop-provider' });
    }
    const freshConsumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaStopFreshReg1ConsumerModule', reg1Router, 'dr.stop');
    apps.push(freshConsumer.app);
    await assertDiscoveryMessaging(freshConsumer.app, nestjs, 'dr.stop', 'dr-b2-fresh-reg1', ['dr-stop-provider']);
    marker('DR-B2');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function runDiscoveryRegistryHaPeerFlapping(nestjs, framework) {
  DRHandler.requests = [];
  const reg1Pub = await reserveTcpEndpoint();
  const reg1Router = await reserveTcpEndpoint();
  const reg2Pub = await reserveTcpEndpoint();
  const reg2Router = await reserveTcpEndpoint();
  const providerEndpoint = await reserveTcpEndpoint();
  const apps = [];
  let reg2;

  try {
    const reg1 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaFlapReg1Module', {
      pubEndpoint: reg1Pub,
      routerEndpoint: reg1Router,
      registryId: 72,
      broadcastIntervalMs: 100,
      peers: [reg2Pub]
    });
    apps.push(reg1.app);
    reg2 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaFlapReg2Module', {
      pubEndpoint: reg2Pub,
      routerEndpoint: reg2Router,
      registryId: 73,
      broadcastIntervalMs: 100,
      peers: [reg1Pub]
    });
    apps.push(reg2.app);
    await startDiscoveryProvider(nestjs, reg1Router, 'dr.flap', providerEndpoint, 'dr-flap-provider', apps);
    const stableConsumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaFlapStableConsumerModule', reg1Router, 'dr.flap');
    apps.push(stableConsumer.app);

    const reg1Query = reg1.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForDiscoveryTopologySnapshot(reg1Query, framework, 'dr.flap', [providerEndpoint]);
    await assertDiscoveryMessaging(stableConsumer.app, nestjs, 'dr.flap', 'dr-b3-before-flap', ['dr-flap-provider']);

    for (let cycle = 1; cycle <= 3; cycle += 1) {
      await reg2.app.close();
      apps.splice(apps.indexOf(reg2.app), 1);
      await waitForRegistryPeerConnectionExact(reg1Query, 0);
      await waitForDiscoveryTopologySnapshot(reg1Query, framework, 'dr.flap', [providerEndpoint]);
      await waitForDiscoveryMemberPeers(reg1Query, framework, 'dr.flap', [providerEndpoint]);
      await assertDiscoveryMessaging(stableConsumer.app, nestjs, 'dr.flap', `dr-b3-during-flap-${cycle}`, ['dr-flap-provider']);

      reg2 = await startDiscoveryRegistry(nestjs, `DiscoveryRegistryHaFlapReg2Cycle${cycle}Module`, {
        pubEndpoint: reg2Pub,
        routerEndpoint: reg2Router,
        registryId: 73,
        broadcastIntervalMs: 100,
        peers: [reg1Pub]
      });
      apps.push(reg2.app);
      const reg2Query = reg2.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
      await waitForRegistryPeerConnectionExact(reg1Query, 1);
      await waitForRegistryPeerConnectionExact(reg2Query, 1);
      await waitForDiscoveryMemberPeers(reg2Query, framework, 'dr.flap', [providerEndpoint]);
      const recoveredConsumer = await startDiscoveryConsumer(nestjs, `DiscoveryRegistryHaFlapRecoveredConsumer${cycle}Module`, reg2Router, 'dr.flap');
      apps.push(recoveredConsumer.app);
      await assertDiscoveryMessaging(recoveredConsumer.app, nestjs, 'dr.flap', `dr-b3-after-flap-${cycle}`, ['dr-flap-provider']);
      await recoveredConsumer.app.close();
      apps.splice(apps.indexOf(recoveredConsumer.app), 1);
    }

    marker('DR-B3');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function runDiscoveryRegistryHaKilledRegistry(nestjs, framework) {
  DRHandler.requests = [];
  const reg1Pub = await reserveTcpEndpoint();
  const reg1Router = await reserveTcpEndpoint();
  const reg2Pub = await reserveTcpEndpoint();
  const reg2Router = await reserveTcpEndpoint();
  const providerEndpoint = await reserveTcpEndpoint();
  const apps = [];
  const children = [];

  try {
    const reg1 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaKillReg1Module', {
      pubEndpoint: reg1Pub,
      routerEndpoint: reg1Router,
      registryId: 79,
      broadcastIntervalMs: 100,
      peers: [reg2Pub]
    });
    apps.push(reg1.app);
    const reg2 = await startDiscoveryRegistryChild('DiscoveryRegistryHaKillReg2Module', {
      pubEndpoint: reg2Pub,
      routerEndpoint: reg2Router,
      registryId: 80,
      broadcastIntervalMs: 100,
      peers: [reg1Pub]
    });
    children.push(reg2);
    await startDiscoveryProviderWithRegistries(
      nestjs,
      [reg1Router, reg2Router],
      'dr.kill',
      providerEndpoint,
      'dr-kill-provider',
      apps
    );
    const consumer = await startDiscoveryConsumerWithRegistries(
      nestjs,
      'DiscoveryRegistryHaKillConsumerModule',
      [reg1Router, reg2Router],
      'dr.kill'
    );
    apps.push(consumer.app);

    const reg2QueryApp = await startDiscoveryQueryClient(nestjs, 'DiscoveryRegistryHaKillReg2QueryModule', reg2Router);
    apps.push(reg2QueryApp.app);
    const reg1Query = reg1.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    const reg2Query = reg2QueryApp.app.get(nestjs.ZLINK_REGISTRY_QUERY_CLIENT, { strict: false });
    await waitForDiscoveryMemberPeers(reg1Query, framework, 'dr.kill', [providerEndpoint]);
    await waitForDiscoveryTopologySnapshot(reg2Query, framework, 'dr.kill', [providerEndpoint]);
    await assertDiscoveryMessaging(consumer.app, nestjs, 'dr.kill', 'dr-c1-before-kill', ['dr-kill-provider']);

    await stopDiscoveryRegistryChild(reg2, 'SIGKILL');
    children.splice(children.indexOf(reg2), 1);
    await waitForDiscoveryMemberPeers(reg1Query, framework, 'dr.kill', [providerEndpoint]);
    await assertDiscoveryMessaging(consumer.app, nestjs, 'dr.kill', 'dr-c1-after-kill', ['dr-kill-provider']);
    await assert.rejects(
      () => withTimeout(
        () => reg2Query.topology({ channelName: 'dr.kill' }),
        2000,
        'DR-C1 dead registry query did not fail in bounded time.'
      ),
      /registry_query_topology failed|Resource temporarily unavailable|timed out|bounded time/i
    );
    marker('DR-C1');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
    for (const child of children.reverse()) {
      await stopDiscoveryRegistryChild(child, 'SIGKILL');
    }
  }
}

async function runDiscoveryRegistryHaRegistryRecovery(nestjs, framework) {
  DRHandler.requests = [];
  const reg1Pub = await reserveTcpEndpoint();
  const reg1Router = await reserveTcpEndpoint();
  const reg2Pub = await reserveTcpEndpoint();
  const reg2Router = await reserveTcpEndpoint();
  const providerEndpoint = await reserveTcpEndpoint();
  const apps = [];
  let reg2;

  try {
    const reg1 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaRecoveryReg1Module', {
      pubEndpoint: reg1Pub,
      routerEndpoint: reg1Router,
      registryId: 74,
      broadcastIntervalMs: 100,
      peers: [reg2Pub]
    });
    apps.push(reg1.app);
    reg2 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaRecoveryReg2Module', {
      pubEndpoint: reg2Pub,
      routerEndpoint: reg2Router,
      registryId: 75,
      broadcastIntervalMs: 100,
      peers: [reg1Pub]
    });
    apps.push(reg2.app);
    await startDiscoveryProvider(nestjs, reg1Router, 'dr.recovery', providerEndpoint, 'dr-recovery-provider', apps);

    const reg1Query = reg1.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    let reg2Query = reg2.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryPeerConnectionExact(reg1Query, 1);
    await waitForRegistryPeerConnectionExact(reg2Query, 1);
    await waitForDiscoveryMemberPeers(reg2Query, framework, 'dr.recovery', [providerEndpoint]);
    const beforeConsumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaRecoveryBeforeConsumerModule', reg2Router, 'dr.recovery');
    apps.push(beforeConsumer.app);
    await assertDiscoveryMessaging(beforeConsumer.app, nestjs, 'dr.recovery', 'dr-c2-before-restart', ['dr-recovery-provider']);
    await beforeConsumer.app.close();
    apps.splice(apps.indexOf(beforeConsumer.app), 1);

    await reg2.app.close();
    apps.splice(apps.indexOf(reg2.app), 1);
    await waitForRegistryPeerConnectionExact(reg1Query, 0);
    await waitForDiscoveryTopologySnapshot(reg1Query, framework, 'dr.recovery', [providerEndpoint]);

    reg2 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaRecoveryReg2RestartedModule', {
      pubEndpoint: reg2Pub,
      routerEndpoint: reg2Router,
      registryId: 75,
      broadcastIntervalMs: 100,
      peers: [reg1Pub]
    });
    apps.push(reg2.app);
    reg2Query = reg2.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryPeerConnectionExact(reg1Query, 1);
    await waitForRegistryPeerConnectionExact(reg2Query, 1);
    await waitForDiscoveryMemberPeers(reg2Query, framework, 'dr.recovery', [providerEndpoint]);
    const recoveredConsumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaRecoveryConsumerModule', reg2Router, 'dr.recovery');
    apps.push(recoveredConsumer.app);
    await assertDiscoveryMessaging(recoveredConsumer.app, nestjs, 'dr.recovery', 'dr-c2-after-restart', ['dr-recovery-provider']);
    marker('DR-C2');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function runDiscoveryRegistryHaFullOutageRecovery(nestjs, framework) {
  DRHandler.requests = [];
  const reg1Pub = await reserveTcpEndpoint();
  const reg1Router = await reserveTcpEndpoint();
  const reg2Pub = await reserveTcpEndpoint();
  const reg2Router = await reserveTcpEndpoint();
  const providerEndpoint = await reserveTcpEndpoint();
  const apps = [];
  let reg1;
  let reg2;
  let provider;

  try {
    reg1 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaFullOutageReg1Module', {
      pubEndpoint: reg1Pub,
      routerEndpoint: reg1Router,
      registryId: 76,
      broadcastIntervalMs: 100,
      peers: [reg2Pub]
    });
    reg2 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaFullOutageReg2Module', {
      pubEndpoint: reg2Pub,
      routerEndpoint: reg2Router,
      registryId: 77,
      broadcastIntervalMs: 100,
      peers: [reg1Pub]
    });
    apps.push(reg1.app, reg2.app);
    provider = await startDiscoveryProviderWithRegistries(
      nestjs,
      [reg1Router, reg2Router],
      'dr.full',
      providerEndpoint,
      'dr-full-provider',
      apps
    );
    const consumer = await startDiscoveryConsumerWithRegistries(
      nestjs,
      'DiscoveryRegistryHaFullOutageConsumerModule',
      [reg1Router, reg2Router],
      'dr.full'
    );
    apps.push(consumer.app);

    let reg1Query = reg1.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    let reg2Query = reg2.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryPeerConnectionExact(reg1Query, 1);
    await waitForRegistryPeerConnectionExact(reg2Query, 1);
    await waitForDiscoveryTopologySnapshot(reg1Query, framework, 'dr.full', [providerEndpoint]);
    await waitForDiscoveryTopologySnapshot(reg2Query, framework, 'dr.full', [providerEndpoint]);
    await assertDiscoveryMessaging(consumer.app, nestjs, 'dr.full', 'dr-c3-before-outage', ['dr-full-provider']);

    await reg2.app.close();
    apps.splice(apps.indexOf(reg2.app), 1);
    await reg1.app.close();
    apps.splice(apps.indexOf(reg1.app), 1);
    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const duringOutage = await client
      .requestToChannel('dr.full', { requestId: 'dr-c3-during-outage' })
      .packetName('dr.probe')
      .timeout(1000)
      .submit();
    assert.deepEqual(duringOutage, { requestId: 'dr-c3-during-outage', handledBy: 'dr-full-provider' });

    await provider.app.close();
    apps.splice(apps.indexOf(provider.app), 1);
    reg1 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaFullOutageReg1RecoveredModule', {
      pubEndpoint: reg1Pub,
      routerEndpoint: reg1Router,
      registryId: 76,
      broadcastIntervalMs: 100,
      peers: [reg2Pub]
    });
    reg2 = await startDiscoveryRegistry(nestjs, 'DiscoveryRegistryHaFullOutageReg2RecoveredModule', {
      pubEndpoint: reg2Pub,
      routerEndpoint: reg2Router,
      registryId: 77,
      broadcastIntervalMs: 100,
      peers: [reg1Pub]
    });
    apps.push(reg1.app, reg2.app);
    reg1Query = reg1.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    reg2Query = reg2.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryPeerConnectionExact(reg1Query, 1);
    await waitForRegistryPeerConnectionExact(reg2Query, 1);
    provider = await startDiscoveryProviderWithRegistries(
      nestjs,
      [reg1Router, reg2Router],
      'dr.full',
      providerEndpoint,
      'dr-full-provider',
      apps
    );
    const reg1Ready = await waitForDiscoveryExactReadyTopology(reg1Query, framework, 'dr.full', [providerEndpoint]);
    const reg2Ready = await waitForDiscoveryExactReadyTopology(reg2Query, framework, 'dr.full', [providerEndpoint]);
    assert.deepEqual(reg2Ready, reg1Ready);
    const reg1Consumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaFullOutageReg1ConsumerModule', reg1Router, 'dr.full');
    apps.push(reg1Consumer.app);
    await assertDiscoveryMessaging(reg1Consumer.app, nestjs, 'dr.full', 'dr-c3-after-reg1-recovery', ['dr-full-provider']);
    const reg2Consumer = await startDiscoveryConsumer(nestjs, 'DiscoveryRegistryHaFullOutageReg2ConsumerModule', reg2Router, 'dr.full');
    apps.push(reg2Consumer.app);
    await assertDiscoveryMessaging(reg2Consumer.app, nestjs, 'dr.full', 'dr-c3-after-reg2-recovery', ['dr-full-provider']);
    marker('DR-C3');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startDiscoveryRegistry(nestjs, moduleName, options) {
  return startApp(
    nestModule(moduleName, {
      imports: [
        nestjs.ZLinkRegistryModule.forRoot(options)
      ]
    })
  );
}

async function registryChildMain(options) {
  const nestjs = loadNest();
  let started;
  let keepAlive;
  try {
    started = await startDiscoveryRegistry(nestjs, options.moduleName, options.registryOptions);
    keepAlive = setInterval(() => {}, 1000);
    if (typeof process.send === 'function') {
      process.send({ type: 'ready' });
    }
    await new Promise((resolve) => {
      const close = async () => {
        if (started !== undefined) {
          await started.app.close();
          started = undefined;
        }
        clearInterval(keepAlive);
        resolve();
      };
      process.once('SIGTERM', close);
      process.once('SIGINT', close);
    });
  } catch (error) {
    if (typeof process.send === 'function') {
      process.send({ type: 'error', message: error instanceof Error ? error.stack ?? error.message : String(error) });
    }
    process.exitCode = 1;
  }
}

async function resilienceProviderChildMain(options) {
  const nestjs = loadNest();
  let started;
  let keepAlive;
  try {
    const channelName = options.channelName ?? 'rl.inflight';
    const packetName = options.packetName ?? 'rl.inflight.probe';
    const providerId = options.providerId ?? 'provider-b';
    const handlerType = createRLChildProviderHandler(providerId, new Set(options.blockRequestIds ?? ['rl-b2-slow-crash']));
    started = await startApp(
      nestModule(options.moduleName ?? 'ResilienceProviderChildModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(options.registryRouterEndpoint)
            .addClientServerChannel(channelName)
              .enableServer(options.providerEndpoint)
              .routingId(options.routingId ?? providerId)
              .addRequestHandler(packetName, handlerType)
            .build())
        ],
        providers: [handlerType]
      })
    );
    keepAlive = setInterval(() => {}, 1000);
    if (typeof process.send === 'function') {
      process.send({ type: 'ready' });
    }
    await new Promise((resolve) => {
      const close = async () => {
        if (started !== undefined) {
          await started.app.close();
          started = undefined;
        }
        clearInterval(keepAlive);
        resolve();
      };
      process.once('SIGTERM', close);
      process.once('SIGINT', close);
    });
  } catch (error) {
    if (typeof process.send === 'function') {
      process.send({ type: 'error', message: error instanceof Error ? error.stack ?? error.message : String(error) });
    }
    process.exitCode = 1;
  }
}

async function startDiscoveryRegistryChild(moduleName, registryOptions) {
  const child = fork(__filename, [
    '__registry_child',
    JSON.stringify({ moduleName, registryOptions })
  ], {
    cwd: nodeRoot,
    stdio: ['ignore', 'pipe', 'pipe', 'ipc']
  });
  child.stdout?.pipe(process.stdout);
  child.stderr?.pipe(process.stderr);
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      child.kill('SIGKILL');
      reject(new Error(`Registry child ${moduleName} did not become ready.`));
    }, 5000);
    child.once('exit', (code, signal) => {
      clearTimeout(timeout);
      reject(new Error(`Registry child ${moduleName} exited before ready: code=${code} signal=${signal}`));
    });
    child.once('message', (message) => {
      clearTimeout(timeout);
      child.removeAllListeners('exit');
      if (message?.type === 'ready') {
        resolve();
        return;
      }
      reject(new Error(`Registry child ${moduleName} failed: ${message?.message ?? stringifyDiagnostic(message)}`));
    });
  });
  return child;
}

async function startResilienceInFlightProviderChild(options) {
  const child = fork(__filename, [
    '__rl_provider_child',
    JSON.stringify(options)
  ], {
    cwd: nodeRoot,
    stdio: ['ignore', 'pipe', 'pipe', 'ipc']
  });
  child.stdout?.pipe(process.stdout);
  child.stderr?.pipe(process.stderr);

  const handling = new Map();
  const handledRequestIds = new Set();
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      child.kill('SIGKILL');
      reject(new Error('RL-B2 provider child did not become ready.'));
    }, 5000);
    child.once('exit', (code, signal) => {
      clearTimeout(timeout);
      reject(new Error(`RL-B2 provider child exited before ready: code=${code} signal=${signal}`));
    });
    child.on('message', (message) => {
      if (message?.type === 'handling' && typeof message.requestId === 'string') {
        handledRequestIds.add(message.requestId);
        const pending = handling.get(message.requestId);
        if (pending !== undefined) {
          handling.delete(message.requestId);
          pending.resolve(message);
        }
        return;
      }
      if (message?.type === 'ready') {
        clearTimeout(timeout);
        child.removeAllListeners('exit');
        resolve();
        return;
      }
      if (message?.type === 'error') {
        clearTimeout(timeout);
        reject(new Error(`RL-B2 provider child failed: ${message.message ?? stringifyDiagnostic(message)}`));
      }
    });
  });

  return {
    child,
    waitForHandling(requestId) {
      if (handledRequestIds.has(requestId)) {
        return Promise.resolve();
      }
      return withTimeout(() => new Promise((resolve) => {
        handling.set(requestId, { resolve });
      }), 3000, `RL-B2 provider child did not start handling ${requestId}.`);
    }
  };
}

async function stopDiscoveryRegistryChild(child, signal = 'SIGTERM') {
  await stopChildProcess(child, signal);
}

async function stopChildProcess(child, signal = 'SIGTERM') {
  if (child.exitCode !== null || child.signalCode !== null) {
    return;
  }
  await new Promise((resolve) => {
    child.once('exit', resolve);
    child.kill(signal);
  });
}

async function startDiscoveryProvider(nestjs, registryRouterEndpoint, channelName, providerEndpoint, routingId, apps) {
  return startDiscoveryProviderWithRegistries(nestjs, [registryRouterEndpoint], channelName, providerEndpoint, routingId, apps);
}

async function startDiscoveryProviderWithRegistries(nestjs, registryRouterEndpoints, channelName, providerEndpoint, routingId, apps) {
  const handler = createDRHandler(routingId);
  const builder = nestjs.zlinkFramework().useDiscovery();
  for (const endpoint of registryRouterEndpoints) {
    builder.addRegistryEndpoint(endpoint);
  }
  const started = await startApp(
    nestModule(`DiscoveryRegistryHaProvider${routingId}Module`, {
      imports: [
        nestjs.ZLinkModule.forRoot(builder
          .addClientServerChannel(channelName)
            .enableServer(providerEndpoint)
            .routingId(routingId)
            .addRequestHandler('dr.probe', handler)
          .build())
      ],
      providers: [handler]
    })
  );
  apps.push(started.app);
  return started;
}

async function startDiscoveryConsumer(nestjs, moduleName, registryRouterEndpoint, channelName) {
  return startDiscoveryConsumerWithRegistries(nestjs, moduleName, [registryRouterEndpoint], channelName);
}

async function startDiscoveryQueryClient(nestjs, moduleName, endpoint) {
  return startApp(
    nestModule(moduleName, {
      imports: [
        nestjs.ZLinkRegistryQueryClientModule.forRoot({ endpoint })
      ]
    })
  );
}

async function startDiscoveryConsumerWithRegistries(nestjs, moduleName, registryRouterEndpoints, channelName) {
  const builder = nestjs.zlinkFramework().useDiscovery();
  for (const endpoint of registryRouterEndpoints) {
    builder.addRegistryEndpoint(endpoint);
  }
  return startApp(
    nestModule(moduleName, {
      imports: [
        nestjs.ZLinkModule.forRoot(builder
          .addClientServerChannel(channelName)
            .enableClient()
          .build())
      ]
    })
  );
}

async function waitForDiscoveryTopologySnapshot(query, framework, channelName, endpoints, timeoutMs = 7000) {
  const expected = new Set(endpoints);
  const deadline = Date.now() + timeoutMs;
  let lastTopology = [];
  while (Date.now() < deadline) {
    lastTopology = await query.topology({
      autoConnectType: framework.ZLinkAutoConnectType.ClientServer,
      serviceKind: framework.ZLinkServiceKind.Socket,
      serviceRole: framework.ZLinkServiceRole.Router,
      channelName
    });
    if ([...expected].every((endpoint) => lastTopology.some((entry) =>
      entry.endpoint === endpoint &&
      entry.state === framework.ZLinkTopologyState.Ready))) {
      return lastTopology;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`Discovery topology snapshot for ${channelName} did not converge: ${stringifyDiagnostic(lastTopology)}`);
}

async function waitForDiscoveryExactReadyTopology(query, framework, channelName, endpoints, timeoutMs = 7000) {
  const expected = new Set(endpoints);
  const deadline = Date.now() + timeoutMs;
  let lastTopology = [];
  while (Date.now() < deadline) {
    lastTopology = await query.topology({
      autoConnectType: framework.ZLinkAutoConnectType.ClientServer,
      serviceKind: framework.ZLinkServiceKind.Socket,
      serviceRole: framework.ZLinkServiceRole.Router,
      channelName
    });
    const readyEntries = lastTopology.filter((entry) =>
      entry.autoConnectType === framework.ZLinkAutoConnectType.ClientServer &&
      entry.serviceKind === framework.ZLinkServiceKind.Socket &&
      entry.serviceRole === framework.ZLinkServiceRole.Router &&
      entry.channelName === channelName &&
      entry.state === framework.ZLinkTopologyState.Ready);
    if (readyEntries.length === expected.size &&
      [...expected].every((endpoint) => readyEntries.some((entry) => entry.endpoint === endpoint))) {
      return normalizeDiscoveryTopology(readyEntries);
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`Discovery exact ready topology for ${channelName} did not converge: ${stringifyDiagnostic(lastTopology)}`);
}

async function waitForDiscoveryEndpoints(query, framework, channelName, endpoints, timeoutMs = 7000) {
  const expected = new Set(endpoints);
  const deadline = Date.now() + timeoutMs;
  let lastTopology = [];
  while (Date.now() < deadline) {
    lastTopology = await query.topology({
      autoConnectType: framework.ZLinkAutoConnectType.ClientServer,
      serviceKind: framework.ZLinkServiceKind.Socket,
      serviceRole: framework.ZLinkServiceRole.Router,
      channelName,
      state: framework.ZLinkTopologyState.Ready
    });
    if ([...expected].every((endpoint) => lastTopology.some((entry) => entry.endpoint === endpoint))) {
      return lastTopology.filter((entry) => expected.has(entry.endpoint));
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`Discovery endpoints for ${channelName} did not converge: ${stringifyDiagnostic(lastTopology)}`);
}

async function waitForRegistryPeerConnection(query, expectedConnectedCount) {
  const deadline = Date.now() + 7000;
  let lastStatus;
  while (Date.now() < deadline) {
    lastStatus = await query.status();
    if (lastStatus.connectedPeerRegistryCount >= expectedConnectedCount) {
      return lastStatus;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`Registry peers did not converge: ${stringifyDiagnostic(lastStatus)}`);
}

async function waitForRegistryPeerConnectionExact(query, expectedConnectedCount) {
  const deadline = Date.now() + 7000;
  let lastStatus;
  while (Date.now() < deadline) {
    lastStatus = await query.status();
    if (lastStatus.connectedPeerRegistryCount === expectedConnectedCount) {
      return lastStatus;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`Registry peer count did not converge to ${expectedConnectedCount}: ${stringifyDiagnostic(lastStatus)}`);
}

async function waitForDiscoveryMemberPeers(query, framework, channelName, endpoints, timeoutMs = 7000) {
  const expected = new Set(endpoints);
  const deadline = Date.now() + timeoutMs;
  let lastPeers = [];
  while (Date.now() < deadline) {
    lastPeers = await query.memberPeers(channelName);
    const matchingPeers = lastPeers.filter((entry) =>
      entry.autoConnectType === framework.ZLinkAutoConnectType.ClientServer &&
      entry.serviceRole === framework.ZLinkServiceRole.Router &&
      entry.channelName === channelName);
    if ([...expected].every((endpoint) => matchingPeers.some((entry) => entry.endpoint === endpoint))) {
      return matchingPeers.filter((entry) => expected.has(entry.endpoint));
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`Discovery member peers for ${channelName} did not converge: ${stringifyDiagnostic(lastPeers)}`);
}

function createDRHandler(routingId) {
  return class DiscoveryRegistryHaHandler {
    handle(payload) {
      DRHandler.requests.push({ ...payload, handledBy: routingId });
      return { ...payload, handledBy: routingId };
    }
  };
}

async function assertDiscoveryMessaging(app, nestjs, channelName, requestId, expectedProviders) {
  const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
  const expected = new Set(expectedProviders);
  const observed = new Set();
  for (let i = 0; i < 80 && observed.size < expected.size; i += 1) {
    const id = `${requestId}-${i}`;
    const reply = await submitUntilReachable(() =>
      client
        .requestToChannel(channelName, { requestId: id })
        .packetName('dr.probe')
        .timeout(1000)
        .submit());
    assert.equal(reply.requestId, id);
    assert.ok(expected.has(reply.handledBy), `unexpected DiscoveryRegistryHa provider: ${reply.handledBy}`);
    observed.add(reply.handledBy);
  }
  assert.deepEqual([...observed].sort(), [...expected].sort());
}

function normalizeDiscoveryTopology(entries) {
  return entries
    .map((entry) => ({
      autoConnectType: entry.autoConnectType,
      channelName: entry.channelName,
      endpoint: entry.endpoint,
      routingId: entry.routingId,
      serviceKind: entry.serviceKind,
      serviceRole: entry.serviceRole,
      state: entry.state
    }))
    .sort((left, right) => `${left.channelName}:${left.endpoint}`.localeCompare(`${right.channelName}:${right.endpoint}`));
}

async function resilienceLifecycle() {
  await runScenario('RL-B1', runResilienceLifecyclePendingCleanup);
  await runScenario('RL-B2', runResilienceLifecycleInFlightProviderCrash);
  await runScenario('RL-A1', runResilienceLifecycleServerRestart);
  await runScenario('RL-A2', runResilienceLifecyclePodReschedule);
  await runScenario('RL-A3', runResilienceLifecycleClientReconnectStorm);
  await runScenario('RL-A5', runResilienceLifecycleProviderFlapping);
  await runScenario('RL-B3', runResilienceLifecycleGracefulShutdown);
  await runScenario('RL-B6', runResilienceLifecycleGrayFailure);
  await runScenario('RL-C2', runResilienceLifecycleRegistryStaleCleanup);
  await runScenario('RL-C3', runResilienceLifecycleNodeDisconnectRecovery);
  await runScenario('RL-C1', runResilienceLifecycleResourceCleanup);
  await runScenario('RL-D1', runResilienceLifecycleHighFanout);
  await runScenario('RL-D2', runResilienceLifecycleObserverIsolation);
  await runScenario('RL-D3', runResilienceLifecycleLoggingMarker);
  await runScenario('RL-D5', runResilienceLifecycleMixedWorkloadSoak);
}

async function runResilienceLifecyclePendingCleanup() {
  EchoHandler.completed = [];
  const endpoint = uniqueEndpoint('rl-pending');
  const nestjs = loadNest();
  const { app } = await startApp(
    nestModule('ResiliencePendingCleanupModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .addClientServerChannel('rl.pending')
            .enableServer(endpoint)
            .enableClient(endpoint)
            .addRequestHandler('rl.echo', EchoHandler)
          .build())
      ],
      providers: [EchoHandler]
    })
  );

  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    await assert.rejects(
      () => client
        .requestToChannel('rl.pending', { id: 1, text: 'slow' })
        .packetName('rl.echo')
        .timeout(50)
        .submit(),
      /timed out|timeout|result 101/i
    );
    const afterTimeout = await client
      .requestToChannel('rl.pending', { id: 2, text: 'after-timeout' })
      .packetName('rl.echo')
      .timeout(1000)
      .submit();
    assert.deepEqual(afterTimeout, { id: 2, text: 'after-timeout', handledBy: 'rm-provider-a' });
    await waitFor(() => EchoHandler.completed.some((message) => message.id === 1), 3000);
    const afterLateReply = await client
      .requestToChannel('rl.pending', { id: 3, text: 'after-late-reply' })
      .packetName('rl.echo')
      .timeout(1000)
      .submit();
    assert.deepEqual(afterLateReply, { id: 3, text: 'after-late-reply', handledBy: 'rm-provider-a' });
    marker('RL-B1');
  } finally {
    await app.close();
  }
}

async function runResilienceLifecycleInFlightProviderCrash() {
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const providerAEndpoint = await reserveTcpEndpoint();
  const providerBEndpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  const apps = [];
  const children = [];

  try {
    const registry = await startApp(
      nestModule('ResilienceInFlightCrashRegistryModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 93
          })
        ]
      })
    );
    apps.push(registry.app);
    await startResilienceInFlightStableProvider(nestjs, registryRouterEndpoint, providerAEndpoint, apps);
    const crashProvider = await startResilienceInFlightProviderChild({
      registryRouterEndpoint,
      providerEndpoint: providerBEndpoint
    });
    children.push(crashProvider.child);

    const crashConsumer = await startApp(
      nestModule('ResilienceInFlightCrashConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .addClientServerChannel('rl.inflight')
              .enableClient(providerBEndpoint)
            .build())
        ]
      })
    );
    apps.push(crashConsumer.app);
    const stableConsumer = await startApp(
      nestModule('ResilienceInFlightStableConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .addClientServerChannel('rl.inflight')
              .enableClient(providerAEndpoint)
            .build())
        ]
      })
    );
    apps.push(stableConsumer.app);

    const crashClient = crashConsumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const stableClient = stableConsumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const beforeStable = await stableClient
      .requestToChannel('rl.inflight', { requestId: 'rl-b2-before-stable' })
      .packetName('rl.inflight.probe')
      .timeout(1000)
      .submit();
    assert.deepEqual(beforeStable, { requestId: 'rl-b2-before-stable', handledBy: 'provider-a' });

    const inFlight = crashClient
      .requestToChannel('rl.inflight', { requestId: 'rl-b2-slow-crash' })
      .packetName('rl.inflight.probe')
      .timeout(1000)
      .submit();
    await crashProvider.waitForHandling('rl-b2-slow-crash');
    await stopChildProcess(crashProvider.child, 'SIGKILL');
    children.splice(children.indexOf(crashProvider.child), 1);
    await assert.rejects(
      () => inFlight,
      isPublicInFlightCrashError
    );

    const afterStable = await stableClient
      .requestToChannel('rl.inflight', { requestId: 'rl-b2-after-stable' })
      .packetName('rl.inflight.probe')
      .timeout(1000)
      .submit();
    assert.deepEqual(afterStable, { requestId: 'rl-b2-after-stable', handledBy: 'provider-a' });
    marker('RL-B2');
  } finally {
    for (const child of children.reverse()) {
      await stopChildProcess(child, 'SIGKILL');
    }
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startResilienceInFlightStableProvider(nestjs, registryRouterEndpoint, endpoint, apps) {
  const started = await startApp(
    nestModule('ResilienceInFlightStableProviderModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .useDiscovery()
            .addRegistryEndpoint(registryRouterEndpoint)
          .addClientServerChannel('rl.inflight')
            .enableServer(endpoint)
            .routingId('provider-a')
            .addRequestHandler('rl.inflight.probe', RLInFlightStableHandler)
          .build())
      ],
      providers: [RLInFlightStableHandler]
    })
  );
  apps.push(started.app);
  return started;
}

async function runResilienceLifecycleServerRestart() {
  RLRestartHandler.requests = [];
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const providerEndpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  const framework = loadFramework();
  const apps = [];
  let provider;

  try {
    const registry = await startApp(
      nestModule('ResilienceRestartRegistryModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 90
          })
        ]
      })
    );
    apps.push(registry.app);
    provider = await startResilienceRestartProvider(nestjs, registryRouterEndpoint, providerEndpoint, 'v1', apps);
    const consumer = await startApp(
      nestModule('ResilienceRestartConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(registryRouterEndpoint)
            .addClientServerChannel('rl.restart')
              .enableClient()
            .build())
        ]
      })
    );
    apps.push(consumer.app);

    const query = registry.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rl.restart', [providerEndpoint]);
    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const before = await submitUntilReachable(() =>
      client
        .requestToChannel('rl.restart', { requestId: 'rl-a1-before' })
        .packetName('rl.restart.probe')
        .timeout(1000)
        .submit());
    assert.deepEqual(before, { requestId: 'rl-a1-before', handledBy: 'provider-a', generation: 'v1' });

    await provider.app.close();
    apps.splice(apps.indexOf(provider.app), 1);
    await waitForResilienceRestartProviderRemoved(query, providerEndpoint);
    await assert.rejects(
      () => client
        .requestToChannel('rl.restart', { requestId: 'rl-a1-down' })
        .packetName('rl.restart.probe')
        .timeout(100)
        .submit(),
      (error) => isPublicRouteUnavailableError(error)
    );

    provider = await startResilienceRestartProvider(nestjs, registryRouterEndpoint, providerEndpoint, 'v2', apps);
    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rl.restart', [providerEndpoint]);
    const after = await submitUntilReachable(() =>
      client
        .requestToChannel('rl.restart', { requestId: 'rl-a1-after' })
        .packetName('rl.restart.probe')
        .timeout(1000)
        .submit());
    assert.deepEqual(after, { requestId: 'rl-a1-after', handledBy: 'provider-a', generation: 'v2' });
    marker('RL-A1');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startResilienceRestartProvider(nestjs, registryRouterEndpoint, endpoint, generation, apps) {
  const handler = createRLRestartHandler(generation);
  const started = await startApp(
    nestModule(`ResilienceRestartProvider${generation}Module`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .useDiscovery()
            .addRegistryEndpoint(registryRouterEndpoint)
          .addClientServerChannel('rl.restart')
            .enableServer(endpoint)
            .routingId('provider-a')
            .addRequestHandler('rl.restart.probe', handler)
          .build())
      ],
      providers: [handler]
    })
  );
  apps.push(started.app);
  return started;
}

async function waitForResilienceRestartProviderRemoved(query, providerEndpoint) {
  const deadline = Date.now() + 5000;
  let lastTopology = [];
  while (Date.now() < deadline) {
    lastTopology = await query.topology({ channelName: 'rl.restart' });
    if (!lastTopology.some((entry) => entry.endpoint === providerEndpoint)) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`RL-A1 provider remained in topology: ${stringifyDiagnostic(lastTopology)}`);
}

async function runResilienceLifecyclePodReschedule() {
  RLRescheduleHandler.requests = [];
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const providerV1Endpoint = await reserveTcpEndpoint();
  const providerV2Endpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  const framework = loadFramework();
  const apps = [];
  let providerV1;
  let providerV2;

  try {
    const registry = await startApp(
      nestModule('ResiliencePodRescheduleRegistryModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 94
          })
        ]
      })
    );
    apps.push(registry.app);
    providerV1 = await startResilienceRescheduleProvider(
      nestjs,
      registryRouterEndpoint,
      providerV1Endpoint,
      'provider-v1',
      apps
    );
    const consumer = await startApp(
      nestModule('ResiliencePodRescheduleConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(registryRouterEndpoint)
            .addClientServerChannel('rl.reschedule')
              .enableClient()
            .build())
        ]
      })
    );
    apps.push(consumer.app);

    const query = registry.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryMessagingSingleReadyProvider(query, framework, 'rl.reschedule', 'provider-a', providerV1Endpoint);
    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const before = await submitUntilReachable(() =>
      client
        .requestToChannel('rl.reschedule', { requestId: 'rl-a2-before-reschedule' })
        .packetName('rl.reschedule.probe')
        .timeout(1000)
        .submit());
    assert.deepEqual(before, { requestId: 'rl-a2-before-reschedule', handledBy: 'provider-v1' });

    await providerV1.app.close();
    apps.splice(apps.indexOf(providerV1.app), 1);
    providerV1 = undefined;
    providerV2 = await startResilienceRescheduleProvider(
      nestjs,
      registryRouterEndpoint,
      providerV2Endpoint,
      'provider-v2',
      apps
    );
    await waitForRegistryMessagingSingleReadyProvider(query, framework, 'rl.reschedule', 'provider-a', providerV2Endpoint);

    for (let i = 0; i < 20; i += 1) {
      const requestId = `rl-a2-after-reschedule-${i}`;
      const reply = await client
        .requestToChannel('rl.reschedule', { requestId })
        .packetName('rl.reschedule.probe')
        .timeout(1000)
        .submit();
      assert.deepEqual(reply, { requestId, handledBy: 'provider-v2' });
    }
    marker('RL-A2');
  } finally {
    if (providerV1 !== undefined && apps.includes(providerV1.app)) {
      await providerV1.app.close();
      apps.splice(apps.indexOf(providerV1.app), 1);
    }
    if (providerV2 !== undefined && apps.includes(providerV2.app)) {
      await providerV2.app.close();
      apps.splice(apps.indexOf(providerV2.app), 1);
    }
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startResilienceRescheduleProvider(nestjs, registryRouterEndpoint, endpoint, providerId, apps) {
  const handler = createRLRescheduleHandler(providerId);
  const started = await startApp(
    nestModule(`ResiliencePodReschedule${providerId}Module`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .useDiscovery()
            .addRegistryEndpoint(registryRouterEndpoint)
          .addClientServerChannel('rl.reschedule')
            .enableServer(endpoint)
            .routingId('provider-a')
            .addRequestHandler('rl.reschedule.probe', handler)
          .build())
      ],
      providers: [handler]
    })
  );
  apps.push(started.app);
  return started;
}

async function runResilienceLifecycleClientReconnectStorm() {
  RLReconnectHandler.requests = [];
  const endpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  const apps = [];
  const clients = [];
  const clientCount = 12;

  try {
    const server = await startApp(
      nestModule('ResilienceReconnectStormServerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .addClientServerChannel('rl.reconnect')
              .enableServer(endpoint)
              .routingId('rl-reconnect-server')
              .addRequestHandler('rl.reconnect.probe', RLReconnectHandler)
            .build())
        ],
        providers: [RLReconnectHandler]
      })
    );
    apps.push(server.app);

    for (let index = 0; index < clientCount; index += 1) {
      const clientApp = await startResilienceReconnectClient(nestjs, endpoint, `before-${index}`);
      clients.push(clientApp);
      apps.push(clientApp.app);
    }

    const warmupReplies = await Promise.all(clients.map((clientApp, index) =>
      submitResilienceReconnectProbe(clientApp.app, nestjs, `client-${index}`, 'before')));
    assert.deepEqual(warmupReplies.map((reply) => reply.clientId).sort(), expectedReconnectClientIds(clientCount));
    assert.deepEqual(new Set(warmupReplies.map((reply) => reply.phase)), new Set(['before']));

    await Promise.all(clients.map((clientApp) => clientApp.app.close()));
    for (const clientApp of clients) {
      apps.splice(apps.indexOf(clientApp.app), 1);
    }
    clients.length = 0;

    await Promise.all(Array.from({ length: clientCount }, async (_, index) => {
      const clientApp = await startResilienceReconnectClient(nestjs, endpoint, `after-${index}`);
      clients.push(clientApp);
      apps.push(clientApp.app);
    }));

    const reconnectReplies = await Promise.all(clients.map((clientApp, index) =>
      submitUntilReachable(() =>
        submitResilienceReconnectProbe(clientApp.app, nestjs, `client-${index}`, 'after'))));
    assert.deepEqual(reconnectReplies.map((reply) => reply.clientId).sort(), expectedReconnectClientIds(clientCount));
    assert.deepEqual(new Set(reconnectReplies.map((reply) => reply.phase)), new Set(['after']));

    const normalizedReplies = await Promise.all(clients.map((clientApp, index) =>
      submitResilienceReconnectProbe(clientApp.app, nestjs, `client-${index}`, 'normalized')));
    assert.deepEqual(normalizedReplies.map((reply) => reply.clientId).sort(), expectedReconnectClientIds(clientCount));
    assert.deepEqual(new Set(normalizedReplies.map((reply) => reply.phase)), new Set(['normalized']));
    assert.equal(RLReconnectHandler.requests.length, clientCount * 3);
    marker('RL-A3');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startResilienceReconnectClient(nestjs, endpoint, suffix) {
  return startApp(
    nestModule(`ResilienceReconnectStormClient${suffix}Module`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .addClientServerChannel('rl.reconnect')
            .enableClient(endpoint)
          .build())
      ]
    })
  );
}

async function submitResilienceReconnectProbe(app, nestjs, clientId, phase) {
  const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
  return client
    .requestToChannel('rl.reconnect', { clientId, phase })
    .packetName('rl.reconnect.probe')
    .timeout(1000)
    .submit();
}

function expectedReconnectClientIds(clientCount) {
  return Array.from({ length: clientCount }, (_, index) => `client-${index}`).sort();
}

async function runResilienceLifecycleProviderFlapping() {
  RLFlapHandler.requests = [];
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const providerAEndpoint = await reserveTcpEndpoint();
  const providerBEndpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  const framework = loadFramework();
  const apps = [];
  let providerB;

  try {
    const registry = await startApp(
      nestModule('ResilienceFlapRegistryModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 92
          })
        ]
      })
    );
    apps.push(registry.app);
    await startResilienceFlapProvider(nestjs, registryRouterEndpoint, providerAEndpoint, 'provider-a', apps);
    providerB = await startResilienceFlapProvider(nestjs, registryRouterEndpoint, providerBEndpoint, 'provider-b', apps);
    const consumer = await startApp(
      nestModule('ResilienceFlapConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(registryRouterEndpoint)
            .addClientServerChannel('rl.flap')
              .enableClient()
            .build())
        ]
      })
    );
    apps.push(consumer.app);

    const query = registry.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rl.flap', [providerAEndpoint, providerBEndpoint]);
    await waitForResilienceFlapProviders(client, ['provider-a', 'provider-b'], 'rl-a5-warmup');

    for (let cycle = 1; cycle <= 3; cycle += 1) {
      await providerB.app.close();
      apps.splice(apps.indexOf(providerB.app), 1);
      await waitForResilienceFlapProviderRemoved(query, providerAEndpoint, providerBEndpoint);
      const providerBCount = RLFlapHandler.requests.filter((request) => request.handledBy === 'provider-b').length;
      for (let i = 0; i < 5; i += 1) {
        const requestId = `rl-a5-down-${cycle}-${i}`;
        const reply = await client
          .requestToChannel('rl.flap', { requestId })
          .packetName('rl.flap.probe')
          .timeout(1000)
          .submit();
        assert.deepEqual(reply, { requestId, handledBy: 'provider-a' });
      }
      assert.equal(RLFlapHandler.requests.filter((request) => request.handledBy === 'provider-b').length, providerBCount);

      providerB = await startResilienceFlapProvider(nestjs, registryRouterEndpoint, providerBEndpoint, 'provider-b', apps);
      await waitForRegistryMessagingChannelEndpoints(query, framework, 'rl.flap', [providerAEndpoint, providerBEndpoint]);
      await waitForResilienceFlapProviders(client, ['provider-a', 'provider-b'], `rl-a5-up-${cycle}`);
    }

    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rl.flap', [providerAEndpoint, providerBEndpoint]);
    marker('RL-A5');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startResilienceFlapProvider(nestjs, registryRouterEndpoint, endpoint, providerId, apps) {
  const handler = createRLFlapHandler(providerId);
  const started = await startApp(
    nestModule(`ResilienceFlap${providerId}Module`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .useDiscovery()
            .addRegistryEndpoint(registryRouterEndpoint)
          .addClientServerChannel('rl.flap')
            .enableServer(endpoint)
            .routingId(providerId)
            .addRequestHandler('rl.flap.probe', handler)
          .build())
      ],
      providers: [handler]
    })
  );
  apps.push(started.app);
  return started;
}

async function waitForResilienceFlapProviders(client, providerIds, prefix) {
  const expected = new Set(providerIds);
  const observed = new Set();
  for (let i = 0; i < 80 && observed.size < expected.size; i += 1) {
    const requestId = `${prefix}-${i}`;
    const reply = await submitUntilReachable(() =>
      client
        .requestToChannel('rl.flap', { requestId })
        .packetName('rl.flap.probe')
        .timeout(1000)
        .submit());
    assert.equal(reply.requestId, requestId);
    assert.ok(expected.has(reply.handledBy), `unexpected flapping provider: ${reply.handledBy}`);
    observed.add(reply.handledBy);
  }
  assert.deepEqual([...observed].sort(), [...expected].sort());
}

async function waitForResilienceFlapProviderRemoved(query, providerAEndpoint, providerBEndpoint) {
  const deadline = Date.now() + 5000;
  let lastTopology = [];
  while (Date.now() < deadline) {
    lastTopology = await query.topology({ channelName: 'rl.flap' });
    if (lastTopology.some((entry) => entry.endpoint === providerAEndpoint) &&
      !lastTopology.some((entry) => entry.endpoint === providerBEndpoint)) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`RL-A5 provider-b remained in topology: ${stringifyDiagnostic(lastTopology)}`);
}

async function runResilienceLifecycleGracefulShutdown() {
  RLGracefulHandler.requests = [];
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const providerAEndpoint = await reserveTcpEndpoint();
  const providerBEndpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  const framework = loadFramework();
  const apps = [];
  let providerB;

  try {
    const registry = await startApp(
      nestModule('ResilienceGracefulRegistryModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 91
          })
        ]
      })
    );
    apps.push(registry.app);
    await startResilienceGracefulProvider(nestjs, registryRouterEndpoint, providerAEndpoint, 'provider-a', apps);
    providerB = await startResilienceGracefulProvider(nestjs, registryRouterEndpoint, providerBEndpoint, 'provider-b', apps);
    const consumer = await startApp(
      nestModule('ResilienceGracefulConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(registryRouterEndpoint)
            .addClientServerChannel('rl.graceful')
              .enableClient()
            .build())
        ]
      })
    );
    apps.push(consumer.app);

    const query = registry.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rl.graceful', [providerAEndpoint, providerBEndpoint]);
    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    await waitForResilienceGracefulProviders(client, ['provider-a', 'provider-b'], 'rl-b3-before');

    const beforeShutdown = await waitForResilienceGracefulProviderReply(client, 'provider-b', 'rl-b3-before-shutdown');
    assert.deepEqual(beforeShutdown, { requestId: 'rl-b3-before-shutdown', handledBy: 'provider-b' });
    const providerBCount = RLGracefulHandler.requests.filter((request) => request.handledBy === 'provider-b').length;

    await providerB.app.close();
    apps.splice(apps.indexOf(providerB.app), 1);
    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rl.graceful', [providerAEndpoint]);
    await waitForResilienceGracefulProviderRemoved(query, providerAEndpoint, providerBEndpoint);

    for (let i = 0; i < 20; i += 1) {
      const requestId = `rl-b3-after-shutdown-${i}`;
      const reply = await client
        .requestToChannel('rl.graceful', { requestId })
        .packetName('rl.graceful.probe')
        .timeout(1000)
        .submit();
      assert.deepEqual(reply, { requestId, handledBy: 'provider-a' });
    }
    assert.equal(RLGracefulHandler.requests.filter((request) => request.handledBy === 'provider-b').length, providerBCount);
    marker('RL-B3');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startResilienceGracefulProvider(nestjs, registryRouterEndpoint, endpoint, providerId, apps) {
  const handler = createRLGracefulHandler(providerId);
  const started = await startApp(
    nestModule(`ResilienceGraceful${providerId}Module`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .useDiscovery()
            .addRegistryEndpoint(registryRouterEndpoint)
          .addClientServerChannel('rl.graceful')
            .enableServer(endpoint)
            .routingId(providerId)
            .addRequestHandler('rl.graceful.probe', handler)
          .build())
      ],
      providers: [handler]
    })
  );
  apps.push(started.app);
  return started;
}

async function waitForResilienceGracefulProviders(client, providerIds, prefix) {
  const expected = new Set(providerIds);
  const observed = new Set();
  for (let i = 0; i < 80 && observed.size < expected.size; i += 1) {
    const requestId = `${prefix}-${i}`;
    const reply = await submitUntilReachable(() =>
      client
        .requestToChannel('rl.graceful', { requestId })
        .packetName('rl.graceful.probe')
        .timeout(1000)
        .submit());
    assert.equal(reply.requestId, requestId);
    assert.ok(expected.has(reply.handledBy), `unexpected graceful provider: ${reply.handledBy}`);
    observed.add(reply.handledBy);
  }
  assert.deepEqual([...observed].sort(), [...expected].sort());
}

async function waitForResilienceGracefulProviderReply(client, providerId, requestId) {
  for (let i = 0; i < 80; i += 1) {
    const reply = await submitUntilReachable(() =>
      client
        .requestToChannel('rl.graceful', { requestId: i === 0 ? requestId : `${requestId}-${i}` })
        .packetName('rl.graceful.probe')
        .timeout(1000)
        .submit());
    if (reply.handledBy === providerId) {
      return { requestId, handledBy: reply.handledBy };
    }
  }
  assert.fail(`RL-B3 did not observe ${providerId} before shutdown.`);
}

async function waitForResilienceGracefulProviderRemoved(query, providerAEndpoint, providerBEndpoint) {
  const deadline = Date.now() + 5000;
  let lastTopology = [];
  while (Date.now() < deadline) {
    lastTopology = await query.topology({ channelName: 'rl.graceful' });
    if (lastTopology.some((entry) => entry.endpoint === providerAEndpoint) &&
      !lastTopology.some((entry) => entry.endpoint === providerBEndpoint)) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`RL-B3 provider-b remained in topology: ${stringifyDiagnostic(lastTopology)}`);
}

async function runResilienceLifecycleGrayFailure() {
  RLGrayFailureHandler.requests = [];
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const healthyEndpoint = await reserveTcpEndpoint();
  const grayEndpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  const framework = loadFramework();
  const apps = [];

  try {
    const registry = await startApp(
      nestModule('ResilienceGrayFailureRegistryModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 99
          })
        ]
      })
    );
    apps.push(registry.app);
    await startResilienceGrayFailureProvider(nestjs, registryRouterEndpoint, healthyEndpoint, 'provider-a', false, apps);
    await startResilienceGrayFailureProvider(nestjs, registryRouterEndpoint, grayEndpoint, 'provider-b', true, apps);
    const consumer = await startApp(
      nestModule('ResilienceGrayFailureConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(registryRouterEndpoint)
            .addClientServerChannel('rl.gray')
              .enableClient()
            .build())
        ]
      })
    );
    apps.push(consumer.app);
    const query = registry.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rl.gray', [healthyEndpoint, grayEndpoint]);

    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const healthyReplies = [];
    const publicFailures = [];
    for (let index = 0; index < 60; index += 1) {
      const requestId = `rl-b6-discovery-${index}`;
      try {
        const reply = await client
          .requestToChannel('rl.gray', { requestId, delayOnGray: true })
          .packetName('rl.gray.probe')
          .timeout(80)
          .submit();
        assert.equal(reply.requestId, requestId);
        assert.equal(reply.handledBy, 'provider-a');
        assert.equal(reply.delayOnGray, true);
        healthyReplies.push(reply);
      } catch (error) {
        assert.ok(isPublicGrayFailureError(error), `unexpected RL-B6 error: ${error?.stack ?? error}`);
        publicFailures.push(error);
      }
    }
    assert.ok(healthyReplies.length >= 20, `RL-B6 healthy provider success count too low: ${healthyReplies.length}`);
    assert.ok(publicFailures.length >= 1, 'RL-B6 did not observe any public gray-provider failure.');
    assert.equal(
      RLGrayFailureHandler.requests.filter((request) => request.handledBy === 'provider-a').length,
      healthyReplies.length
    );
    assert.ok(RLGrayFailureHandler.requests.some((request) =>
      request.handledBy === 'provider-b' && request.delayed === true));
    marker('RL-B6');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startResilienceGrayFailureProvider(nestjs, registryRouterEndpoint, endpoint, providerId, gray, apps) {
  const handler = createRLGrayFailureHandler(providerId, gray);
  const started = await startApp(
    nestModule(`ResilienceGrayFailure${providerId}ProviderModule`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .useDiscovery()
            .addRegistryEndpoint(registryRouterEndpoint)
          .addClientServerChannel('rl.gray')
            .enableServer(endpoint)
            .routingId(providerId)
            .addRequestHandler('rl.gray.probe', handler)
          .build())
      ],
      providers: [handler]
    })
  );
  apps.push(started.app);
  return started;
}

async function runResilienceLifecycleRegistryStaleCleanup() {
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const providerAEndpoint = await reserveTcpEndpoint();
  const providerBEndpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  const framework = loadFramework();
  const apps = [];
  const children = [];

  try {
    const registry = await startApp(
      nestModule('ResilienceStaleRegistryModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 97,
            heartbeatIntervalMs: 100,
            heartbeatTimeoutMs: 350,
            broadcastIntervalMs: 100
          })
        ]
      })
    );
    apps.push(registry.app);
    await startResilienceLifecycleStaleProvider(nestjs, registryRouterEndpoint, providerAEndpoint, 'provider-a', apps);
    const killedProvider = await startResilienceInFlightProviderChild({
      moduleName: 'ResilienceStaleKilledProviderModule',
      registryRouterEndpoint,
      providerEndpoint: providerBEndpoint,
      channelName: 'rl.stale',
      packetName: 'rl.stale.probe',
      providerId: 'provider-b',
      routingId: 'provider-b',
      blockRequestIds: []
    });
    children.push(killedProvider.child);

    const consumer = await startApp(
      nestModule('ResilienceStaleConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(registryRouterEndpoint)
            .addClientServerChannel('rl.stale')
              .enableClient()
            .build())
        ]
      })
    );
    apps.push(consumer.app);
    const query = registry.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    await waitForRegistryMessagingChannelEndpoints(query, framework, 'rl.stale', [providerAEndpoint, providerBEndpoint]);
    await waitForResilienceProviderReplies(client, 'rl.stale', 'rl.stale.probe', ['provider-a', 'provider-b'], 'rl-c2-before');

    await stopChildProcess(killedProvider.child, 'SIGKILL');
    children.splice(children.indexOf(killedProvider.child), 1);
    await waitForRegistryChannelEndpointAbsent(query, framework, 'rl.stale', providerBEndpoint);

    for (let index = 0; index < 10; index += 1) {
      const requestId = `rl-c2-after-${index}`;
      const reply = await client
        .requestToChannel('rl.stale', { requestId })
        .packetName('rl.stale.probe')
        .timeout(1000)
        .submit();
      assert.deepEqual(reply, { requestId, handledBy: 'provider-a' });
    }
    marker('RL-C2');
  } finally {
    for (const child of children.reverse()) {
      await stopChildProcess(child, 'SIGKILL');
    }
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function startResilienceLifecycleStaleProvider(nestjs, registryRouterEndpoint, endpoint, providerId, apps) {
  const handler = createRLStaleHandler(providerId);
  const started = await startApp(
    nestModule(`ResilienceStale${providerId}ProviderModule`, {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .useDiscovery()
            .addRegistryEndpoint(registryRouterEndpoint)
          .addClientServerChannel('rl.stale')
            .enableServer(endpoint)
            .routingId(providerId)
            .addRequestHandler('rl.stale.probe', handler)
          .build())
      ],
      providers: [handler]
    })
  );
  apps.push(started.app);
  return started;
}

async function waitForResilienceProviderReplies(client, channelName, packetName, providerIds, prefix) {
  const expected = new Set(providerIds);
  const observed = new Set();
  for (let index = 0; index < 80 && observed.size < expected.size; index += 1) {
    const requestId = `${prefix}-${index}`;
    const reply = await submitUntilReachable(() =>
      client
        .requestToChannel(channelName, { requestId })
        .packetName(packetName)
        .timeout(1000)
        .submit());
    assert.equal(reply.requestId, requestId);
    assert.ok(expected.has(reply.handledBy), `unexpected provider for ${channelName}: ${reply.handledBy}`);
    observed.add(reply.handledBy);
  }
  assert.deepEqual([...observed].sort(), [...expected].sort());
}

async function waitForRegistryChannelEndpointAbsent(query, framework, channelName, endpoint) {
  const deadline = Date.now() + 5000;
  let lastTopology = [];
  while (Date.now() < deadline) {
    lastTopology = await query.topology({
      autoConnectType: framework.ZLinkAutoConnectType.ClientServer,
      serviceKind: framework.ZLinkServiceKind.Socket,
      serviceRole: framework.ZLinkServiceRole.Router,
      channelName
    });
    if (!lastTopology.some((entry) => entry.endpoint === endpoint)) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(`Registry endpoint remained in topology: ${endpoint}; topology=${stringifyDiagnostic(lastTopology)}`);
}

function isPublicRouteUnavailableError(error) {
  const framework = loadFramework();
  if (error instanceof framework.ZLinkFrameworkException) {
    return [
      framework.ZLinkFrameworkErrorKind.RouteNotConnected,
      framework.ZLinkFrameworkErrorKind.RequestTargetNotFound
    ].includes(error.kind);
  }
  return error instanceof Error && /ZLink async submit timed out/i.test(error.message);
}

async function runResilienceLifecycleNodeDisconnectRecovery() {
  const registryPubEndpoint = await reserveTcpEndpoint();
  const registryRouterEndpoint = await reserveTcpEndpoint();
  const providerEndpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  const framework = loadFramework();
  const apps = [];
  const children = [];

  try {
    const registry = await startApp(
      nestModule('ResilienceDisconnectRegistryModule', {
        imports: [
          nestjs.ZLinkRegistryModule.forRoot({
            pubEndpoint: registryPubEndpoint,
            routerEndpoint: registryRouterEndpoint,
            registryId: 98,
            heartbeatIntervalMs: 100,
            heartbeatTimeoutMs: 350,
            broadcastIntervalMs: 100
          })
        ]
      })
    );
    apps.push(registry.app);
    let provider = await startResilienceInFlightProviderChild({
      moduleName: 'ResilienceDisconnectProviderModule',
      registryRouterEndpoint,
      providerEndpoint,
      channelName: 'rl.disconnect',
      packetName: 'rl.disconnect.probe',
      providerId: 'provider-v1',
      routingId: 'provider-a',
      blockRequestIds: []
    });
    children.push(provider.child);

    const consumer = await startApp(
      nestModule('ResilienceDisconnectConsumerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .useDiscovery()
              .addRegistryEndpoint(registryRouterEndpoint)
            .addClientServerChannel('rl.disconnect')
              .enableClient()
            .build())
        ]
      })
    );
    apps.push(consumer.app);
    const query = registry.app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });
    const client = consumer.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    await waitForRegistryMessagingSingleReadyProvider(query, framework, 'rl.disconnect', 'provider-a', providerEndpoint);
    const before = await submitUntilReachable(() =>
      client
        .requestToChannel('rl.disconnect', { requestId: 'rl-c3-before' })
        .packetName('rl.disconnect.probe')
        .timeout(1000)
        .submit());
    assert.deepEqual(before, { requestId: 'rl-c3-before', handledBy: 'provider-v1' });

    await stopChildProcess(provider.child, 'SIGKILL');
    children.splice(children.indexOf(provider.child), 1);
    await waitForRegistryChannelEndpointAbsent(query, framework, 'rl.disconnect', providerEndpoint);
    const downStartedAt = Date.now();
    await assert.rejects(
      () => client
        .requestToChannel('rl.disconnect', { requestId: 'rl-c3-down' })
        .packetName('rl.disconnect.probe')
        .timeout(100)
        .submit(),
      (error) => isPublicRouteUnavailableError(error)
    );
    assert.ok(Date.now() - downStartedAt < 1200, 'RL-C3 down request did not fail within the expected public timeout window.');

    provider = await startResilienceInFlightProviderChild({
      moduleName: 'ResilienceDisconnectRecoveredProviderModule',
      registryRouterEndpoint,
      providerEndpoint,
      channelName: 'rl.disconnect',
      packetName: 'rl.disconnect.probe',
      providerId: 'provider-v2',
      routingId: 'provider-a',
      blockRequestIds: []
    });
    children.push(provider.child);
    await waitForRegistryMessagingSingleReadyProvider(query, framework, 'rl.disconnect', 'provider-a', providerEndpoint);
    const after = await submitUntilReachable(() =>
      client
        .requestToChannel('rl.disconnect', { requestId: 'rl-c3-after' })
        .packetName('rl.disconnect.probe')
        .timeout(1000)
        .submit());
    assert.deepEqual(after, { requestId: 'rl-c3-after', handledBy: 'provider-v2' });
    marker('RL-C3');
  } finally {
    for (const child of children.reverse()) {
      await stopChildProcess(child, 'SIGKILL');
    }
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function runResilienceLifecycleResourceCleanup() {
  EchoHandler.completed = [];
  const baselineHandles = activeTcpHandleCount();
  const baselineHeap = process.memoryUsage().heapUsed;
  const endpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  const apps = [];
  const clientCount = 8;
  const requestCount = 20;

  try {
    const server = await startApp(
      nestModule('ResilienceResourceCleanupServerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .addClientServerChannel('rl.cleanup')
              .enableServer(endpoint)
              .routingId('rl-cleanup-provider')
              .addRequestHandler('rl.cleanup.probe', EchoHandler)
            .build())
        ],
        providers: [EchoHandler]
      })
    );
    apps.push(server.app);

    for (let index = 0; index < clientCount; index += 1) {
      const clientApp = await startApp(
        nestModule(`ResilienceResourceCleanupClient${index}Module`, {
          imports: [
            nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
              .addClientServerChannel('rl.cleanup')
                .enableClient(endpoint)
              .build())
          ]
        })
      );
      apps.push(clientApp.app);
    }

    const clients = apps.slice(1).map((app) => app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false }));
    await Promise.all(clients.map(async (client, clientIndex) => {
      for (let seq = 0; seq < requestCount; seq += 1) {
        const id = clientIndex * requestCount + seq;
        const reply = await client
          .requestToChannel('rl.cleanup', { id, text: `cleanup-${id}` })
          .packetName('rl.cleanup.probe')
          .timeout(1000)
          .submit();
        assert.deepEqual(reply, { id, text: `cleanup-${id}`, handledBy: 'rm-provider-a' });
      }
    }));
    assert.equal(EchoHandler.completed.length, clientCount * requestCount);
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }

  await waitFor(() => activeTcpHandleCount() <= baselineHandles + 1, 5000);
  const cleanupHeap = process.memoryUsage().heapUsed;
  assert.ok(
    cleanupHeap <= baselineHeap + 64 * 1024 * 1024,
    `RL-C1 heap grew too much: before=${baselineHeap} after=${cleanupHeap}`
  );
  marker('RL-C1');
}

async function runResilienceLifecycleHighFanout() {
  RLFanoutEvidence.events = [];
  const eventsEndpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  let publisher;
  const subscribers = [];

  try {
    publisher = await startApp(
      nestModule('ResilienceHighFanoutPublisherModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .addFanoutChannel('ps.events')
              .enablePublisher(eventsEndpoint)
            .build())
        ]
      })
    );

    for (let index = 0; index < 6; index += 1) {
      subscribers.push(await startPubSubSubscriber(
        `ResilienceHighFanoutSubscriber${index}Module`,
        createRLFanoutHandler(index),
        eventsEndpoint
      ));
    }

    const fanout = publisher.app.get(nestjs.ZLINK_FANOUT_CLIENT, { strict: false });
    await publishUntil(fanout, 'rl-d1', -1, () =>
      hasRLFanoutSequence(6, [-1]));
    RLFanoutEvidence.events = [];

    for (let seq = 1; seq <= 8; seq += 1) {
      await publishEvent(fanout, 'rl-d1', seq);
    }

    await waitFor(() => hasRLFanoutSequence(6, [1, 2, 3, 4, 5, 6, 7, 8]));
    assert.equal(new Set(RLFanoutEvidence.events
      .filter((event) => event.seq > 0)
      .map((event) => `${event.subscriberId}:${event.seq}`)).size, 48);
    marker('RL-D1');
  } finally {
    for (const subscriber of subscribers.reverse()) {
      await subscriber.app.close();
    }
    if (publisher !== undefined) {
      await publisher.app.close();
    }
  }
}

async function runResilienceLifecycleObserverIsolation() {
  EchoHandler.completed = [];
  RLObserverFailures.events = [];
  const endpoint = uniqueEndpoint('rl-observer');
  const nestjs = loadNest();
  const framework = loadFramework();
  const builder = nestjs.zlinkFramework();
  builder.configureDispatch()
    .setMessageFlowObserver(RLThrowingFlowObserver)
    .messageFlow(framework.ZLinkMessageFlowLogMode.ErrorsOnly);
  builder.addClientServerChannel('rl.observer')
    .enableServer(endpoint)
    .enableClient(endpoint)
    .addRequestHandler('rl.echo', EchoHandler);
  const { app } = await startApp(
    nestModule('ResilienceObserverIsolationModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(builder.build())
      ],
      providers: [EchoHandler, RLThrowingFlowObserver]
    })
  );

  try {
    const runtime = app.get(nestjs.ZLINK_FRAMEWORK_RUNTIME, { strict: false });
    const unsubscribe = runtime.errorSink.onRuntimeTaskException((failure) => {
      RLObserverFailures.events.push({
        taskName: failure.taskName,
        message: failure.error instanceof Error ? failure.error.message : String(failure.error)
      });
    });
    try {
      const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
      await assert.rejects(
        () => client
          .requestToChannel('rl.observer', { id: 10, text: 'missing' })
          .packetName('rl.missing')
          .timeout(1000)
          .submit(),
        /No channel request handler is registered for 'rl\.observer:rl\.missing'/
      );
      await waitFor(() => RLObserverFailures.events.some((event) =>
        event.taskName === 'dispatch-error-observer' &&
        event.message === 'rl observer failure'));
      const reply = await client
        .requestToChannel('rl.observer', { id: 11, text: 'after-observer-failure' })
        .packetName('rl.echo')
        .timeout(1000)
        .submit();
      assert.deepEqual(reply, { id: 11, text: 'after-observer-failure', handledBy: 'rm-provider-a' });
      marker('RL-D2');
    } finally {
      unsubscribe();
    }
  } finally {
    await app.close();
  }
}

async function runResilienceLifecycleLoggingMarker() {
  const logDir = fs.mkdtempSync(path.join(os.tmpdir(), 'zlink-node-rl-log-'));
  const logFile = path.join(logDir, 'dispatch-errors.log');
  RLLoggingObserver.logFile = logFile;
  const endpoint = uniqueEndpoint('rl-log');
  const nestjs = loadNest();
  const framework = loadFramework();
  const builder = nestjs.zlinkFramework();
  builder.configureDispatch()
    .setMessageFlowObserver(RLLoggingObserver)
    .messageFlow(framework.ZLinkMessageFlowLogMode.ErrorsOnly);
  builder.addClientServerChannel('rl.log')
    .enableServer(endpoint)
    .enableClient(endpoint)
    .addRequestHandler('rl.echo', EchoHandler);
  const { app } = await startApp(
    nestModule('ResilienceLoggingMarkerModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(builder.build())
      ],
      providers: [EchoHandler, RLLoggingObserver]
    })
  );

  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    await assert.rejects(
      () => client
        .requestToChannel('rl.log', { id: 20, text: 'missing' })
        .packetName('rl.missing')
        .timeout(1000)
        .submit(),
      /No channel request handler is registered for 'rl\.log:rl\.missing'/
    );
    await waitFor(() => fs.existsSync(logFile) && fs.readFileSync(logFile, 'utf8').includes('packetName=rl.missing'));
    const text = fs.readFileSync(logFile, 'utf8');
    assert.match(text, /reason=handlerMissing/);
    assert.match(text, /action=replyError/);
    assert.match(text, /packetName=rl\.missing/);
    assert.match(text, /channelName=rl\.log/);
    marker('RL-D3');
  } finally {
    await app.close();
    fs.rmSync(logDir, { recursive: true, force: true });
    RLLoggingObserver.logFile = undefined;
  }
}

async function runResilienceLifecycleMixedWorkloadSoak() {
  RLSoakEvidence.requests = [];
  RLSoakEvidence.sends = [];
  const endpoint = await reserveTcpEndpoint();
  const nestjs = loadNest();
  const apps = [];
  const clients = [];
  const clientCount = 6;
  const durationMs = 2000;

  try {
    const server = await startApp(
      nestModule('ResilienceMixedWorkloadSoakServerModule', {
        imports: [
          nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
            .addClientServerChannel('rl.soak')
              .enableServer(endpoint)
              .routingId('rl-soak-provider')
              .addRequestHandler('rl.soak.request', RLSoakRequestHandler)
              .addSendHandler('rl.soak.send', RLSoakSendHandler)
            .build())
        ],
        providers: [RLSoakRequestHandler, RLSoakSendHandler]
      })
    );
    apps.push(server.app);

    for (let index = 0; index < clientCount; index += 1) {
      const clientApp = await startApp(
        nestModule(`ResilienceMixedWorkloadSoakClient${index}Module`, {
          imports: [
            nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
              .addClientServerChannel('rl.soak')
                .enableClient(endpoint)
              .build())
          ]
        })
      );
      clients.push(clientApp);
      apps.push(clientApp.app);
    }

    const startedAt = Date.now();
    const latencies = [];
    let requestCount = 0;
    let sendCount = 0;
    await Promise.all(clients.map(async (clientApp, clientIndex) => {
      const client = clientApp.app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
      let seq = 0;
      while (Date.now() - startedAt < durationMs) {
        const requestId = `rl-d5-c${clientIndex}-r${seq}`;
        const sentAt = Date.now();
        const reply = await client
          .requestToChannel('rl.soak', { requestId, clientIndex, seq })
          .packetName('rl.soak.request')
          .timeout(1000)
          .submit();
        latencies.push(Date.now() - sentAt);
        assert.deepEqual(reply, { requestId, clientIndex, seq, handledBy: 'rl-soak-provider' });
        requestCount += 1;

        const sendId = `rl-d5-c${clientIndex}-s${seq}`;
        await client
          .sendToChannel('rl.soak', { sendId, clientIndex, seq })
          .packetName('rl.soak.send')
          .submit();
        sendCount += 1;
        seq += 1;
      }
    }));

    await waitFor(() => RLSoakEvidence.sends.length === sendCount, 3000);
    assert.equal(RLSoakEvidence.requests.length, requestCount);
    assert.ok(requestCount >= clientCount * 10, `RL-D5 request count too low: ${requestCount}`);
    assert.equal(new Set(RLSoakEvidence.requests.map((item) => item.requestId)).size, requestCount);
    assert.equal(new Set(RLSoakEvidence.sends.map((item) => item.sendId)).size, sendCount);
    assertSoakLatencyStable(latencies);
    marker('RL-D5');
  } finally {
    for (const app of apps.reverse()) {
      await app.close();
    }
  }
}

async function spotService() {
  const nestjs = loadNest();
  const { app } = await startApp(
    nestModule('SpotServiceModule', {
      imports: [
        nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
          .addSpotMesh('sm.play')
            .routingId('sm-node-a')
            .addEntrySpot(EntrySpot)
            .addSpotFactory(UserSpot)
            .addSpotFactory(OtherUserSpot)
            .addSpotFactory(LifecycleSpot)
            .addSpotFactory(PayloadFidelitySpot)
            .addSpotFactory(StageWrapperSpot)
            .addSpotFactory(TimerSpot)
            .addSpotFactory(IdleCloseSpot)
            .addSpotFactory(OverrunPolicySpot)
            .addSpotFactory(WorkerSpot)
            .actorFactory('sm-a6-player', LifecycleActorFactory)
          .build())
      ],
      providers: []
    })
  );

  try {
    const spotManager = app.get(nestjs.ZLINK_SPOT_MANAGER, { strict: false });
    const actorManager = app.get(nestjs.ZLINK_ACTOR_MANAGER, { strict: false });
    const created = await spotManager.create(UserSpot, { owner: 'u1' });
    assert.equal(created.state, 'created');
    assert.equal(await spotManager.close(created.spotRid), true);
    selfCheck('SM-SPOT-MANAGER-CREATE-CLOSE');

    entrySpotEvents.length = 0;
    smA1Actor = undefined;
    const entryActorRef = await actorManager.getOrCreate('sm-a1-actor', 'sm-a6-player');
    assert.equal(entryActorRef.actorId, 'sm-a1-actor');
    assert.ok(smA1Actor);
    const entryJoin = await smA1Actor.context
      .joinEntrySpot(entryActorRef.nodeRid, { spotRid: 'sm-a1-room', owner: 'entry-user' })
      .submit();
    assert.equal(entryJoin.resultCode, 0);
    assert.equal(entryJoin.actor.actorId, 'sm-a1-actor');
    assert.deepEqual(entryJoin.reply, {
      spotRid: 'sm-a1-room',
      state: 'created',
      owner: 'entry-user'
    });
    assert.deepEqual(entrySpotEvents, [
      'entry-join:sm-a1-actor:sm-a1-room',
      'user-create:sm-a1-room:entry-user',
      'entry-created:sm-a1-room:created',
      'entry-joined:sm-a1-actor'
    ]);
    assert.deepEqual(await spotManager.getOrCreate(UserSpot, 'sm-a1-room', { owner: 'ignored' }), {
      spotRid: 'sm-a1-room',
      state: 'existing'
    });
    await spotManager.close('sm-a1-room');
    selfCheck('SM-A1-ENTRY-JOIN-CREATE');

    lifecycleEvents.length = 0;
    smA6Actor = undefined;
    const lifecycle = await spotManager.getOrCreate(LifecycleSpot, 'sm-a6-spot', { owner: 'u1' });
    assert.equal(lifecycle.state, 'created');
    assert.deepEqual(lifecycleEvents, ['create:u1', 'initialize']);
    const actorRef = await actorManager.getOrCreate('sm-a6-actor', 'sm-a6-player');
    assert.equal(actorRef.actorId, 'sm-a6-actor');
    assert.ok(smA6Actor);
    const joined = await smA6Actor.context.joinSpot('sm-a6-spot', { actor: 'sm-a6-actor' }).submit();
    assert.equal(joined.resultCode, 0);
    assert.equal(await spotManager.close('sm-a6-spot'), false);
    assert.deepEqual(lifecycleEvents, ['create:u1', 'initialize', 'join:sm-a6-actor', 'joined:sm-a6-actor']);
    await smA6Actor.context.getSpot(LifecycleSpot).leave(smA6Actor);
    assert.equal(await spotManager.close('sm-a6-spot'), true);
    assert.equal(await spotManager.find('sm-a6-spot'), null);
    assert.deepEqual(lifecycleEvents, [
      'create:u1',
      'initialize',
      'join:sm-a6-actor',
      'joined:sm-a6-actor',
      'leave:sm-a6-actor',
      'closing'
    ]);
    marker('SM-A6');

    lifecycleEvents.length = 0;
    smB1Actor = undefined;
    await spotManager.getOrCreate(LifecycleSpot, 'sm-b1-spot', { owner: 'play-a' });
    const smB1ActorRef = await actorManager.getOrCreate('sm-b1-actor', 'sm-a6-player');
    assert.equal(smB1ActorRef.actorId, 'sm-b1-actor');
    assert.ok(smB1Actor);
    assert.equal(smB1Actor.context.isJoined, false);
    const localJoin = await smB1Actor.context
      .joinSpot('sm-b1-spot', { actorId: 'sm-b1-actor', node: 'play-a' })
      .submit();
    assert.equal(localJoin.resultCode, 0);
    assert.equal(localJoin.actor.actorId, 'sm-b1-actor');
    assert.equal(smB1Actor.context.isJoined, true);
    assert.equal(smB1Actor.context.spotRid, 'sm-b1-spot');
    assert.deepEqual(lifecycleEvents, [
      'create:play-a',
      'initialize',
      'join:sm-b1-actor',
      'joined:sm-b1-actor'
    ]);
    await smB1Actor.context.getSpot(LifecycleSpot).leave(smB1Actor);
    assert.equal(await spotManager.close('sm-b1-spot'), true);
    selfCheck('SM-B1-LOCAL-JOIN-CALLBACK');

    payloadFidelityEvents.length = 0;
    smB3Actor = undefined;
    await spotManager.getOrCreate(PayloadFidelitySpot, 'sm-b3-spot', { owner: 'play-a' });
    const smB3ActorRef = await actorManager.getOrCreate('sm-b3-actor', 'sm-a6-player');
    assert.equal(smB3ActorRef.actorId, 'sm-b3-actor');
    assert.ok(smB3Actor);
    const smB3JoinPayload = {
      actorId: 'sm-b3-actor',
      displayName: 'payload-user',
      level: 17,
      attributes: {
        rank: 'gold',
        region: 'kr',
        flags: { tutorialComplete: true, muted: false }
      },
      tags: ['duo', 'ranked', 'night'],
      loadout: [
        { slot: 'primary', itemId: 'bow-7', power: 91 },
        { slot: 'support', itemId: 'ward-2', power: 12 }
      ]
    };
    const smB3Join = await smB3Actor.context
      .joinSpot('sm-b3-spot', smB3JoinPayload)
      .submit();
    assert.equal(smB3Join.resultCode, 0);
    assert.equal(smB3Join.actor.actorId, 'sm-b3-actor');
    assert.deepEqual(smB3Join.reply, {
      actorId: 'sm-b3-actor',
      received: smB3JoinPayload
    });
    assert.deepEqual(payloadFidelityEvents, [
      { event: 'create', payload: { owner: 'play-a' } },
      { event: 'join', actorId: 'sm-b3-actor', payload: smB3JoinPayload },
      { event: 'joined', actorId: 'sm-b3-actor' }
    ]);
    await smB3Actor.context.getSpot(PayloadFidelitySpot).leave(smB3Actor);
    assert.equal(await spotManager.close('sm-b3-spot'), true);
    selfCheck('SM-B3-JOIN-PAYLOAD-FIDELITY');

    smB8Actor = undefined;
    lifecycleEvents.length = 0;
    const smB8ActorRef = await actorManager.getOrCreate('sm-b8-actor', 'sm-a6-player');
    assert.equal(smB8ActorRef.actorId, 'sm-b8-actor');
    assert.ok(smB8Actor);
    assert.ok(entrySpotInstance);
    assert.equal(smB8Actor.context.isJoined, false);
    await entrySpotInstance.context.destroyActor(smB8Actor);
    await entrySpotInstance.context.destroyActor(smB8Actor);
    assert.equal(await actorManager.find('sm-b8-actor'), undefined);
    assert.deepEqual(lifecycleEvents, []);
    selfCheck('SM-B8-ENTRY-DESTROY-CLEANUP');

    stageWrapperEvents.length = 0;
    await spotManager.getOrCreate(StageWrapperSpot, 'sm-a5-stage', { stageId: 'stage-17', initialScore: 3 });
    assert.deepEqual(stageWrapperEvents, [
      'stage:create:stage-17:3',
      'spot:initialize',
      'stage:initialize:stage-17'
    ]);
    const firstStageReply = await spotManager.executeOnSpot(StageWrapperSpot, 'sm-a5-stage', (spot) =>
      spot.handleStageRequest({ op: 'add', value: 4 }));
    assert.deepEqual(firstStageReply, { stageId: 'stage-17', score: 7, version: 1 });
    const secondStageReply = await spotManager.executeOnSpot(StageWrapperSpot, 'sm-a5-stage', (spot) =>
      spot.handleStageRequest({ op: 'add', value: 5 }));
    assert.deepEqual(secondStageReply, { stageId: 'stage-17', score: 12, version: 2 });
    await waitFor(() => stageWrapperEvents.some((event) => event.startsWith('stage:tick:stage-17:')));
    assert.equal(await spotManager.close('sm-a5-stage'), true);
    assert.deepEqual(stageWrapperEvents.filter((event) => event.startsWith('stage:request:')), [
      'stage:request:stage-17:add:4:7:1',
      'stage:request:stage-17:add:5:12:2'
    ]);
    assert.equal(stageWrapperEvents.filter((event) => event === 'stage:closing:stage-17:12:2').length, 1);
    marker('SM-A5');

    const first = await spotManager.getOrCreate(UserSpot, 'sm-a7-spot', { owner: 'u1' });
    assert.equal(first.state, 'created');
    await assert.rejects(
      () => spotManager.getOrCreate(OtherUserSpot, 'sm-a7-spot', { owner: 'u1' }),
      (error) =>
        error instanceof loadFramework().ZLinkFrameworkException &&
        error.kind === loadFramework().ZLinkFrameworkErrorKind.SpotTypeMismatch
    );
    const afterMismatch = await spotManager.getOrCreate(UserSpot, 'sm-a7-spot', { owner: 'u1' });
    assert.equal(afterMismatch.state, 'existing');
    marker('SM-A7');

    const ownerRoutingKeyToSpotRid = (key) => `sm-a4-owner-${Buffer.from(key).toString('hex')}`;
    const ownerAlphaRid = ownerRoutingKeyToSpotRid('order:alpha');
    const ownerAlphaAgainRid = ownerRoutingKeyToSpotRid('order:alpha');
    const ownerBetaRid = ownerRoutingKeyToSpotRid('order:beta');
    assert.equal(ownerAlphaAgainRid, ownerAlphaRid);
    assert.notEqual(ownerBetaRid, ownerAlphaRid);
    const ownerAlpha = await spotManager.getOrCreate(UserSpot, ownerAlphaRid, { owner: 'alpha-owner' });
    const ownerAlphaAgain = await spotManager.getOrCreate(UserSpot, ownerAlphaAgainRid, { owner: 'ignored-owner' });
    const ownerBeta = await spotManager.getOrCreate(UserSpot, ownerBetaRid, { owner: 'beta-owner' });
    assert.deepEqual(ownerAlpha, { spotRid: ownerAlphaRid, state: 'created', reply: { owner: 'alpha-owner' } });
    assert.deepEqual(ownerAlphaAgain, { spotRid: ownerAlphaRid, state: 'existing' });
    assert.deepEqual(ownerBeta, { spotRid: ownerBetaRid, state: 'created', reply: { owner: 'beta-owner' } });
    assert.equal(await spotManager.close(ownerAlphaRid), true);
    assert.equal(await spotManager.close(ownerBetaRid), true);
    selfCheck('SM-A4-KEY-ROUTING-MAPPING');

    workerEvents.length = 0;
    releaseWorker = undefined;
    await spotManager.getOrCreate(WorkerSpot, 'sm-a8-spot', { owner: 'u1' });
    let workerDone;
    await spotManager.executeOnSpot(WorkerSpot, 'sm-a8-spot', (spot) => {
      workerDone = spot.startWorker();
    });
    await waitFor(() => typeof releaseWorker === 'function' && workerEvents.includes('work:start'));
    await spotManager.executeOnSpot(WorkerSpot, 'sm-a8-spot', (spot) => {
      spot.recordFastPath();
    });
    assert.equal(workerEvents.includes('completed:worker-result:fast'), false);
    releaseWorker();
    await Promise.race([
      workerDone,
      new Promise((_, reject) => setTimeout(() => reject(new Error('SM-A8 worker completion timed out.')), 3000))
    ]);
    assert.deepEqual(workerEvents, [
      'scheduled',
      'work:start',
      'fast',
      'work:end',
      'completed:worker-result:fast'
    ]);
    assert.equal(await spotManager.close('sm-a8-spot'), true);
    selfCheck('SM-WORKER-OFFLOAD');

    timerEvents.length = 0;
    await spotManager.getOrCreate(TimerSpot, 'sm-e2-spot', { owner: 'u1' });
    await waitFor(() => timerEvents.filter((event) => event.startsWith('tick:')).length >= 2);
    assert.deepEqual(timerEvents.slice(0, 2), ['tick:1:1', 'tick:2:2']);
    assert.equal(await spotManager.close('sm-e2-spot'), true);
    const timerClosing = timerEvents.find((event) => event.startsWith('closing:'));
    assert.ok(timerClosing);
    assert.ok(Number(timerClosing.slice('closing:'.length)) >= 2);
    marker('SM-E2');

    idleEvents.length = 0;
    smE3Actor = undefined;
    await spotManager.getOrCreate(IdleCloseSpot, 'sm-e3-spot', { owner: 'u1' });
    const idleActorRef = await actorManager.getOrCreate('sm-e3-actor', 'sm-a6-player');
    assert.equal(idleActorRef.actorId, 'sm-e3-actor');
    assert.ok(smE3Actor);
    const idleJoined = await smE3Actor.context.joinSpot('sm-e3-spot', { actor: 'sm-e3-actor' }).submit();
    assert.equal(idleJoined.resultCode, 0);
    await waitFor(() => idleEvents.includes('close-with-actor:false'));
    assert.deepEqual(await spotManager.find('sm-e3-spot'), { spotRid: 'sm-e3-spot' });
    await smE3Actor.context.getSpot(IdleCloseSpot).leave(smE3Actor);
    await waitFor(async () => (await spotManager.find('sm-e3-spot')) === null && idleEvents.includes('closing'));
    assert.deepEqual(idleEvents, [
      'join:sm-e3-actor',
      'activity',
      'close-with-actor:false',
      'leave:sm-e3-actor',
      'close-after-leave:true',
      'closing'
    ]);
    marker('SM-E3');

    overrunEvents.clear();
    await spotManager.getOrCreate(OverrunPolicySpot, 'sm-e4-skip', { policy: 'skipLateTicks' });
    await spotManager.getOrCreate(OverrunPolicySpot, 'sm-e4-catch', { policy: 'catchUpBounded' });
    await spotManager.getOrCreate(OverrunPolicySpot, 'sm-e4-delay', { policy: 'delayNextTick' });
    await waitFor(() =>
      overrunEvents.get('skipLateTicks')?.length >= 2 &&
      overrunEvents.get('catchUpBounded')?.length >= 3 &&
      overrunEvents.get('delayNextTick')?.length >= 2,
    3000);
    const skipLateTicks = overrunEvents.get('skipLateTicks');
    const catchUpTicks = overrunEvents.get('catchUpBounded');
    const delayNextTicks = overrunEvents.get('delayNextTick');
    const skipLateDueIndex = BigInt(Math.max(1, Math.floor(skipLateTicks[1].startedElapsedMs / skipLateTicks[1].periodMs)));
    const catchUpDueIndex = BigInt(Math.max(1, Math.floor(catchUpTicks[1].startedElapsedMs / catchUpTicks[1].periodMs)));
    assert.ok(skipLateTicks[1].scheduledIndex > skipLateTicks[0].scheduledIndex + 1n);
    assert.equal(skipLateTicks[1].scheduledIndex, skipLateDueIndex);
    assert.ok(skipLateTicks[1].skippedTicks > 0n);
    assert.ok(catchUpDueIndex - catchUpTicks[0].scheduledIndex > 2n);
    assert.equal(catchUpTicks[1].scheduledIndex, catchUpDueIndex - 1n);
    assert.equal(catchUpTicks[1].skippedTicks, catchUpTicks[1].scheduledIndex - catchUpTicks[0].scheduledIndex - 1n);
    assert.ok(catchUpTicks[2].scheduledIndex === catchUpTicks[1].scheduledIndex + 1n);
    assert.equal(catchUpTicks[2].skippedTicks, 0n);
    assert.equal(delayNextTicks[1].scheduledIndex, delayNextTicks[0].scheduledIndex + 1n);
    assert.equal(delayNextTicks[1].skippedTicks, 0n);
    assert.ok(
      delayNextTicks[1].startedElapsedMs - delayNextTicks[0].startedElapsedMs
      >= overrunFirstTickDelayMs + delayNextTicks[1].periodMs
    );
    await spotManager.close('sm-e4-skip');
    await spotManager.close('sm-e4-catch');
    await spotManager.close('sm-e4-delay');
    marker('SM-E4');
  } finally {
    await app.close();
  }
}

class EchoHandler {
  static completed = [];
  async handle(payload, context) {
    if (payload.text === 'slow') {
      await new Promise((resolve) => setTimeout(resolve, 300));
    }
    EchoHandler.completed.push(payload);
    return { ...payload, handledBy: context.routingId ?? 'rm-provider-a' };
  }
}

class RLObserverFailures {
  static events = [];
}

class RLRestartHandler {
  static requests = [];

  static record(payload, generation) {
    const observed = { ...payload, handledBy: 'provider-a', generation };
    RLRestartHandler.requests.push(observed);
    return observed;
  }
}

function createRLRestartHandler(generation) {
  return class {
    handle(payload) {
      return RLRestartHandler.record(payload, generation);
    }
  };
}

class RLReconnectHandler {
  static requests = [];

  handle(payload) {
    const observed = { ...payload, handledBy: 'rl-reconnect-server' };
    RLReconnectHandler.requests.push(observed);
    return observed;
  }
}

class RLInFlightStableHandler {
  handle(payload) {
    return { ...payload, handledBy: 'provider-a' };
  }
}

class RLRescheduleHandler {
  static requests = [];

  static record(payload, handledBy) {
    const observed = { ...payload, handledBy };
    RLRescheduleHandler.requests.push(observed);
    return observed;
  }
}

function createRLRescheduleHandler(providerId) {
  return class {
    handle(payload) {
      return RLRescheduleHandler.record(payload, providerId);
    }
  };
}

function createRLChildProviderHandler(providerId, blockRequestIds) {
  return class {
    async handle(payload) {
      if (typeof process.send === 'function') {
        process.send({ type: 'handling', requestId: payload.requestId, handledBy: providerId });
      }
      if (blockRequestIds.has(payload.requestId)) {
        await new Promise(() => {});
      }
      return { ...payload, handledBy: providerId };
    }
  };
}

class RLFlapHandler {
  static requests = [];

  static record(payload, handledBy) {
    const observed = { ...payload, handledBy };
    RLFlapHandler.requests.push(observed);
    return observed;
  }
}

function createRLFlapHandler(providerId) {
  return class {
    handle(payload) {
      return RLFlapHandler.record(payload, providerId);
    }
  };
}

class RLGrayFailureHandler {
  static requests = [];
}

function createRLGrayFailureHandler(providerId, gray) {
  return class {
    async handle(payload) {
      if (gray && payload.delayOnGray === true) {
        RLGrayFailureHandler.requests.push({ ...payload, handledBy: providerId, delayed: true });
        await new Promise((resolve) => setTimeout(resolve, 300));
        return { ...payload, handledBy: providerId, delayed: true };
      }
      const observed = { ...payload, handledBy: providerId };
      RLGrayFailureHandler.requests.push(observed);
      return observed;
    }
  };
}

class RLGracefulHandler {
  static requests = [];

  static record(payload, handledBy) {
    const observed = { ...payload, handledBy };
    RLGracefulHandler.requests.push(observed);
    return observed;
  }
}

function createRLGracefulHandler(providerId) {
  return class {
    handle(payload) {
      return RLGracefulHandler.record(payload, providerId);
    }
  };
}

function createRLStaleHandler(providerId) {
  return class {
    handle(payload) {
      return { ...payload, handledBy: providerId };
    }
  };
}

class RLThrowingFlowObserver {
  onMessageFlow(flow) {
    if (flow.packetName === 'rl.missing') {
      throw new Error('rl observer failure');
    }
  }
}

class RLLoggingObserver {
  static logFile;

  onMessageFlow(flow) {
    if (flow.outcome !== 'error') {
      return;
    }
    fs.appendFileSync(
      RLLoggingObserver.logFile,
      [
        'message-flow-error',
        `reason=${flow.errorReason}`,
        `action=${flow.errorAction}`,
        `packetName=${flow.packetName}`,
        `channelName=${flow.channelName}`
      ].join(' ') + '\n'
    );
  }
}

class RLSoakEvidence {
  static requests = [];
  static sends = [];
}

class RLSoakRequestHandler {
  handle(payload, context) {
    const observed = { ...payload, handledBy: context.routingId ?? 'rl-soak-provider' };
    RLSoakEvidence.requests.push(observed);
    return observed;
  }
}

class RLSoakSendHandler {
  handle(payload) {
    RLSoakEvidence.sends.push(payload);
  }
}

class RLFanoutEvidence {
  static events = [];
}

function createRLFanoutHandler(subscriberId) {
  return class ResilienceHighFanoutHandler {
    handle(payload, context) {
      if (context.topic === 'rl-d1') {
        RLFanoutEvidence.events.push({ subscriberId, seq: payload.seq });
      }
    }
  };
}

function hasRLFanoutSequence(subscriberCount, sequence) {
  for (let subscriberId = 0; subscriberId < subscriberCount; subscriberId += 1) {
    const actual = RLFanoutEvidence.events
      .filter((event) => event.subscriberId === subscriberId)
      .map((event) => event.seq);
    if (!sequence.every((seq) => actual.includes(seq))) {
      return false;
    }
  }
  return true;
}

class AuditHandler {
  static messages = [];
  handle(payload) {
    AuditHandler.messages.push(payload);
  }
}

class DRHandler {
  static requests = [];
  handle(payload, context) {
    const handledBy = context.routingId ?? 'dr-provider';
    DRHandler.requests.push({ ...payload, handledBy });
    return { ...payload, handledBy };
  }
}

class RMFlowObserver {
  static events = [];
  onMessageFlow(event) {
    RMFlowObserver.events.push(event);
  }
}

class RMDiscoveryHandler {
  static requests = [];
  static commands = [];
  static record(payload, handledBy) {
    RMDiscoveryHandler.requests.push({ ...payload, handledBy });
    return { ...payload, handledBy };
  }
  static recordCommand(payload, handledBy) {
    RMDiscoveryHandler.commands.push({ ...payload, handledBy });
  }
}

function createRMDiscoveryHandler(routingId) {
  return class {
    async handle(payload, context) {
      if (context.packetName === 'rm.discovery.command') {
        RMDiscoveryHandler.recordCommand(payload, routingId);
        return undefined;
      }
      if (payload.text === 'slow') {
        await new Promise((resolve) => setTimeout(resolve, 1000));
      }
      return RMDiscoveryHandler.record(payload, routingId);
    }
  };
}

class RMFailoverHandler {
  static requests = [];
  static record(payload, handledBy) {
    RMFailoverHandler.requests.push({ ...payload, handledBy });
    return { ...payload, handledBy };
  }
}

function createRMFailoverHandler(providerId) {
  return class {
    handle(payload) {
      return RMFailoverHandler.record(payload, providerId);
    }
  };
}

class RMCrossChannelHandler {
  static requests = [];
  static record(payload, handledBy, channelName) {
    const observed = { ...payload, handledBy, channelName };
    RMCrossChannelHandler.requests.push(observed);
    return observed;
  }
}

function createRMCrossChannelHandler(providerId) {
  return class {
    handle(payload, context) {
      return RMCrossChannelHandler.record(payload, providerId, context.channelName);
    }
  };
}

class RMScaleHandler {
  static requests = [];
  static record(payload, handledBy) {
    const observed = { ...payload, handledBy };
    RMScaleHandler.requests.push(observed);
    return observed;
  }
}

function createRMScaleHandler(providerId) {
  return class {
    handle(payload) {
      return RMScaleHandler.record(payload, providerId);
    }
  };
}

class RMRouteHandler {
  static requests = [];
  static record(payload, handledBy, sourceNodeRid) {
    const observed = { ...payload, handledBy, sourceNodeRid };
    RMRouteHandler.requests.push(observed);
    return observed;
  }
}

function createRMRouteHandler(providerId) {
  return class {
    handle(payload, context) {
      return RMRouteHandler.record(payload, providerId, context.sourceNodeRid);
    }
  };
}

class RMManualDistributionHandler {
  static requests = [];
  static record(payload, handledBy) {
    RMManualDistributionHandler.requests.push({ ...payload, handledBy });
    return { ...payload, handledBy };
  }
}

function createRMManualDistributionHandler(routingId) {
  return class {
    handle(payload) {
      return RMManualDistributionHandler.record(payload, routingId);
    }
  };
}

class PubSubAlphaHandler {
  static events = [];
  handle(payload, context) {
    if (context.topic === 'all' || context.topic === 'alpha') {
      PubSubAlphaHandler.events.push({ topic: context.topic, seq: payload.seq });
    }
  }
}

class PubSubBetaHandler {
  static events = [];
  handle(payload, context) {
    if (context.topic === 'all' || context.topic === 'beta') {
      PubSubBetaHandler.events.push({ topic: context.topic, seq: payload.seq });
    }
  }
}

class PubSubGammaHandler {
  static events = [];
  handle(payload, context) {
    if (context.topic === 'all' || context.topic === 'alpha' || context.topic === 'gamma') {
      PubSubGammaHandler.events.push({ topic: context.topic, seq: payload.seq });
    }
  }
}

class PubSubLateHandler {
  static events = [];
  handle(payload, context) {
    if (context.topic === 'all') {
      PubSubLateHandler.events.push({ topic: context.topic, seq: payload.seq });
    }
  }
}

class PubSubSlowHandler {
  static started = [];
  static events = [];
  async handle(payload, context) {
    if (context.topic !== 'all') {
      return;
    }
    PubSubSlowHandler.started.push({ topic: context.topic, seq: payload.seq });
    await new Promise((resolve) => setTimeout(resolve, 1000));
    PubSubSlowHandler.events.push({ topic: context.topic, seq: payload.seq });
  }
}

class PubSubFlowObserver {
  static events = [];
  onMessageFlow(flow) {
    PubSubFlowObserver.events.push(flow);
  }
}

class RcDecoratedRequestHandler {
  static requests = [];
  handle(payload, context) {
    RcDecoratedRequestHandler.requests.push({ ...payload, contentType: context.contentType });
    return { ...payload, contentType: context.contentType };
  }
}

class RcDecoratedSendHandler {
  static messages = [];
  handle(payload, context) {
    RcDecoratedSendHandler.messages.push({ ...payload, contentType: context.contentType });
  }
}

class RcManualRequestHandler {
  static requests = [];
  handle(payload, context) {
    RcManualRequestHandler.requests.push({ ...payload, contentType: context.contentType });
    if (payload.id === 25) {
      RcFilterOrder.events.push('handler');
    }
    return { ...payload, contentType: context.contentType };
  }
}

class RcManualSendHandler {
  static messages = [];
  handle(payload, context) {
    RcManualSendHandler.messages.push({ ...payload, contentType: context.contentType });
  }
}

class RcCodecHandler {
  static calls = [];
  handle(payload, context) {
    RcCodecHandler.calls.push({
      id: payload.id,
      codec: payload.codec,
      contentType: context.contentType
    });
    return { ...payload, contentType: context.contentType };
  }
}

class RcCodecSendHandler {
  static messages = [];
  handle(payload, context) {
    RcCodecSendHandler.messages.push({
      id: payload.id,
      codec: payload.codec,
      contentType: context.contentType
    });
  }
}

class RcFilterOrder {
  static events = [];
}

class RcFirstFilter {
  async invoke(invocation, next) {
    if (invocation.context.packetName === 'rc.echo.manual' && invocation.message.id === 25) {
      assert.equal(invocation.channelName, 'rc.api');
      assert.equal(invocation.packetName, 'rc.echo.manual');
      assert.equal(invocation.message.id, 25);
      RcFilterOrder.events.push('first:before');
    }
    const reply = await next();
    if (invocation.context.packetName === 'rc.echo.manual' && invocation.message.id === 25) {
      RcFilterOrder.events.push('first:after');
    }
    return reply;
  }
}

class RcSecondFilter {
  async invoke(invocation, next) {
    if (invocation.context.packetName === 'rc.echo.manual' && invocation.message.id === 25) {
      assert.equal(invocation.channelName, 'rc.api');
      assert.equal(invocation.packetName, 'rc.echo.manual');
      assert.equal(invocation.message.codec, 'json');
      RcFilterOrder.events.push('second:before');
    }
    const reply = await next();
    if (invocation.context.packetName === 'rc.echo.manual' && invocation.message.id === 25) {
      RcFilterOrder.events.push('second:after');
    }
    return reply;
  }
}

function applyRcDecorators() {
  if (applyRcDecorators.applied === true) {
    return;
  }
  const nestjs = loadNest();
  nestjs.zlinkRequestHandler('rc-decorated', 'rc.echo.decorated')(RcDecoratedRequestHandler);
  nestjs.zlinkSendHandler('rc-decorated', 'rc.audit.decorated')(RcDecoratedSendHandler);
  applyRcDecorators.applied = true;
}

async function runRegistrationCodecAutoDiscovery(nestjs) {
  const discoveryRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'zlink-node-rc-auto-'));
  const fixturePath = path.join(discoveryRoot, 'auto-handlers.js');
  fs.writeFileSync(fixturePath, `
const nestjs = require(${JSON.stringify(path.join(nodeRoot, 'packages/nestjs/dist'))});

const events = { requests: [], messages: [] };

class AutoRequestHandler {
  handle(payload, context) {
    events.requests.push({ ...payload, contentType: context.contentType });
    return { ...payload, source: 'auto' };
  }
}

class AutoSendHandler {
  handle(payload, context) {
    events.messages.push({ ...payload, contentType: context.contentType });
  }
}

nestjs.zlinkRequestHandler('rc-auto', 'rc.echo.auto')(AutoRequestHandler);
nestjs.zlinkSendHandler('rc-auto', 'rc.audit.auto')(AutoSendHandler);

module.exports = { AutoRequestHandler, AutoSendHandler, events };
`);
  const endpoint = uniqueEndpoint('rc-auto');
  class RegistrationCodecAutoModule {}
  nestjs.zlinkModule({
    imports: [
      nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
        .codecs().addJson()
        .addClientServerChannel('rc.auto')
          .enableServer(endpoint)
          .enableClient(endpoint)
          .addHandlerGroup('rc-auto')
        .build())
    ],
    providerDiscovery: [discoveryRoot]
  })(RegistrationCodecAutoModule);

  const { app } = await startApp(RegistrationCodecAutoModule);
  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT, { strict: false });
    const reply = await client
      .requestToChannel('rc.auto', { id: 11, codec: 'json' })
      .packetName('rc.echo.auto')
      .submit();
    assert.deepEqual(reply, { id: 11, codec: 'json', source: 'auto' });
    await client
      .sendToChannel('rc.auto', { id: 12, codec: 'json' })
      .packetName('rc.audit.auto')
      .submit();
    const fixture = require(fixturePath);
    await waitFor(() => fixture.events.messages.some((message) => message.id === 12));
    assert.equal(fixture.events.requests.length, 1);
    marker('RC-A1');
  } finally {
    await app.close();
  }
}

async function assertInvalidRegistration(nestjs) {
  await assert.rejects(
    async () => {
      const { app } = await startApp(
        nestModule('RegistrationCodecInvalidDuplicateModule', {
          imports: [
            nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
              .addClientServerChannel('rc.invalid.duplicate')
                .enableServer(uniqueEndpoint('rc-invalid-duplicate'))
                .addRequestHandler('rc.duplicate', RcManualRequestHandler)
                .addRequestHandler('rc.duplicate', RcDecoratedRequestHandler)
              .build())
          ],
          providers: [RcManualRequestHandler, RcDecoratedRequestHandler]
        })
      );
      await app.close();
    },
    /duplicate|same|rc\.duplicate/i
  );
  assert.throws(
    () => nestjs.zlinkRequestHandler('', 'rc.invalid.group')(class InvalidGroupHandler {}),
    /handler group|empty/i
  );
  await assert.rejects(
    async () => {
      const { app } = await startApp(
        nestModule('RegistrationCodecInvalidChannelKindModule', {
          imports: [
            nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
              .addFanoutChannel('rc.invalid.kind')
                .enablePublisher(uniqueEndpoint('rc-invalid-kind'))
                .addPublishHandler('rc.invalid.publish', PubSubAlphaHandler)
              .build())
          ],
          providers: [PubSubAlphaHandler]
        })
      );
      await app.close();
    },
    /publish handlers require a subscriber|subscriber capability|rc\.invalid\.kind/i
  );
}

const entrySpotEvents = [];
let entrySpotInstance;

class EntrySpot {
  constructor(spots) {
    this.spots = spots;
    entrySpotInstance = this;
  }

  async onActorJoin(actor, request) {
    const payload = request.decode();
    entrySpotEvents.push(`entry-join:${actor.actorId}:${payload.spotRid}`);
    const created = await this.spots.getOrCreate(UserSpot, payload.spotRid, { owner: payload.owner });
    entrySpotEvents.push(`entry-created:${created.spotRid}:${created.state}`);
    return {
      accepted: true,
      reply: {
        spotRid: created.spotRid,
        state: created.state,
        owner: created.reply?.owner
      }
    };
  }

  async onJoinedActor(actor) {
    if (actor.actorId === 'sm-b8-actor') {
      lifecycleEvents.push(`entry-joined:${actor.actorId}`);
    }
    entrySpotEvents.push(`entry-joined:${actor.actorId}`);
  }

  async onCreateActor(actor) {
    if (actor.actorId === 'sm-b8-actor') {
      lifecycleEvents.push(`entry-created:${actor.actorId}`);
    }
  }
}
Inject(nestjsPublic.ZLINK_SPOT_MANAGER)(EntrySpot, undefined, 0);

class UserSpot {
  async onCreate(request) {
    const payload = request.decode();
    this.owner = payload.owner;
    if (payload.owner === 'entry-user') {
      entrySpotEvents.push(`user-create:${this.context.spotRid}:${this.owner}`);
    }
    return { accepted: true, reply: { owner: this.owner } };
  }
}

class OtherUserSpot {}

const lifecycleEvents = [];
let smA1Actor;
let smA6Actor;
let smB1Actor;
let smB3Actor;
let smB8Actor;
let smE3Actor;

class LifecycleSpot {
  constructor(context) {
    this.context = context;
  }

  async onCreate(request) {
    lifecycleEvents.push(`create:${request.decode().owner}`);
    return { accepted: true };
  }

  async onInitialize() {
    lifecycleEvents.push('initialize');
  }

  async onActorJoin(actor) {
    lifecycleEvents.push(`join:${actor.actorId}`);
    return { accepted: true };
  }

  async onJoinedActor(actor) {
    lifecycleEvents.push(`joined:${actor.actorId}`);
  }

  async onLeaveActor(actor) {
    lifecycleEvents.push(`leave:${actor.actorId}`);
  }

  async onClosing() {
    lifecycleEvents.push('closing');
  }

  leave(actor) {
    return this.context.leaveActor(actor);
  }
}

class LifecycleActor {
  constructor(actorId, context) {
    this.actorId = actorId;
    this.context = context;
  }
}

class LifecycleActorFactory {
  create(actorId, context) {
    const actor = new LifecycleActor(actorId, context);
    if (actorId === 'sm-a6-actor') {
      smA6Actor = actor;
    }
    if (actorId === 'sm-a1-actor') {
      smA1Actor = actor;
    }
    if (actorId === 'sm-b1-actor') {
      smB1Actor = actor;
    }
    if (actorId === 'sm-b3-actor') {
      smB3Actor = actor;
    }
    if (actorId === 'sm-b8-actor') {
      smB8Actor = actor;
    }
    if (actorId === 'sm-e3-actor') {
      smE3Actor = actor;
    }
    return actor;
  }
}

const payloadFidelityEvents = [];

class PayloadFidelitySpot {
  constructor(context) {
    this.context = context;
  }

  async onCreate(request) {
    const payload = request.decode();
    payloadFidelityEvents.push({ event: 'create', payload });
    return { accepted: true };
  }

  async onActorJoin(actor, request) {
    const payload = request.decode();
    payloadFidelityEvents.push({ event: 'join', actorId: actor.actorId, payload });
    return {
      accepted: true,
      reply: {
        actorId: actor.actorId,
        received: payload
      }
    };
  }

  async onJoinedActor(actor) {
    payloadFidelityEvents.push({ event: 'joined', actorId: actor.actorId });
  }

  leave(actor) {
    return this.context.leaveActor(actor);
  }
}

const stageWrapperEvents = [];

class StageModel {
  constructor(stageId, score) {
    this.stageId = stageId;
    this.score = score;
    this.version = 0;
    stageWrapperEvents.push(`stage:create:${this.stageId}:${this.score}`);
  }

  initialize() {
    stageWrapperEvents.push(`stage:initialize:${this.stageId}`);
  }

  apply(command) {
    if (command.op !== 'add') {
      throw new Error(`unsupported stage op: ${command.op}`);
    }
    this.score += command.value;
    this.version += 1;
    stageWrapperEvents.push(`stage:request:${this.stageId}:${command.op}:${command.value}:${this.score}:${this.version}`);
    return {
      stageId: this.stageId,
      score: this.score,
      version: this.version
    };
  }

  tick(tick) {
    stageWrapperEvents.push(`stage:tick:${this.stageId}:${tick.deliveryIndex}:${this.score}:${this.version}`);
  }

  closing() {
    stageWrapperEvents.push(`stage:closing:${this.stageId}:${this.score}:${this.version}`);
  }
}

class StageWrapperSpot {
  constructor(context) {
    this.context = context;
  }

  onCreate(request) {
    const payload = request.decode();
    this.stage = new StageModel(payload.stageId, payload.initialScore);
    return { accepted: true };
  }

  async onInitialize() {
    stageWrapperEvents.push('spot:initialize');
    this.stage.initialize();
    await this.context.addTimer('sm-a5-stage-tick', 10, StageWrapperTimerHandler);
  }

  handleStageRequest(command) {
    return this.stage.apply(command);
  }

  async onClosing() {
    this.stage.closing();
  }
}

class StageWrapperTimerHandler {
  async handle(spot, tick) {
    spot.stage.tick(tick);
  }
}

const timerEvents = [];

class TimerSpot {
  constructor(context) {
    this.context = context;
    this.count = 0;
  }

  async onInitialize() {
    await this.context.addTimer('sm-e2-heartbeat', 10, TimerTickHandler);
  }

  async onClosing() {
    timerEvents.push(`closing:${this.count}`);
  }
}

class TimerTickHandler {
  async handle(spot, tick) {
    spot.count += 1;
    timerEvents.push(`tick:${tick.deliveryIndex}:${spot.count}`);
  }
}

const idleEvents = [];

class IdleCloseSpot {
  constructor(context) {
    this.context = context;
    this.actorJoined = false;
    this.activityObserved = false;
    this.closeWhileJoinedObserved = false;
    this.leaveObserved = false;
  }

  async onInitialize() {
    await this.context.addTimer('sm-e3-idle-close', 10, IdleCloseTimerHandler);
  }

  async onActorJoin(actor) {
    this.actorJoined = true;
    idleEvents.push(`join:${actor.actorId}`);
    return { accepted: true };
  }

  async onLeaveActor(actor) {
    this.leaveObserved = true;
    idleEvents.push(`leave:${actor.actorId}`);
  }

  async onClosing() {
    idleEvents.push('closing');
  }

  leave(actor) {
    return this.context.leaveActor(actor);
  }
}

class IdleCloseTimerHandler {
  async handle(spot) {
    if (!spot.actorJoined) {
      return;
    }
    if (!spot.activityObserved) {
      spot.activityObserved = true;
      idleEvents.push('activity');
      return;
    }
    if (!spot.leaveObserved) {
      if (spot.closeWhileJoinedObserved) {
        return;
      }
      spot.closeWhileJoinedObserved = true;
      const closed = await spot.context.close();
      idleEvents.push(`close-with-actor:${closed}`);
      return;
    }
    const closed = await spot.context.close();
    idleEvents.push(`close-after-leave:${closed}`);
  }
}

const overrunEvents = new Map();
const overrunFirstTickDelayMs = 35;

class OverrunPolicySpot {
  constructor(context) {
    this.context = context;
  }

  onCreate(request) {
    this.policy = request.decode().policy;
    overrunEvents.set(this.policy, []);
    return { accepted: true };
  }

  async onInitialize() {
    const framework = loadFramework();
    const timerOptions = {
      overrunPolicy: framework.ZLinkTimerOverrunPolicy[this.policy === 'skipLateTicks'
        ? 'SkipLateTicks'
        : this.policy === 'catchUpBounded'
          ? 'CatchUpBounded'
          : 'DelayNextTick'],
      maxCatchUpTicks: 2
    };
    await this.context.addTimer(`sm-e4-${this.policy}`, 10, OverrunPolicyTimerHandler, timerOptions);
  }
}

class OverrunPolicyTimerHandler {
  async handle(spot, tick) {
    const events = overrunEvents.get(spot.policy);
    events.push({
      scheduledIndex: tick.scheduledIndex,
      startedElapsedMs: tick.startedElapsedMs,
      periodMs: tick.periodMs,
      skippedTicks: tick.skippedTicks
    });
    if (events.length === 1) {
      await new Promise((resolve) => setTimeout(resolve, overrunFirstTickDelayMs));
    }
  }
}

const workerEvents = [];
let releaseWorker;

class WorkerSpot {
  constructor(context) {
    this.context = context;
    this.state = [];
  }

  startWorker() {
    let resolveCompletion;
    let rejectCompletion;
    const completion = new Promise((resolve, reject) => {
      resolveCompletion = resolve;
      rejectCompletion = reject;
    });
    this.context.runWorker(async () => {
      workerEvents.push('work:start');
      await new Promise((resolve) => {
        releaseWorker = resolve;
      });
      workerEvents.push('work:end');
      return 'worker-result';
    }).onCompleted((result) => {
      workerEvents.push(`completed:${result}:${this.state.join(',')}`);
      this.state.push(result);
      resolveCompletion();
    }, (error) => {
      rejectCompletion(error);
    });
    workerEvents.push('scheduled');
    return completion;
  }

  recordFastPath() {
    this.state.push('fast');
    workerEvents.push('fast');
  }
}

async function startApp(rootModule) {
  const { NestFactory } = require('@nestjs/core');
  const app = await NestFactory.createApplicationContext(rootModule, {
    logger: false,
    abortOnError: false
  });
  return { app };
}

function loadNest() {
  return require(path.join(nodeRoot, 'packages/nestjs/dist'));
}

function loadFramework() {
  return require(path.join(nodeRoot, 'packages/framework/dist'));
}

function loadFrameworkProtobuf() {
  return require(path.join(nodeRoot, 'packages/framework-codec-protobuf/dist'));
}

function loadFrameworkMessagePack() {
  return require(path.join(nodeRoot, 'packages/framework-codec-msgpack/dist'));
}

function nestModule(name, metadata) {
  const { Module } = require('@nestjs/common');
  const moduleClass = { [name]: class {} }[name];
  Module(metadata)(moduleClass);
  return moduleClass;
}

function marker(id) {
  console.log(`scenario ${id} passed`);
}

function selfCheck(id) {
  console.log(`self-check ${id} passed`);
}

async function runScenario(id, scenario) {
  const selected = selectedScenarioIds();
  if (selected.size > 0 && !selected.has(id)) {
    return;
  }
  await scenario();
}

function selectedScenarioIds() {
  const value = process.env.NODE_E2E_SCENARIOS ?? '';
  return new Set(value
    .split(',')
    .map((item) => item.trim())
    .filter((item) => item.length > 0));
}

async function startPubSubSubscriber(moduleName, handlerType, endpoint) {
  const nestjs = loadNest();
  const framework = loadFramework();
  const builder = nestjs.zlinkFramework();
  builder.configureDispatch()
    .setMessageFlowObserver(PubSubFlowObserver)
    .messageFlow(framework.ZLinkMessageFlowLogMode.ErrorsOnly)
    .traceLogFile(path.join(os.tmpdir(), `zlink-node-ps-flow-${process.pid}.log`));
  builder.addFanoutChannel('ps.events')
    .enableSubscriber(endpoint)
    .addPublishHandler('ps.event', handlerType);
  return await startApp(
    nestModule(moduleName, {
      imports: [
        nestjs.ZLinkModule.forRoot(builder.build())
      ],
      providers: [handlerType, PubSubFlowObserver]
    })
  );
}

async function publishUntil(fanout, topic, seq, predicate) {
  for (let attempt = 0; attempt < 100; attempt += 1) {
    await publishEvent(fanout, topic, seq);
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 30));
  }
  throw new Error(`Timed out waiting for fanout subscribers on topic ${topic}.`);
}

async function publishEvent(fanout, topic, seq) {
  await fanout
    .publishToChannel('ps.events', topic, { seq })
    .packetName('ps.event')
    .submit();
}

function commonSequence(eventGroups, topic, sequence) {
  return eventGroups.every((events) => {
    const actual = events
      .filter((event) => event.topic === topic && event.seq > 0)
      .map((event) => event.seq);
    return containsSequence(actual, sequence);
  });
}

function containsSequence(actual, expected) {
  for (let start = 0; start <= actual.length - expected.length; start += 1) {
    if (expected.every((seq, index) => actual[start + index] === seq)) {
      return true;
    }
  }
  return false;
}

function hasOnlyTopics(events, topics) {
  const allowed = new Set(topics);
  return events.every((event) => allowed.has(event.topic));
}

function uniqueEndpoint(label) {
  return `inproc://node-e2e-${label}-${process.pid}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

async function reserveTcpEndpoint() {
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const address = server.address();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.close(resolve);
  });
  return `tcp://127.0.0.1:${address.port}`;
}

async function waitFor(predicate, timeoutMs = 3000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
  throw new Error('Timed out waiting for Node e2e condition.');
}

async function withTimeout(operation, timeoutMs, message) {
  let timeout;
  try {
    return await Promise.race([
      operation(),
      new Promise((_, reject) => {
        timeout = setTimeout(() => reject(new Error(message)), timeoutMs);
      })
    ]);
  } finally {
    clearTimeout(timeout);
  }
}

async function submitUntilReachable(submit, timeoutMs = 5000) {
  const deadline = Date.now() + timeoutMs;
  let lastError;
  while (Date.now() < deadline) {
    try {
      return await submit();
    } catch (error) {
      if (!isTransientMessagingConnectError(error)) {
        throw error;
      }
      lastError = error;
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
  }
  throw lastError;
}

function isTransientMessagingConnectError(error) {
  const framework = loadFramework();
  if (error instanceof framework.ZLinkFrameworkException) {
    return error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected && error.isRetriable === true;
  }
  return error instanceof Error &&
    (((error.code === 2 || error.code === 12) && /Host unreachable/.test(error.message)) ||
      (error.code === 5 && /Connection refused/.test(error.message)));
}

function isPublicInFlightCrashError(error) {
  const framework = loadFramework();
  if (error instanceof framework.ZLinkFrameworkException) {
    return [
      framework.ZLinkFrameworkErrorKind.RouteNotConnected,
      framework.ZLinkFrameworkErrorKind.RequestTargetNotFound,
      framework.ZLinkFrameworkErrorKind.RequestRejected,
      framework.ZLinkFrameworkErrorKind.RequestFailed
    ].includes(error.kind);
  }
  return error instanceof Error && /ZLink async submit timed out/i.test(error.message);
}

function isPublicGrayFailureError(error) {
  const framework = loadFramework();
  if (error instanceof framework.ZLinkFrameworkException) {
    return error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected &&
      /timed out|timeout|result 101|request failed/i.test(error.message);
  }
  return error instanceof Error && /timed out|timeout|result 101|async submit timed out/i.test(error.message);
}

function assertSoakLatencyStable(latencies) {
  assert.ok(latencies.length >= 10, `RL-D5 latency sample count too low: ${latencies.length}`);
  const midpoint = Math.floor(latencies.length / 2);
  const firstHalf = latencies.slice(0, midpoint);
  const secondHalf = latencies.slice(midpoint);
  const firstAverage = average(firstHalf);
  const secondAverage = average(secondHalf);
  const maxLatency = Math.max(...latencies);
  assert.ok(maxLatency < 1000, `RL-D5 max latency too high: ${maxLatency}ms`);
  assert.ok(
    secondAverage <= Math.max(firstAverage * 4, firstAverage + 100),
    `RL-D5 latency drift too high: first=${firstAverage}ms second=${secondAverage}ms`
  );
}

function activeTcpHandleCount() {
  if (typeof process._getActiveHandles !== 'function') {
    return 0;
  }
  return process._getActiveHandles().filter((handle) =>
    handle instanceof net.Server ||
    (handle instanceof net.Socket && (handle.localAddress !== undefined || handle.remoteAddress !== undefined))
  ).length;
}

function average(values) {
  return values.reduce((total, value) => total + value, 0) / values.length;
}

function countBy(values) {
  const counts = {};
  for (const value of values) {
    counts[value] = (counts[value] ?? 0) + 1;
  }
  return counts;
}

function stringifyDiagnostic(value) {
  return JSON.stringify(value, (_key, item) =>
    typeof item === 'bigint' ? item.toString() : item);
}

main().catch((error) => {
  console.error(error && error.stack ? error.stack : error);
  process.exit(1);
});
