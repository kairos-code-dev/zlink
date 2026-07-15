// MON-B2: monitoring 등록 검증 시나리오를 검증한다.
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonB2(options: ClientOptions): Promise<void> {
  const evidence = await Promise.all([
    postJson<string>(options.triggerUrl, '/validation/registration/duplicate-source', {}),
    postJson<string>(options.triggerUrl, '/validation/registration/interval', {}),
    postJson<string>(options.triggerUrl, '/validation/registration/missing-spot', {}),
    postJson<string>(options.triggerUrl, '/validation/registration/missing-socket', {})
  ]);

  ensure(
    evidence.some((line) => line.includes('mon-b2|duplicate=') && line.includes('Duplicate monitoring source')),
    'MON-B2 duplicate source validation evidence missing.'
  );
  ensure(
    evidence.some((line) => line.includes('mon-b2|interval=') && line.includes('must be a positive integer')),
    'MON-B2 interval validation evidence missing.'
  );
  ensure(
    evidence.some((line) => line.includes('mon-b2|missing-spot=not registered')),
    'MON-B2 missing spot validation evidence missing.'
  );
  ensure(
    evidence.some((line) => line.includes('mon-b2|missing-socket=not registered')),
    'MON-B2 missing socket validation evidence missing.'
  );

  console.log('scenario MON-B2 passed');
}
