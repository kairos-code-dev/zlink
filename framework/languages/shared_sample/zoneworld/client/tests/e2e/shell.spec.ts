import { expect, test } from '@playwright/test';

test('game shell exposes the authoritative world controls', async ({ page }) => {
  await page.goto('/game.html');
  await expect(page.getByRole('heading', { name: 'ZoneWorld', exact: true })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Enter world' })).toBeVisible();
  await expect(page.getByLabel('ZoneWorld map')).toBeVisible();
});

test('operations shell starts without polling or an implicit connection', async ({ page }) => {
  await page.goto('/ops.html');
  await expect(page.getByRole('heading', { name: 'ZoneWorld Ops' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Connect console' })).toBeVisible();
  await expect(page.getByText('No polling. Every transition arrives from the server.')).toBeVisible();
});
