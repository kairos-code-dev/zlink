import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkRegistryModule } from '@zlink-systems/nestjs';
import { closeHttpServer, startHttpServer } from './Support/http-server';
import { parseRegistryOptions } from './Configuration/registry-options';

export async function startRegistryHost(args: readonly string[]): Promise<void> {
  const options = parseRegistryOptions(args);
  let stopping = false;

  class RegistryModule {}
  Module({
    imports: [
      ZLinkRegistryModule.forRoot({
        pubEndpoint: options.registryPubEndpoint,
        routerEndpoint: options.registryRouterEndpoint
      })
    ]
  })(RegistryModule);

  const app = await NestFactory.createApplicationContext(RegistryModule, { logger: false });
  const server = await startHttpServer(options.httpUrl, [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'registry', rid: options.rid }) },
    {
      method: 'POST',
      path: '/shutdown',
      handle: () => {
        stopping = true;
        return { status: 'stopping' };
      }
    }
  ]);
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}
