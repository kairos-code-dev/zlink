const { Module } = require('@nestjs/common');
const path = require('node:path');
const { ZLinkModule, zlinkDiscoverProviders, zlinkFramework } = require('../../../../../packages/nestjs/dist');
const { SampleNames } = require('../Configuration/sample-names');
const {
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

function createBingoRegistryModule(config: {
  registryEndpoint: string;
}) {
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
      ...zlinkDiscoverProviders(path.join(__dirname, 'Handlers'))
    ]
  })(BingoRegistryModule);

  return BingoRegistryModule;
}

export { BingoServiceRegistry, createBingoRegistryModule };
