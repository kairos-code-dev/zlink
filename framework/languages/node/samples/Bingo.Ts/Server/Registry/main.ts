require('reflect-metadata');

const { Inject, Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkFramework, zlinkHandlers } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { SampleNames } = require('../Configuration/sample-names');
const { loadSampleConfig } = require('../Configuration/sample-config');
const {
  PacketNames,
  registerServiceRes,
  resolveServiceRes
} = require('../../Shared/Contracts/messages');
import type {
  RegisterServiceReq,
  RegisterServiceRes,
  ResolveServiceReq,
  ResolveServiceRes
} from '../../Shared/Contracts/messages';

class BingoServiceRegistry {
  private readonly services: Map<string, string>;

  constructor() {
    this.services = new Map();
  }

  register(request: RegisterServiceReq): RegisterServiceRes {
    if (typeof request.serviceName !== 'string' || request.serviceName.length === 0) {
      throw new Error('serviceName is required.');
    }
    if (typeof request.endpoint !== 'string' || request.endpoint.length === 0) {
      throw new Error('endpoint is required.');
    }
    this.services.set(request.serviceName, request.endpoint);
    return registerServiceRes(request.serviceName, request.endpoint);
  }

  resolve(request: ResolveServiceReq): ResolveServiceRes {
    const endpoint = this.services.get(request.serviceName);
    if (endpoint === undefined) {
      throw new Error(`Service '${request.serviceName}' is not registered.`);
    }
    return resolveServiceRes(request.serviceName, endpoint);
  }
}

class RegisterServiceHandler {
  constructor(private readonly registry: BingoServiceRegistry) {}

  handle(request: RegisterServiceReq): RegisterServiceRes {
    return this.registry.register(request);
  }
}

class ResolveServiceHandler {
  constructor(private readonly registry: BingoServiceRegistry) {}

  handle(request: ResolveServiceReq): ResolveServiceRes {
    return this.registry.resolve(request);
  }
}

Inject(BingoServiceRegistry)(RegisterServiceHandler, undefined, 0);
Inject(BingoServiceRegistry)(ResolveServiceHandler, undefined, 0);

async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  class BingoRegistryModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .clientServerChannel(SampleNames.registryChannel, (channel) => channel
            .server(config.registryEndpoint)
            .handlerGroup('registry'))
          .build()
      })
    ],
    providers: [
      BingoServiceRegistry,
      ...zlinkHandlers('registry')
        .request(RegisterServiceHandler, PacketNames.registerServiceReq)
        .request(ResolveServiceHandler, PacketNames.resolveServiceReq)
        .providers()
    ]
  })(BingoRegistryModule);

  const app = await NestFactory.createApplicationContext(BingoRegistryModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.registryEndpoint,
    channelName: SampleNames.registryChannel
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await closeNestRuntime(app);
  }
}

bootstrap().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
