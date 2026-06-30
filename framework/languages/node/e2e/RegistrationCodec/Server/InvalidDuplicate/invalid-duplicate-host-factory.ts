import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { PacketNames, RegistrationCodecNames } from '../../Shared/messages';
import { parseInvalidDuplicateOptions, type InvalidDuplicateOptions } from './Configuration/invalid-duplicate-options';
import { DuplicateEchoRequestHandler } from './Handlers/duplicate-handlers';

export async function startInvalidDuplicate(args: readonly string[]): Promise<void> {
  const options = parseInvalidDuplicateOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const InvalidDuplicateModule = createInvalidDuplicateModule(options);
  const app = await NestFactory.createApplicationContext(InvalidDuplicateModule, { logger: false, abortOnError: false });
  await app.close();
}

function createInvalidDuplicateModule(options: InvalidDuplicateOptions): Function {
  class InvalidDuplicateModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          const channel = builder.addClientServerChannel(RegistrationCodecNames.channel)
            .enableServer(options.channelEndpoint);
          channel.addRequestHandler(PacketNames.echoManualReq, DuplicateEchoRequestHandler);
          channel.addRequestHandler(PacketNames.echoManualReq, DuplicateEchoRequestHandler);
          return builder.build();
        }
      })
    ],
    providers: [DuplicateEchoRequestHandler]
  })(InvalidDuplicateModule);
  return InvalidDuplicateModule;
}
