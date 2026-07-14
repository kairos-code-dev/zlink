import type { ClientOptions } from '../Support/client-options';
import { expectStartupFailure } from '../Support/process-support';
import { ensure } from '../Support/scenario-assert';

// RC-A6 verifies that duplicate handler registration fails during startup.
export async function runRcA6(options: ClientOptions): Promise<void> {
  const output = await expectStartupFailure(
    options.invalidMain,
    ['--config', options.invalidConfig],
    options.logDir,
    'invalid-duplicate'
  );
  ensure(output.includes('Duplicate') || output.includes('duplicate'), 'RC-A6 expected duplicate registration error output.');
  console.log('scenario RC-A6 passed');
}
