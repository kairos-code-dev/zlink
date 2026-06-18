import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkRegistryModule } from '@zlink-systems/nestjs';

type RegistryServerConfig = {
  registryPubEndpoint: string;
  registryRouterEndpoint: string;
};

type SupportChatRegistryServer = {
  start(): Promise<void>;
  close(): Promise<void>;
};

function createSupportChatRegistryServer(config: RegistryServerConfig): SupportChatRegistryServer {
  let app: Awaited<ReturnType<typeof NestFactory.createApplicationContext>> | undefined;

  class SupportChatRegistryModule {}
  Module({
    imports: [
      ZLinkRegistryModule.forRoot({
        pubEndpoint: config.registryPubEndpoint,
        routerEndpoint: config.registryRouterEndpoint
      })
    ]
  })(SupportChatRegistryModule);

  return {
    async start(): Promise<void> {
      app = await NestFactory.createApplicationContext(SupportChatRegistryModule, { logger: false });
    },
    async close(): Promise<void> {
      await app?.close();
      app = undefined;
    }
  };
}

export { createSupportChatRegistryServer };
export type { SupportChatRegistryServer, RegistryServerConfig };
