#!/usr/bin/env node
'use strict';

require('reflect-metadata');

const fs = require('node:fs/promises');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, ZLINK_CHANNEL_CLIENT, ZLINK_ROUTE_CLIENT, zlinkFramework } = require('@zlink-systems/nestjs');
const { ZLinkHttpClient } = require('@zlink-systems/http-client');

async function main() {
  const [role, ...rest] = process.argv.slice(2);
  if (role === undefined) {
    throw new Error('scenario E2E role is required.');
  }
  const options = parseOptions(rest);
  if (role === 'server') {
    await runServer(options);
    return;
  }
  if (role === 'client') {
    await runClient(options);
    return;
  }
  throw new Error(`Unknown scenario E2E role '${role}'.`);
}

async function runServer(options) {
  await fs.mkdir(options.workDir, { recursive: true });
  const evidence = {
    requestIds: [],
    commandIds: [],
    completedRequestIds: [],
    routeRequestIds: [],
    routeSourceRids: [],
    dispatchErrors: [],
    serverId: options.serverName
  };

  class ScenarioEvidenceStore {
    constructor() {
      this.requestIds = evidence.requestIds;
      this.commandIds = evidence.commandIds;
      this.completedRequestIds = evidence.completedRequestIds;
      this.routeRequestIds = evidence.routeRequestIds;
      this.routeSourceRids = evidence.routeSourceRids;
      this.dispatchErrors = evidence.dispatchErrors;
    }
  }

  class ScenarioProfileHandler {
    constructor(store) {
      this.store = store;
    }

    async handle(request) {
      this.store.requestIds.push(request.requestId);
      if (request.value === 'throw') {
        throw new Error('DERR-007 handler exception');
      }
      if (request.value === 'slow' || request.requestId.includes('timeout')) {
        await delay(1000);
      }
      this.store.completedRequestIds.push(request.requestId);
      return { requestId: request.requestId, value: `profile:${request.value}`, serverId: options.serverName };
    }
  }

  class ScenarioDispatchObserver {
    constructor(store) {
      this.store = store;
    }

    onDispatchError(event) {
      const marker = [
        `surface=${String(event.surface)}`,
        `kind=${String(event.messageKind)}`,
        `reason=${String(event.reason)}`,
        `action=${String(event.action)}`
      ];
      if (event.packetName !== undefined) {
        marker.push(`packetName=${String(event.packetName)}`);
      }
      if (event.channelName !== undefined) {
        marker.push(`channelName=${String(event.channelName)}`);
      }
      if (event.error?.message !== undefined) {
        marker.push(`error=${String(event.error.message)}`);
      }
      const entry = marker.join(' ');
      this.store.dispatchErrors.push(entry);
      console.error(`zlink node scenario dispatch error: ${entry}`);
    }
  }

  class ScenarioProfileCommandHandler {
    constructor(store) {
      this.store = store;
    }

    async handle(command) {
      this.store.commandIds.push(command.commandId);
    }
  }

  class ScenarioRoutePingHandler {
    constructor(store) {
      this.store = store;
    }

    async handle(request, context) {
      this.store.routeRequestIds.push(request.requestId);
      this.store.routeSourceRids.push(String(context.sourceNodeRid));
      return {
        requestId: request.requestId,
        value: `route:${request.value}`,
        targetRid: options.serverName,
        sourceRid: String(context.sourceNodeRid)
      };
    }
  }

  Reflect.defineMetadata('design:paramtypes', [ScenarioEvidenceStore], ScenarioProfileHandler);
  Reflect.defineMetadata('design:paramtypes', [ScenarioEvidenceStore], ScenarioProfileCommandHandler);
  Reflect.defineMetadata('design:paramtypes', [ScenarioEvidenceStore], ScenarioRoutePingHandler);
  Reflect.defineMetadata('design:paramtypes', [ScenarioEvidenceStore], ScenarioDispatchObserver);

  const frameworkBuilder = zlinkFramework().codecs().addJson();
  frameworkBuilder.configureDispatch().setMessageDispatchErrorObserver(ScenarioDispatchObserver);
  if (options.mode === 'route-peer') {
    frameworkBuilder
      .addRouteMeshChannel(options.channel)
        .enableRouter(options.zlinkEndpoint)
        .routingId(options.serverName)
        .connect(options.routePeerEndpoints)
        .addRequestHandler('ScenarioRoutePing', ScenarioRoutePingHandler);
  } else {
    frameworkBuilder
      .addClientServerChannel(options.channel)
        .enableServer(options.zlinkEndpoint)
        .addRequestHandler('ScenarioProfileRequest', ScenarioProfileHandler)
        .addSendHandler('ScenarioProfileCommand', ScenarioProfileCommandHandler);
  }

  class ScenarioServerModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => frameworkBuilder.build()
      })
    ],
    providers: [
      { provide: ScenarioEvidenceStore, useValue: new ScenarioEvidenceStore() },
      ScenarioProfileHandler,
      ScenarioProfileCommandHandler,
      ScenarioRoutePingHandler,
      ScenarioDispatchObserver
    ]
  })(ScenarioServerModule);

  const app = await NestFactory.createApplicationContext(ScenarioServerModule, {
    logger: false,
    abortOnError: false
  });
  const server = http.createServer((request, response) => {
    if (request.url === '/health') {
      writeJson(response, 200, { status: 'ready' });
      return;
    }
    if (request.url === '/evidence') {
      writeJson(response, 200, {
        serverId: options.serverName,
        requestIds: evidence.requestIds,
        commandIds: evidence.commandIds,
        completedRequestIds: evidence.completedRequestIds,
        routeRequestIds: evidence.routeRequestIds,
        routeSourceRids: evidence.routeSourceRids,
        dispatchErrors: evidence.dispatchErrors
      });
      return;
    }
    writeJson(response, 404, { error: 'not-found' });
  });
  await listen(server, new URL(options.httpEndpoint).port);
  await fs.writeFile(options.readyFile, 'ready');
  await waitForShutdown();
  server.close();
  await app.close();
}

async function runClient(options) {
  await fs.mkdir(options.workDir, { recursive: true });
  const scenario = JSON.parse(await fs.readFile(options.scenarioFile, 'utf8'));

  ensure(['CH-001', 'CH-002', 'CH-004', 'CH-006', 'CH-007', 'DERR-001', 'DERR-002', 'DERR-007'].includes(scenario.id), 'scenario id must be supported');
  ensure(scenario.roles.server.some((server) => server.channel === options.channel),
    'scenario server channel must match the configured channel');
  ensure(scenario.roles.client.length > 0, 'scenario must declare at least one client role');
  ensure(path.resolve(options.workDir, scenario.artifacts.logs) === path.resolve(options.logDir),
    'scenario logs artifact must match configured log directory');
  ensure(path.resolve(options.workDir, scenario.artifacts.report) === path.resolve(options.reportFile),
    'scenario report artifact must match configured report file');
  ensure(scenario.artifacts.evidence === 'server:/evidence' || scenario.artifacts.evidence === 'servers:/evidence',
    'scenario evidence artifact must use supported evidence endpoint');

  const clientFrameworkBuilder = zlinkFramework().codecs().addJson();
  if (scenario.id === 'CH-004') {
    clientFrameworkBuilder
      .addRouteMeshChannel(options.channel)
        .enableRouter(options.zlinkEndpoint)
        .routingId(scenario.roles.client[0].routingId)
        .connect(options.routePeerEndpoints);
  } else {
    clientFrameworkBuilder
      .addClientServerChannel(options.channel)
        .enableClient(options.zlinkEndpoints);
  }

  class ScenarioClientModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => clientFrameworkBuilder.build()
      })
    ]
  })(ScenarioClientModule);

  const app = await NestFactory.createApplicationContext(ScenarioClientModule, {
    logger: false,
    abortOnError: false
  });
  const channels = app.get(ZLINK_CHANNEL_CLIENT, { strict: false });
  const routes = app.get(ZLINK_ROUTE_CLIENT, { strict: false });
  const httpClient = ZLinkHttpClient.create(options.httpEndpoints[0]).json().timeout(3000).build();
  const httpClients = options.httpEndpoints.map((endpoint) =>
    ZLinkHttpClient.create(endpoint).json().timeout(3000).build());
  const repliesByServer = new Map();
  try {
    for (const step of scenario.steps) {
      if (step.waitReady !== undefined) {
        const targets = step.waitReady.targets ?? [step.waitReady.target];
        const endpoints = targets.length === 1 ? [httpClient] : httpClients;
        ensure(endpoints.length === targets.length, 'readiness endpoint count must match server targets');
        let serverReady = false;
        const deadline = Date.now() + 10000;
        while (Date.now() < deadline && !serverReady) {
          serverReady = true;
          for (const endpoint of endpoints) {
            try {
              const response = await endpoint.get('/health').submitRaw();
              serverReady = serverReady && response.status === 200;
            } catch {
              serverReady = false;
            }
          }
          if (!serverReady) {
            await delay(100);
          }
        }
        ensure(targets.every((target) => target.startsWith('server:')), 'readiness targets must be server roles');
        ensure(serverReady, 'server readiness must be reached before client request');
        continue;
      }

      if (step.clientRequest !== undefined) {
        const request = step.clientRequest;
        ensure(request.client === scenario.roles.client[0].name, 'request client must match scenario role');
        ensure(request.channel === options.channel, 'request channel must match configured channel');
        ensure(request.packetName === 'ScenarioProfileRequest', 'request packet name must match handler');

        const reply = await channels
          .requestToChannel(request.channel, { requestId: request.requestId, value: request.value })
          .packetName(request.packetName)
          .timeout(3000)
          .submit();

        ensure(reply.requestId === request.requestId, 'reply request id must match');
        ensure(reply.value === request.expectReply, 'reply value must match scenario expectation');
        if (scenario.expect.reply.requestId === request.requestId) {
          ensure(reply.requestId === scenario.expect.reply.requestId, 'reply request id must match expect block');
          ensure(reply.value === scenario.expect.reply.value, 'reply value must match expect block');
        }
        continue;
      }

      if (step.clientTimeoutRequest !== undefined) {
        const request = step.clientTimeoutRequest;
        ensure(request.client === scenario.roles.client[0].name, 'timeout request client must match scenario role');
        ensure(request.channel === options.channel, 'timeout request channel must match configured channel');
        ensure(request.packetName === 'ScenarioProfileRequest', 'timeout request packet name must match handler');

        try {
          await channels
            .requestToChannel(request.channel, { requestId: request.requestId, value: request.value })
            .packetName(request.packetName)
            .timeout(request.timeoutMs)
            .submit();
          ensure(false, 'timeout request must fail with public timeout error');
        } catch (error) {
          ensure(/timeout|timed out|result 101/i.test(String(error?.message ?? error)),
            `timeout request must fail with a public timeout error: ${String(error?.message ?? error)}`);
        }
        continue;
      }

      if (step.concurrentTimeoutAndRequest !== undefined) {
        const request = step.concurrentTimeoutAndRequest;
        ensure(request.client === scenario.roles.client[0].name, 'concurrent request client must match scenario role');
        ensure(request.channel === options.channel, 'concurrent request channel must match configured channel');
        ensure(request.packetName === 'ScenarioProfileRequest', 'concurrent request packet name must match handler');

        const slowRequest = channels
          .requestToChannel(request.channel, {
            requestId: request.timeoutRequestId,
            value: request.timeoutValue
          })
          .packetName(request.packetName)
          .timeout(request.timeoutMs)
          .submit()
          .then(
            (reply) => ({ reply }),
            (error) => ({ error })
          );

        const reply = await channels
          .requestToChannel(request.channel, {
            requestId: request.normalRequestId,
            value: request.normalValue
          })
          .packetName(request.packetName)
          .timeout(3000)
          .submit();
        ensure(reply.requestId === request.normalRequestId, 'concurrent normal reply request id must match');
        ensure(reply.value === request.expectNormalReply, 'concurrent normal reply value must match');

        const slowResult = await slowRequest;
        ensure(slowResult.error !== undefined, 'concurrent slow request must fail with public timeout error');
        ensure(/timeout|timed out|result 101/i.test(String(slowResult.error?.message ?? slowResult.error)),
          `concurrent slow request must fail with a public timeout error: ${String(slowResult.error?.message ?? slowResult.error)}`);
        continue;
      }

      if (step.waitFor !== undefined) {
        ensure(step.waitFor.durationMs > 0, 'waitFor duration must be positive');
        await delay(step.waitFor.durationMs);
        continue;
      }

      if (step.roundRobinRequest !== undefined) {
        const request = step.roundRobinRequest;
        ensure(request.client === scenario.roles.client[0].name, 'request client must match scenario role');
        ensure(request.channel === options.channel, 'request channel must match configured channel');
        ensure(request.packetName === 'ScenarioProfileRequest', 'request packet name must match handler');
        ensure(request.requestCount === 90, 'CH-002 request count must be 90');
        ensure(request.warmupCount === 3, 'CH-002 warm-up count must be 3');

        for (let index = 0; index < request.warmupCount; index += 1) {
          const reply = await channels
            .requestToChannel(request.channel, {
              requestId: `${request.requestIdPrefix}-warmup-${index}`,
              value: `${request.valuePrefix}-warmup-${index}`
            })
            .packetName(request.packetName)
            .timeout(3000)
            .submit();
          ensure(reply.requestId === `${request.requestIdPrefix}-warmup-${index}`,
            'warm-up reply request id must match');
        }

        for (let index = 0; index < request.requestCount; index += 1) {
          const requestId = `${request.requestIdPrefix}-${index}`;
          const value = `${request.valuePrefix}-${index}`;
          const reply = await channels
            .requestToChannel(request.channel, { requestId, value })
            .packetName(request.packetName)
            .timeout(3000)
            .submit();
          ensure(reply.requestId === requestId, 'round-robin reply request id must match');
          ensure(reply.value === `profile:${value}`, 'round-robin reply value must match');
          ensure(typeof reply.serverId === 'string' && reply.serverId.length > 0,
            'round-robin reply must include server id');
          repliesByServer.set(reply.serverId, (repliesByServer.get(reply.serverId) ?? 0) + 1);
        }
        continue;
      }

      if (step.routeTargetedRequest !== undefined) {
        const request = step.routeTargetedRequest;
        ensure(request.client === scenario.roles.client[0].name, 'route request client must match scenario role');
        ensure(request.channel === options.channel, 'route request channel must match configured channel');
        ensure(request.packetName === 'ScenarioRoutePing', 'route request packet name must match handler');

        const reply = await routes
          .request(request.channel, request.targetRid, {
            requestId: request.requestId,
            value: request.value
          })
          .packetName(request.packetName)
          .timeout(3000)
          .submit();
        ensure(reply.requestId === request.requestId, 'route reply request id must match');
        ensure(reply.value === request.expectReply, 'route reply value must match');
        ensure(reply.targetRid === request.targetRid, 'route reply target routing id must match');
        ensure(reply.sourceRid === request.sourceRid, 'route reply source routing id must match');

        try {
          await routes
            .request(request.channel, request.missingRid, {
              requestId: `${request.requestId}-missing`,
              value: request.value
            })
            .packetName(request.packetName)
            .timeout(request.missingTimeoutMs)
            .submit();
          ensure(false, 'missing route request must fail with public error');
        } catch (error) {
          ensure(/timeout|timed out|result 101|not connected|not ready|host unreachable/i.test(String(error?.message ?? error)),
            `missing route request must expose public route error: ${String(error?.message ?? error)}`);
        }

        const httpByServer = new Map(scenario.roles.server.map((server, index) => [server.name, httpClients[index]]));
        const targetHttp = httpByServer.get(request.targetRid);
        const otherHttp = httpByServer.get(request.nonTargetRid);
        ensure(targetHttp !== undefined, 'target route peer evidence endpoint must exist');
        ensure(otherHttp !== undefined, 'non-target route peer evidence endpoint must exist');

        let targetEvidence = { routeRequestIds: [], routeSourceRids: [] };
        let targetMatched = false;
        const deadline = Date.now() + 10000;
        while (Date.now() < deadline && !targetMatched) {
          targetEvidence = await targetHttp.get('/evidence').fetch();
          targetMatched = targetEvidence.routeRequestIds.includes(request.requestId)
            && targetEvidence.routeSourceRids.includes(request.sourceRid);
          if (!targetMatched) {
            await delay(100);
          }
        }
        ensure(targetMatched, 'target route peer evidence must include request and source routing id');
        const nonTargetDeadline = Date.now() + 1000;
        while (Date.now() < nonTargetDeadline) {
          const otherEvidence = await otherHttp.get('/evidence').fetch();
          ensure(!otherEvidence.routeRequestIds.includes(request.requestId),
            'non-target route peer evidence must not include targeted request');
          await delay(100);
        }
        continue;
      }

      if (step.assertEvidence !== undefined) {
        const evidenceStep = step.assertEvidence;
        ensure(evidenceStep.target === 'server:api-a', 'evidence target must be server:api-a');
        let evidence = { requestIds: [], commandIds: [], completedRequestIds: [] };
        let evidenceMatched = false;
        const deadline = Date.now() + 10000;
        while (Date.now() < deadline && !evidenceMatched) {
          evidence = await httpClient.get('/evidence').fetch();
          const requestMatched = typeof evidenceStep.requestId !== 'string'
            || evidenceStep.requestId.length === 0
            || evidence.requestIds.includes(evidenceStep.requestId);
          const commandMatched = typeof evidenceStep.commandId !== 'string'
            || evidenceStep.commandId.length === 0
            || evidence.commandIds.includes(evidenceStep.commandId);
          const completedMatched = typeof evidenceStep.completedRequestId !== 'string'
            || evidenceStep.completedRequestId.length === 0
            || evidence.completedRequestIds.includes(evidenceStep.completedRequestId);
          evidenceMatched = requestMatched && commandMatched && completedMatched;
          if (!evidenceMatched) {
            await delay(100);
          }
        }
        ensure(evidenceMatched, 'server evidence must match scenario step before timeout');
        if (typeof evidenceStep.requestId === 'string' && evidenceStep.requestId.length > 0) {
          ensure(evidence.requestIds.includes(evidenceStep.requestId),
            'server evidence must include request id from step');
        }
        if (typeof evidenceStep.commandId === 'string' && evidenceStep.commandId.length > 0) {
          ensure(evidence.commandIds.includes(evidenceStep.commandId),
            'server evidence must include command id from step');
        }
        if (typeof evidenceStep.completedRequestId === 'string' && evidenceStep.completedRequestId.length > 0) {
          ensure(evidence.completedRequestIds.includes(evidenceStep.completedRequestId),
            'server evidence must include completed request id from step');
        }
        ensure((scenario.expect.serverEvidence.requestIds ?? [])
          .every((requestId) => evidence.requestIds.includes(requestId)),
          'server evidence must include all expected request ids');
        ensure((scenario.expect.serverEvidence.commandIds ?? [])
          .every((commandId) => evidence.commandIds.includes(commandId)),
          'server evidence must include all expected command ids');
        ensure((scenario.expect.serverEvidence.completedRequestIds ?? [])
          .every((requestId) => evidence.completedRequestIds.includes(requestId)),
          'server evidence must include all expected completed request ids');
        continue;
      }

      if (step.unregisteredRequest !== undefined) {
        const request = step.unregisteredRequest;
        ensure(request.client === scenario.roles.client[0].name, 'unregistered request client must match scenario role');
        ensure(request.channel === options.channel, 'unregistered request channel must match configured channel');
        ensure(request.expectError === 'handler_not_found', 'unregistered request expected error must be handler_not_found');

        try {
          await channels
            .requestToChannel(request.channel, { requestId: request.requestId, value: request.value })
            .packetName(request.packetName)
            .timeout(3000)
            .submit();
          ensure(false, 'unregistered request must fail with public handler missing error');
        } catch (error) {
          ensure(/No channel request handler is registered|handler/i.test(String(error?.message ?? error)),
            `unregistered request must expose public handler error: ${String(error?.message ?? error)}`);
        }

        const observed = await waitForDispatchEvidence(httpClient, (marker) =>
          marker.includes('surface=channel')
          && marker.includes('kind=request')
          && marker.includes('reason=handlerMissing')
          && marker.includes('action=replyError')
          && marker.includes(`packetName=${request.packetName}`));
        ensure(observed, 'server dispatch evidence must include unregistered request error');
        continue;
      }

      if (step.unregisteredSend !== undefined) {
        const send = step.unregisteredSend;
        ensure(send.client === scenario.roles.client[0].name, 'unregistered send client must match scenario role');
        ensure(send.channel === options.channel, 'unregistered send channel must match configured channel');
        ensure(send.expectAction === 'drop', 'unregistered send expected action must be drop');

        await channels
          .sendToChannel(send.channel, { commandId: send.commandId, value: send.value })
          .packetName(send.packetName)
          .submit();

        const observed = await waitForDispatchEvidence(httpClient, (marker) =>
          marker.includes('surface=channel')
          && marker.includes('kind=send')
          && marker.includes('reason=handlerMissing')
          && marker.includes('action=drop')
          && marker.includes(`packetName=${send.packetName}`));
        ensure(observed, 'server dispatch evidence must include unregistered send drop');
        continue;
      }

      if (step.handlerExceptionRequest !== undefined) {
        const request = step.handlerExceptionRequest;
        ensure(request.client === scenario.roles.client[0].name, 'handler exception request client must match scenario role');
        ensure(request.channel === options.channel, 'handler exception request channel must match configured channel');
        ensure(request.packetName === 'ScenarioProfileRequest', 'handler exception request packet name must match handler');
        ensure(request.expectError === 'request_failed', 'handler exception expected error must be request_failed');

        try {
          await channels
            .requestToChannel(request.channel, { requestId: request.requestId, value: request.value })
            .packetName(request.packetName)
            .timeout(3000)
            .submit();
          ensure(false, 'handler exception request must fail with public request_failed error');
        } catch (error) {
          ensure(String(error?.message ?? error).includes('DERR-007 handler exception'),
            `handler exception error message must be preserved: ${String(error?.message ?? error)}`);
        }

        const observed = await waitForDispatchEvidence(httpClient, (marker) =>
          marker.includes('surface=channel')
          && marker.includes('kind=request')
          && marker.includes('reason=handlerException')
          && marker.includes('action=replyError')
          && marker.includes(`packetName=${request.packetName}`)
          && marker.includes('error=DERR-007 handler exception'));
        ensure(observed, 'server dispatch evidence must include handler exception request');
        continue;
      }

      if (step.clientSend !== undefined) {
        const send = step.clientSend;
        ensure(send.client === scenario.roles.client[0].name, 'send client must match scenario role');
        ensure(send.channel === options.channel, 'send channel must match configured channel');
        ensure(send.packetName === 'ScenarioProfileCommand', 'send packet name must match handler');
        await channels
          .sendToChannel(send.channel, { commandId: send.commandId, value: send.value })
          .packetName(send.packetName)
          .submit();
        continue;
      }

      if (step.assertDistribution !== undefined) {
        for (const [serverId, expectedCount] of Object.entries(step.assertDistribution.expectedCounts)) {
          ensure(repliesByServer.get(serverId) === expectedCount,
            `round-robin reply count for ${serverId} must be ${expectedCount}`);
        }

        for (const endpoint of httpClients) {
          const evidence = await endpoint.get('/evidence').fetch();
          const expectedCount = step.assertDistribution.expectedCounts[evidence.serverId];
          ensure(expectedCount !== undefined, 'server evidence must identify a known server');
          const verifiedRequests = evidence.requestIds.filter((requestId) =>
            requestId.startsWith(`${scenario.expect.reply.requestIdPrefix}-`)
            && !requestId.includes('-warmup-'));
          ensure(verifiedRequests.length === expectedCount,
            `server evidence count for ${evidence.serverId} must be ${expectedCount}`);
        }
        continue;
      }

      throw new Error('scenario step must contain a known action.');
    }

    const report = {
      scenarioId: scenario.id,
      language: 'node',
      runtimeVersion: process.version,
      transportBackend: 'tcp',
      codec: 'json',
      result: 'passed',
      scenarioFile: options.scenarioFile,
      processCount: scenario.roles.server.length + scenario.roles.client.length,
      passed: 1,
      failed: 0,
      skipped: 0,
      logPath: options.logDir,
      evidencePath: options.httpEndpoints.map((endpoint) => `${endpoint}/evidence`).join(','),
      executedScriptPath: options.executedScriptPath,
      processIds: processIds(options.serverProcessIds, scenario.roles.client),
      failureLayer: null,
      completedAt: new Date().toISOString()
    };
    await fs.writeFile(options.reportFile, JSON.stringify(report, null, 2));
    console.log(`scenario=${scenario.id} language=node result=passed evidence=${options.reportFile}`);
  } finally {
    await httpClient.close();
    await Promise.all(httpClients.map((client) => client.close()));
    await app.close();
  }
}

function parseOptions(args) {
  const values = new Map();
  for (let index = 0; index < args.length; index += 2) {
    if (index + 1 >= args.length || !args[index].startsWith('--')) {
      throw new Error('scenario E2E arguments must use --name value pairs.');
    }
    values.set(args[index].slice(2), args[index + 1]);
  }
  const workDir = required(values, 'work-dir');
  return {
    workDir,
    scenarioFile: values.get('scenario') ?? '',
    zlinkEndpoint: required(values, 'zlink-endpoint'),
    zlinkEndpoints: splitList(required(values, 'zlink-endpoint')),
    httpEndpoint: required(values, 'http-endpoint'),
    httpEndpoints: splitList(required(values, 'http-endpoint')),
    channel: values.get('channel') ?? 'scenario-api',
    readyFile: values.get('ready-file') ?? path.join(workDir, 'server.ready'),
    reportFile: values.get('report-file') ?? path.join(workDir, 'report.json'),
    logDir: values.get('log-dir') ?? path.join(workDir, 'logs'),
    executedScriptPath: values.get('executed-script') ?? '',
    serverProcessIds: values.get('server-process-ids') ?? '',
    serverName: values.get('server-name') ?? 'api-a',
    mode: values.get('mode') ?? '',
    routePeerEndpoints: splitList(values.get('route-peer-endpoints') ?? '')
  };
}

function processIds(serverProcessIds, clientRoles) {
  const result = {};
  for (const client of clientRoles) {
    result[`client:${client.name}`] = process.pid;
  }
  for (const entry of splitList(serverProcessIds)) {
    const separator = entry.indexOf('=');
    ensure(separator > 0 && separator < entry.length - 1, 'server process id entry must use role=pid');
    result[entry.slice(0, separator)] = Number(entry.slice(separator + 1));
  }
  return result;
}

function splitList(value) {
  return value.split(',').map((item) => item.trim()).filter((item) => item.length > 0);
}

function required(values, name) {
  const value = values.get(name);
  if (value === undefined || value.trim().length === 0) {
    throw new Error(`--${name} is required.`);
  }
  return value;
}

function writeJson(response, status, body) {
  response.writeHead(status, { 'content-type': 'application/json' });
  response.end(JSON.stringify(body));
}

function listen(server, port) {
  return new Promise((resolve) => {
    server.listen(Number(port), '127.0.0.1', resolve);
  });
}

function waitForShutdown() {
  return new Promise((resolve) => {
    process.once('SIGINT', resolve);
    process.once('SIGTERM', resolve);
  });
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

async function waitForDispatchEvidence(httpClient, predicate) {
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    const evidence = await httpClient.get('/evidence').fetch();
    if ((evidence.dispatchErrors ?? []).some(predicate)) {
      return true;
    }
    await delay(100);
  }
  return false;
}

function ensure(condition, message) {
  if (!condition) {
    throw new Error(`ensure failed: ${message}`);
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
