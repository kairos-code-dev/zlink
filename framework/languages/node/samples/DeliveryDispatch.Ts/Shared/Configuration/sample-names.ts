const SampleNames = {
  dispatchChannel: 'deliverydispatch.dispatch',
  routeMesh: 'deliverydispatch.mesh',
  courierStreamNode: 'delivery-courier-stream',
  trackingChannel: 'deliverydispatch.tracking',
  customerStreamNode: 'delivery-customer-stream',
  courierActorType: 'delivery-courier',
  customerActorType: 'delivery-customer'
} as const;

const SampleTimings = {
  requestTimeout: 10000,
  offerDecisionTimeout: 700,
  offerSweepInterval: 50,
  clientTimeout: 30000
} as const;

function courierActorNodeRid(courierId: string): string {
  if (courierId === 'courier-a') {
    return 'courier-node-1';
  }
  if (courierId === 'courier-b') {
    return 'courier-node-2';
  }
  throw new Error(`Unknown courier '${courierId}'.`);
}

export {
  SampleNames,
  SampleTimings,
  courierActorNodeRid
};
