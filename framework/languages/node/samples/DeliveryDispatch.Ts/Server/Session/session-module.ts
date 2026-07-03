import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { PacketNames } from '../../Shared/Contracts/messages';
import { CustomerSessionDirectory } from './customer-session-directory';
import { CustomerSessionFactory } from './customer-session';
import { DeliveryStatusFanoutHandler } from './delivery-status-fanout-handler';
import { createDeliveryDispatchLocationStore, deliveryDispatchLocationOptions } from '../Configuration/location-store';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function createSessionModule(config: DeliveryDispatchServerConfig) {
  class SessionModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-session.log`)
            .traceLabel('session');
          builder.addLocationStore(createDeliveryDispatchLocationStore(config));
          Object.assign(builder.configureLocations(), deliveryDispatchLocationOptions());
          return builder
            .addClientServerChannel(SampleNames.trackingChannel)
              .enableClient()
            .addFanoutChannel(SampleNames.statusFanoutChannel)
              .enableSubscriber()
              .addPublishHandler(PacketNames.deliveryStatusNotify, DeliveryStatusFanoutHandler)
            .addSpotMesh(SampleNames.deliverySpotMesh)
              .enableRouter(config.sessionSpotRouterEndpoint, config.sessionSpotNodeRid)
              .enablePubSub(config.sessionSpotEndpoint, config.sessionSpotNodeRid)
            .addStreamNode(SampleNames.customerStreamNode)
              .bind(config.sessionStreamEndpoint)
              .registerSession(CustomerSessionFactory)
            .build();
        }
      })
    ],
    providers: [
      CustomerSessionDirectory,
      CustomerSessionFactory,
      DeliveryStatusFanoutHandler
    ]
  })(SessionModule);

  return SessionModule;
}

export { createSessionModule };
