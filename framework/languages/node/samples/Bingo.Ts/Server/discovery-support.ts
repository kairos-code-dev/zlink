require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, ZLINK_CHANNEL_CLIENT, zlinkFramework } = require('../../../../packages/nestjs/dist');
const { SampleNames, SampleTimings } = require('./Configuration/sample-names');
const {
  PacketNames,
  registerServiceReq,
  resolveServiceReq
} = require('../Shared/Contracts/messages');
const { closeNestRuntime, retry } = require('./runtime-support');
import type {
  RegisterServiceRes,
  ResolveServiceRes
} from '../Shared/Contracts/messages';

async function createRegistryClient(registryEndpoint: string): Promise<any> {
  return await createChannelClient(SampleNames.registryChannel, registryEndpoint);
}

async function createChannelClient(channelName: string, endpoint: string): Promise<any> {
  class BingoRegistryClientModule {}

  Module({
    imports: [
      ZLinkModule.forRoot(
        zlinkFramework()
          .clientServerChannel(channelName, (channel) => channel
            .client(endpoint))
          .build()
      )
    ]
  })(BingoRegistryClientModule);

  const app = await NestFactory.createApplicationContext(BingoRegistryClientModule, {
    logger: false,
    abortOnError: false
  });
  const client = app.get(ZLINK_CHANNEL_CLIENT, { strict: false });

  return {
    requestToChannel(targetChannelName: string, payload: unknown): any {
      return client.requestToChannel(targetChannelName, payload);
    },
    request(payload: unknown): any {
      return client.requestToChannel(channelName, payload);
    },
    async register(serviceName: string, endpoint: string): Promise<RegisterServiceRes> {
      return await retry(() => client
        .requestToChannel(channelName, registerServiceReq(serviceName, endpoint))
        .packetName(PacketNames.registerServiceReq)
        .timeout(SampleTimings.requestTimeout)
        .submit());
    },
    async resolve(serviceName: string): Promise<ResolveServiceRes> {
      return await retry(() => client
        .requestToChannel(channelName, resolveServiceReq(serviceName))
        .packetName(PacketNames.resolveServiceReq)
        .timeout(SampleTimings.requestTimeout)
        .submit());
    },
    async stop(): Promise<void> {
      await closeNestRuntime(app);
    }
  };
}

export { createChannelClient, createRegistryClient };
