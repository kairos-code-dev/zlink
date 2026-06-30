import 'reflect-metadata';
import { ZLinkHttpClient } from '@zlink-systems/http-client';
import { loadSampleConfig } from './Configuration/sample-config';
import { ShoppingMallClientScenario } from './shoppingmall-client-scenario';
import { SampleTimings } from '../Shared/Configuration/sample-names';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const apiA = ZLinkHttpClient.create(config.apiAHttpUrl)
    .timeout(SampleTimings.httpTimeout)
    .build();
  const apiB = ZLinkHttpClient.create(config.apiBHttpUrl)
    .timeout(SampleTimings.httpTimeout)
    .build();
  try {
    await new ShoppingMallClientScenario().run(
      apiA,
      apiB,
      AbortSignal.timeout(SampleTimings.workflowTimeout)
    );
  } finally {
    await Promise.allSettled([
      apiA.close(),
      apiB.close()
    ]);
  }
  console.log('shoppingmall=completed');
  console.log('PASS ShoppingMall.Ts');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
