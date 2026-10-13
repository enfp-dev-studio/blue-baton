// SPDX-License-Identifier: GPL-3.0-only
import { useEffect, useState } from 'react';
import { Group } from '@/components/parts';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Switch } from '@/components/ui/switch';
import { STRINGS, chooseLang, chosenLang, t } from '@/lib/board/i18n.js';
import { type Appearance, chooseAppearance, chosenAppearance } from '@/lib/theme';
import type { Controller } from '@/lib/board/types';

type Invoke = (cmd: string, args?: Record<string, unknown>) => Promise<unknown>;
const invoke: Invoke = (cmd, args) =>
  (window as unknown as { __TAURI__: { core: { invoke: Invoke } } }).__TAURI__.core.invoke(cmd, args);

function Row({ label, id, children }: { label: string; id: string; children: React.ReactNode }) {
  return (
    <div className="flex min-h-[44px] items-center justify-between gap-3 px-3 py-2">
      <label htmlFor={id} className="text-ui">
        {label}
      </label>
      {children}
    </div>
  );
}

/* App-wide preferences: language, light or dark, and (in the app) launch at login */
export function GeneralGroup({ ctl }: { ctl: Controller }) {
  const [appearance, setAppearance] = useState<Appearance>(chosenAppearance);
  const [autostart, setAutostart] = useState<boolean | null>(null);
  useEffect(() => {
    if (!ctl.inApp) return;
    const read = () => invoke('autostart_get').then((v) => setAutostart(!!v)).catch(() => {});
    read();
    window.addEventListener('focus', read); /* it can also be changed from the menu bar menu */
    return () => window.removeEventListener('focus', read);
  }, [ctl.inApp]);
  return (
    <Group title={t('general.title')} className="divide-y">
      <Row label={t('lang.label')} id="bb-lang">
        <Select value={chosenLang() || 'system'} onValueChange={(v) => chooseLang(v === 'system' ? '' : v)}>
          <SelectTrigger id="bb-lang" size="sm" className="text-xs">
            <SelectValue />
          </SelectTrigger>
          <SelectContent align="end">
            <SelectItem value="system">{t('lang.system')}</SelectItem>
            {Object.entries(STRINGS).map(([code, table]) => (
              <SelectItem key={code} value={code}>
                {(table as Record<string, string>)['lang.name']}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
      </Row>
      <Row label={t('general.appearance')} id="bb-theme">
        <Select
          value={appearance || 'auto'}
          onValueChange={(v) => {
            const next = (v === 'auto' ? '' : v) as Appearance;
            setAppearance(next);
            chooseAppearance(next);
          }}
        >
          <SelectTrigger id="bb-theme" size="sm" className="text-xs">
            <SelectValue />
          </SelectTrigger>
          <SelectContent align="end">
            <SelectItem value="auto">{t('theme.auto')}</SelectItem>
            <SelectItem value="light">{t('theme.light')}</SelectItem>
            <SelectItem value="dark">{t('theme.dark')}</SelectItem>
          </SelectContent>
        </Select>
      </Row>
      {ctl.inApp && autostart !== null && (
        <Row label={t('tray.autostart')} id="bb-autostart">
          <Switch
            id="bb-autostart"
            checked={autostart}
            onCheckedChange={(on) => {
              setAutostart(on);
              invoke('autostart_set', { on }).catch(() => setAutostart(!on));
            }}
          />
        </Row>
      )}
    </Group>
  );
}
