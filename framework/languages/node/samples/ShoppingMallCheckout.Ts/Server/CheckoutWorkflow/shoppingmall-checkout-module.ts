import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { PacketNames } from '../../Shared/Contracts/messages';
import { SampleNames, SampleTimings } from '../../Shared/Configuration/sample-names';
import { AuthorizePaymentHandler } from './Handlers/authorize-payment-handler';
import { ConfirmOrderHandler } from './Handlers/confirm-order-handler';
import { ReserveInventoryHandler } from './Handlers/reserve-inventory-handler';
import { StartCheckoutHandler } from './Handlers/start-checkout-handler';
import { CheckoutStore } from './checkout-store';

function createShoppingMallCheckoutModule(config: { checkoutEndpoint: string }) {
  class ShoppingMallCheckoutModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .options({ requestTimeoutMs: SampleTimings.requestTimeout })
          .codecs()
            .addJson()
          .addClientServerChannel(SampleNames.checkoutChannel)
            .enableServer(config.checkoutEndpoint)
            .addRequestHandler(PacketNames.startCheckoutReq, StartCheckoutHandler)
            .addRequestHandler(PacketNames.authorizePaymentReq, AuthorizePaymentHandler)
            .addRequestHandler(PacketNames.reserveInventoryReq, ReserveInventoryHandler)
            .addRequestHandler(PacketNames.confirmOrderReq, ConfirmOrderHandler)
          .build()
      })
    ],
    providers: [
      CheckoutStore,
      StartCheckoutHandler,
      AuthorizePaymentHandler,
      ReserveInventoryHandler,
      ConfirmOrderHandler
    ]
  })(ShoppingMallCheckoutModule);

  return ShoppingMallCheckoutModule;
}

export {
  createShoppingMallCheckoutModule
};
