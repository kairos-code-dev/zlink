import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLINK_CHANNEL_CLIENT, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import { PacketNames } from '../../Shared/messages';
import { parseServerOptions } from './Configuration/server-options';
import type { ServerOptions } from './Configuration/server-options';
import { createWorkflowEndpoints } from './Endpoints/workflow-endpoints';
import { EvidenceDispatchErrorObserver, WorkflowRequestHandler } from './Handlers/workflow-handlers';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startWorkflowHost(args: readonly string[]): Promise<void> {
  const options = parseServerOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  let stopping = false;
  const WorkflowModule = createWorkflowModule(options, evidence);
  const app = await NestFactory.createApplicationContext(WorkflowModule, { logger: false, abortOnError: false });
  const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const server = await startHttpServer(options.httpUrl, createWorkflowEndpoints(evidence, channel, () => { stopping = true; }));

  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createWorkflowModule(options: ServerOptions, evidence: EvidenceStore): Function {
  class WorkflowModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .setMessageFlowObserver(EvidenceDispatchErrorObserver)
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          if (options.registryRouterEndpoint !== undefined) {
            builder.useDiscovery().addRegistryEndpoint(options.registryRouterEndpoint);
          }
          builder.addClientServerChannel('workflow')
            .enableServer(options.workflowEndpoint)
            .routingId(options.rid)
            .enableClient()
            .addRequestHandler(PacketNames.workflowReq, WorkflowRequestHandler);
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      EvidenceDispatchErrorObserver,
      WorkflowRequestHandler
    ]
  })(WorkflowModule);
  return WorkflowModule;
}
