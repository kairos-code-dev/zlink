const SampleNames = {
  dispatchChannel: 'deliverydispatch.dispatch',
  courierAChannel: 'deliverydispatch.courier.a',
  courierBChannel: 'deliverydispatch.courier.b',
  trackingChannel: 'deliverydispatch.tracking',
  statusFanoutChannel: 'deliverydispatch.status',
  deliverySpotMesh: 'delivery-spots',
  customerStreamNode: 'delivery-customer-stream',
  customerActorType: 'delivery-customer'
} as const;

const SampleTimings = {
  requestTimeout: 10000,
  dispatchTimeout: 2000,
  clientTimeout: 20000
} as const;

function courierChannel(courierId: string): string {
  if (courierId === 'courier-a') {
    return SampleNames.courierAChannel;
  }
  if (courierId === 'courier-b') {
    return SampleNames.courierBChannel;
  }
  throw new Error(`Unknown courier '${courierId}'.`);
}

export {
  SampleNames,
  SampleTimings,
  courierChannel
};
