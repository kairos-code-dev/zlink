// RC-A6: 잘못된 등록은 시작 단계에서 차단 시나리오를 검증한다.
import type { ClientOptions } from '../Support/client-options';
import { expectStartupFailure } from '../Support/process-support';
import { ensure } from '../Support/scenario-assert';

// RC-A6 verifies that each invalid registration axis fails during startup.
export async function runRcA6(options: ClientOptions): Promise<void> {
  const cases = [
    { name: 'duplicate', config: options.invalidDuplicateConfig, expected: ['duplicate'] },
    { name: 'missing-handler-group', config: options.invalidHandlerGroupConfig, expected: ['handler', 'server'] },
    { name: 'mixed-channel-kinds', config: options.invalidChannelKindsConfig, expected: ['duplicate', 'channel'] }
  ] as const;
  for (const invalidCase of cases) {
    const output = await expectStartupFailure(
      options.invalidMain,
      ['--config', invalidCase.config],
      options.logDir,
      `invalid-${invalidCase.name}`
    );
    const normalized = output.toLowerCase();
    ensure(
      invalidCase.expected.every((part) => normalized.includes(part)),
      `RC-A6 expected ${invalidCase.name} registration error output.`
    );
  }
  console.log('scenario RC-A6 passed');
}
