import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { BindCourierHandler } from './bind-courier-handler';
import { CourierActorFactory } from './courier-actor';
import { CourierEntrySpot } from './courier-entry-spot';
import { OfferDeliveryActorNodeHandler, OfferDeliveryHandler } from './offer-delivery-handler';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

type CourierOptions = {
  courierId: string;
  mode: string;
};

function createCourierGatewayModule(config: DeliveryDispatchServerConfig) {
  class CourierGatewayModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-courier-gateway.log`)
            .traceLabel('courier-gateway');
          return builder
            .useDiscovery()
              .addRegistryEndpoint(config.registryRouterEndpoint)
            .addClientServerChannel(SampleNames.courierRouteChannel)
              .enableServer(config.courierRouteEndpoint)
              .addHandlerGroup('courier-gateway')
            .addRouteMesh(SampleNames.courierActorNodeRouteChannel)
              .routingId('courier-gateway')
              .connect([
                config.courierActorNode1RouteEndpoint,
                config.courierActorNode2RouteEndpoint
              ])
            .build();
        }
      })
    ],
    providers: [
      BindCourierHandler,
      OfferDeliveryHandler
    ]
  })(CourierGatewayModule);

  return CourierGatewayModule;
}

function createCourierActorNodeModule(config: DeliveryDispatchServerConfig, options: CourierOptions) {
  class CourierActorNodeModule {}
  const endpoint = options.courierId === 'courier-a'
    ? config.courierActorNode1RouteEndpoint
    : config.courierActorNode2RouteEndpoint;
  const spotEndpoint = options.courierId === 'courier-a'
    ? config.courierActorNode1SpotEndpoint
    : config.courierActorNode2SpotEndpoint;
  const nodeRid = options.courierId === 'courier-a' ? 'courier-node-1' : 'courier-node-2';

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.DELIVERYDISPATCH_LOG_DIR ?? 'logs'}/flow-${nodeRid}.log`)
            .traceLabel(nodeRid);
          return builder
            .useDiscovery()
              .addRegistryEndpoint(config.registryRouterEndpoint)
            .addRouteMesh(SampleNames.courierActorNodeRouteChannel)
              .enableRouter(endpoint)
              .routingId(nodeRid)
              .addHandlerGroup('courier-actor-node')
            .addSpotMesh(SampleNames.courierActorSpotMesh)
              .enableRouter(spotEndpoint, nodeRid)
              .addEntrySpot(CourierEntrySpot)
              .actorFactory(SampleNames.courierActorType, CourierActorFactory)
            .build();
        }
      })
    ],
    providers: [
      { provide: 'DELIVERYDISPATCH_COURIER_OPTIONS', useValue: options },
      CourierActorFactory,
      CourierEntrySpot,
      OfferDeliveryActorNodeHandler
    ]
  })(CourierActorNodeModule);

  return CourierActorNodeModule;
}

export {
  createCourierActorNodeModule,
  createCourierGatewayModule
};

export type {
  CourierOptions
};
