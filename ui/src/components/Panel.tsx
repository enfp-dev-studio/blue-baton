// SPDX-License-Identifier: GPL-3.0-only
import { useEffect, useLayoutEffect, useRef } from 'react';
import { LogicalSize } from '@tauri-apps/api/dpi';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { Plus, Settings } from 'lucide-react';
import { RadioGroup as RadioGroupPrimitive } from 'radix-ui';
import { Button } from '@/components/ui/button';
import { DeviceIcon, Keycaps, Logo, StateTag } from '@/components/parts';
import { hostStateText, hostStateTone, stateText, stateTone, t } from '@/lib/board/i18n.js';
import type { Controller, Tone } from '@/lib/board/types';
import { useBoard } from '@/lib/useBoard';
import { cn } from '@/lib/utils';

const WIDTH = 340;
const ROW = 44;

function invoke(cmd: string) {
  const app = (window as unknown as { __TAURI__?: { core: { invoke: (c: string) => Promise<unknown> } } }).__TAURI__;
  return app?.core.invoke(cmd).catch(() => {});
}

/*
 * The panel under the menu bar icon (like Google Drive): switch hosts, see the input devices, open settings.
 * A second client of the board (role 'panel'); the settings window stays the place for pairing and removal.
 */
export function Panel({ ctl }: { ctl: Controller }) {
  const s = useBoard(ctl);
  const a = ctl.actions;
  const card = useRef<HTMLDivElement>(null);

  /* Fit the window to the card (it is transparent around it) */
  useLayoutEffect(() => {
    if (!ctl.inApp || !card.current) return;
    const el = card.current;
    const win = getCurrentWindow();
    const fit = () => win.setSize(new LogicalSize(WIDTH, Math.ceil(el.getBoundingClientRect().height) + 20)).catch(() => {});
    const ro = new ResizeObserver(fit);
    ro.observe(el);
    fit();
    return () => ro.disconnect();
  }, [ctl.inApp]);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === 'Escape' && invoke('panel_hide');
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  const ready = s.link === 'connected';
  const inputs = [...s.devices.values()].filter((d) => d.state !== 'offline');
  const hosts = [1, 2, 3].filter((n) => s.hosts.has(n));
  const at = hosts.indexOf(s.activeHost);
  return (
    <div className="p-2.5">
      <div ref={card} className="overflow-hidden border-[1.5px] border-foreground bg-popover text-ui text-popover-foreground shadow-hard">
        <div className="flex items-center gap-2 px-3.5 pb-2 pt-3">
          <Logo className="size-5 shrink-0" />
          <span className="font-semibold">{t('app.name')}</span>
          <span className="min-w-0 flex-1" />
          {ready ? (
            <StateTag tone="ok" className="min-w-0 truncate text-2xs text-muted-foreground [&>span:first-child]:bg-ok">{t('link.info', { transport: s.transport, fw: s.fw || '?' })}</StateTag>
          ) : (
            <StateTag tone={s.link === 'connecting' ? 'busy' : 'idle'} className="text-2xs">{t('link.disconnected')}</StateTag>
          )}
          <Button size="icon" variant="ghost" className="size-7 text-muted-foreground" title={t('panel.settings')} onClick={() => invoke('window_show')}>
            <Settings className="size-4" />
          </Button>
        </div>

        {!ready ? (
          <div className="px-4 pb-5 pt-3 text-center text-xs text-muted-foreground">{t('link.searching')}</div>
        ) : (
          <div className="grid gap-3 px-1.5 pb-2">
            <section>
              <div className="px-2 pb-1 text-2xs font-medium text-muted-foreground">{t('panel.sendTo')}</div>
              {hosts.length ? (
                <RadioGroupPrimitive.Root
                  asChild
                  value={String(s.activeHost)}
                  onValueChange={(v) => a.selectHost(Number(v))}
                  aria-label={t('hosts.groupLabel')}
                >
                  <ul className="relative grid list-none p-0" style={{ ['--to' as string]: Math.max(at, 0) }}>
                    {at >= 0 && (
                      <li
                        aria-hidden="true"
                        className="pointer-events-none absolute left-0 top-0 h-[44px] w-full translate-y-[calc(var(--to)*44px)] bg-primary transition-[translate] duration-500 ease-[cubic-bezier(0.22,0.9,0.24,1)] motion-reduce:transition-none"
                      >
                        <span className="absolute left-0 top-1/2 h-6 w-[4px] -translate-y-1/2 bg-primary-foreground" />
                      </li>
                    )}
                    {hosts.map((n) => {
                      const h = s.hosts.get(n)!;
                      const on = s.activeHost === n;
                      return (
                        <li key={n} data-host={n} style={{ height: ROW }}>
                          <RadioGroupPrimitive.Item
                            value={String(n)}
                            className={cn(
                              'relative flex size-full items-center gap-2.5 px-2.5 text-left outline-none',
                              on ? 'text-primary-foreground' : 'hover:bg-accent focus-visible:bg-accent',
                            )}
                          >
                            <span className={cn('grid size-5 shrink-0 place-items-center rounded-sm text-2xs font-semibold', on ? 'bg-primary-foreground text-primary' : 'border border-foreground')}>
                              {n}
                            </span>
                            <span className="min-w-0 flex-1 leading-tight">
                              <span className={cn('block truncate', on && 'font-medium')}>{h.name || t('hosts.unnamed')}</span>
                              {h.state !== 'ready' && <StateTag tone={hostStateTone(h.state) as Tone} className="text-2xs">{hostStateText(h.state)}</StateTag>}
                            </span>
                            <Keycaps combo={s.combos[n - 1]} />
                          </RadioGroupPrimitive.Item>
                        </li>
                      );
                    })}
                  </ul>
                </RadioGroupPrimitive.Root>
              ) : (
                <p className="px-2 py-1 text-xs text-muted-foreground">{t('hosts.noneActive')}</p>
              )}
            </section>
            <section>
              <div className="px-2 pb-1 text-2xs font-medium text-muted-foreground">{t('devices.title')}</div>
              {inputs.length ? (
                <ul className="grid list-none p-0">
                  {inputs.map((d) => (
                    <li key={d.id} data-device={d.addr} className="group flex h-9 items-center gap-2.5 rounded-md px-2.5">
                      <span className="grid size-5 shrink-0 place-items-center rounded-sm text-muted-foreground transition-shadow group-data-live:bg-primary group-data-live:text-primary-foreground">
                        <DeviceIcon kinds={d.kinds} appearance={d.appearance} className="size-4" />
                      </span>
                      <span className="min-w-0 flex-1 truncate">{d.name || t('common.unnamedDevice')}</span>
                      <StateTag tone={stateTone(d.state) as Tone} className="text-2xs">{stateText(d.state)}</StateTag>
                    </li>
                  ))}
                </ul>
              ) : (
                <p className="px-2 py-1 text-xs text-muted-foreground">{t('tray.noInputs')}</p>
              )}
            </section>
          </div>
        )}

        <div className="flex items-center gap-0.5 border-t px-1.5 py-1.5">
          {ready && s.hosts.size < 3 && (
            <Button size="sm" variant="ghost" className="px-2.5 font-normal" onClick={() => { a.openHostWindow(); invoke('window_show'); }}>
              <Plus /> {t('tray.addHost')}
            </Button>
          )}
          <Button size="sm" variant="ghost" className="px-2.5 font-normal" onClick={() => invoke('window_show')}>
            {t('panel.settings')}
          </Button>
          <Button size="sm" variant="ghost" className="ml-auto px-2.5 font-normal text-muted-foreground" onClick={() => invoke('app_quit')}>
            {t('panel.quit')}
          </Button>
        </div>
      </div>
    </div>
  );
}
