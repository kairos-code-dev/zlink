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
  ZLinkChannelClient,
  ZLinkRequestCall
} from '../../../packages/framework/dist';
import type {
  RegisterServiceRes,
  ResolveServiceRes
} from '../Shared/Contracts/messages';

type BingoRegistryClient = BingoChannelClient & {
  register(serviceName: string, endpoint: string): Promise<RegisterServiceRes>;
  resolve(serviceName: string): Promise<ResolveServiceRes>;
};

type BingoChannelClient = {
  requestToChannel(targetChannelName: string, payload: unknown): ZLinkRequestCall;
  request(payload: unknown): ZLinkRequestCall;
  stop(): Promise<void>;
};

async function createRegistryClient(registryEndpoint: string): Promise<BingoRegistryClient> {
  return await createChannelClient(SampleNames.registryChannel, registryEndpoint);
}

async function createChannelClient(channelName: string, endpoint: string): Promise<BingoRegistryClient> {
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
  const client = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;

  return {
    requestToChannel(targetChannelName: string, payload: unknown): ZLinkRequestCall {
      return client.requestToChannel(targetChannelName, payload);
    },
    request(payload: unknown): ZLinkRequestCall {
      return client.requestToChannel(channelName, payload);
    },
    async register(serviceName: string, endpoint: string): Promise<RegisterServiceRes> {
      return await retry(() => client
        .requestToChannel(channelName, registerServiceReq(serviceName, endpoint))
        .packetName(PacketNames.registerServiceReq)
        .timeout(SampleTimings.requestTimeout)
        .submit<RegisterServiceRes>());
    },
    async resolve(serviceName: string): Promise<ResolveServiceRes> {
      return await retry(() => client
        .requestToChannel(channelName, resolveServiceReq(serviceName))
        .packetName(PacketNames.resolveServiceReq)
        .timeout(SampleTimings.requestTimeout)
        .submit<ResolveServiceRes>());
    },
    async stop(): Promise<void> {
      await closeNestRuntime(app);
    }
  };
}

export { createChannelClient, createRegistryClient };
export type { BingoChannelClient, BingoRegistryClient };
