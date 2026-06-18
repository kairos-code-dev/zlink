import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames, SampleTimings } from '../Shared/Configuration/sample-names';

function createDeliveryDispatchClientModule(config: { dispatchEndpoint: string }) {
  class DeliveryDispatchClientModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .options({ requestTimeoutMs: SampleTimings.requestTimeout })
          .codecs()
            .addJson()
          .addClientServerChannel(SampleNames.dispatchChannel)
            .enableClient(config.dispatchEndpoint)
          .build()
      })
    ]
  })(DeliveryDispatchClientModule);

  return DeliveryDispatchClientModule;
}

export {
  createDeliveryDispatchClientModule
};
