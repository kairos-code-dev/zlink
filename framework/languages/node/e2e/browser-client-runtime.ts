type BrowserE2eStatus = 'running' | 'passed' | 'failed';

interface BrowserE2eResult {
  readonly name: string;
  readonly status: BrowserE2eStatus;
  readonly error?: string;
}

declare global {
  interface Window {
    __zlinkE2eArgs?: readonly string[];
    __zlinkE2eResult?: BrowserE2eResult;
  }
}

function browserE2eArgs(): readonly string[] {
  return window.__zlinkE2eArgs ?? [];
}

async function runBrowserE2e(name: string, scenario: () => Promise<void>): Promise<void> {
  window.__zlinkE2eResult = { name, status: 'running' };
  try {
    await scenario();
    window.__zlinkE2eResult = { name, status: 'passed' };
  } catch (error) {
    const message = error instanceof Error ? `${error.name}: ${error.message}` : String(error);
    window.__zlinkE2eResult = { name, status: 'failed', error: message };
    console.error(error);
  }
}

export {
  browserE2eArgs,
  runBrowserE2e
};

export type { BrowserE2eResult };
