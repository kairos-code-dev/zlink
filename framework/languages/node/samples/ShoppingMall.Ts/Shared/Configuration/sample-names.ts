const SampleNames = {
  orderWorkflowRouteChannel: 'shoppingmall.order.workflow.route'
} as const;

const SampleTimings = {
  requestTimeout: 10000,
  httpTimeout: 30000,
  workflowTimeout: 30000
} as const;

export {
  SampleNames,
  SampleTimings
};
