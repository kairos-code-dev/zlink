import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkRegistryModule } from '@zlink-systems/nestjs';

type RegistryServerConfig = {
  registryPubEndpoint: string;
  registryRouterEndpoint: string;
};

type BingoRegistryServer = {
  start(): Promise<void>;
  close(): Promise<void>;
};

function createBingoRegistryServer(config: RegistryServerConfig): BingoRegistryServer {
  let app: Awaited<ReturnType<typeof NestFactory.createApplicationContext>> | undefined;

  class BingoRegistryModule {}
  Module({
    imports: [
      ZLinkRegistryModule.forRoot({
        pubEndpoint: config.registryPubEndpoint,
        routerEndpoint: config.registryRouterEndpoint
      })
    ]
  })(BingoRegistryModule);

  return {
    async start(): Promise<void> {
      app = await NestFactory.createApplicationContext(BingoRegistryModule, { logger: false });
    },
    async close(): Promise<void> {
      await app?.close();
      app = undefined;
    }
  };
}

export { createBingoRegistryServer };
export type { BingoRegistryServer, RegistryServerConfig };
