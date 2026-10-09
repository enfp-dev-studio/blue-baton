import { useEffect, useLayoutEffect, useRef } from 'react';
import { LogicalSize } from '@tauri-apps/api/dpi';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { Lock, Plus, Settings } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { DeviceIcon, Keycaps, Logo, StateTag } from '@/components/parts';
import { hostStateText, hostStateTone, stateText, stateTone, t } from '@/lib/board/i18n.js';
import type { Controller, Tone } from '@/lib/board/types';
import { useBoard } from '@/lib/useBoard';
import { cn } from '@/lib/utils';

const WIDTH = 340;
const ROW = 52;

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

  const ready = s.link === 'connected' && s.unlocked;
  const inputs = [...s.devices.values()].filter((d) => d.state !== 'offline');
  return (
    <div className="p-2.5">
      <div ref={card} className="overflow-hidden rounded-2xl border bg-popover text-popover-foreground shadow-[0_12px_40px_rgb(0_0_0/0.28)]">
        <div className="flex items-center gap-2.5 border-b px-4 py-3">
          <Logo className="size-7 shrink-0" />
          <div className="min-w-0 flex-1 leading-tight">
            <div className="text-sm font-bold tracking-tight">{t('app.name')}</div>
            {s.link === 'connected' ? (
              <StateTag tone="ok" className="text-[11px]">{t('link.info', { transport: s.transport, fw: s.fw || '?' })}</StateTag>
            ) : (
              <StateTag tone={s.link === 'connecting' ? 'busy' : 'idle'} className="text-[11px]">{t('link.disconnected')}</StateTag>
            )}
          </div>
          <Button size="icon" variant="ghost" className="size-8 rounded-full" title={t('panel.settings')} onClick={() => invoke('window_show')}>
            <Settings className="size-4" />
          </Button>
        </div>

        {!ready ? (
          <div className="px-4 py-5 text-center text-sm text-muted-foreground">
            {s.link === 'connected' ? (
              <>
                <Lock className="mx-auto mb-2 size-5" />
                {t('panel.unlockHint')}
              </>
            ) : (
              t('link.searching')
            )}
          </div>
        ) : (
          <div className="grid gap-4 px-3 py-3">
            <section>
              <div className="mb-1.5 px-1 text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">{t('panel.sendTo')}</div>
              <ul
                role="radiogroup"
                aria-label={t('hosts.groupLabel')}
                className="relative grid list-none gap-[var(--gap)] p-0 [--gap:4px]"
                style={{ ['--to' as string]: s.activeHost - 1 }}
              >
                <li
                  aria-hidden="true"
                  className="pointer-events-none absolute left-0 top-0 h-[52px] w-full translate-y-[calc(var(--to)*(52px+var(--gap)))] rounded-xl bg-primary/10 ring-[1.5px] ring-primary transition-[translate] duration-500 ease-[cubic-bezier(0.22,0.9,0.24,1)] motion-reduce:transition-none"
                />
                {[1, 2, 3].map((n) => {
                  const h = s.hosts.get(n);
                  const on = s.activeHost === n;
                  return (
                    <li key={n} data-host={n} style={{ height: ROW }}>
                      <button
                        type="button"
                        role="radio"
                        aria-checked={on}
                        disabled={!h}
                        onClick={() => a.selectHost(n)}
                        className="relative flex size-full items-center gap-2.5 rounded-xl px-2.5 text-left outline-none hover:bg-muted/60 focus-visible:ring-2 focus-visible:ring-ring disabled:opacity-50 disabled:hover:bg-transparent"
                      >
                        <span className={cn('grid size-6 shrink-0 place-items-center rounded-lg text-xs font-bold', on ? 'bg-primary text-primary-foreground' : 'bg-muted text-muted-foreground')}>
                          {n}
                        </span>
                        <span className="min-w-0 flex-1">
                          <span className={cn('block truncate text-sm', h ? 'font-semibold' : 'text-muted-foreground')}>{h ? h.name || t('hosts.unnamed') : t('hosts.empty')}</span>
                          {h && <StateTag tone={hostStateTone(h.state) as Tone} className="text-[11px]">{hostStateText(h.state)}</StateTag>}
                        </span>
                        <Keycaps combo={s.combos[n - 1]} />
                      </button>
                    </li>
                  );
                })}
              </ul>
            </section>
            <section>
              <div className="mb-1.5 px-1 text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">{t('devices.title')}</div>
              {inputs.length ? (
                <ul className="grid gap-1">
                  {inputs.map((d) => (
                    <li key={d.id} data-device={d.addr} className="group flex items-center gap-2.5 rounded-xl px-2.5 py-1.5">
                      <span className="grid size-8 shrink-0 place-items-center rounded-lg bg-primary/10 text-primary transition-shadow group-data-live:shadow-[0_0_0_3px_var(--glow)]">
                        <DeviceIcon kinds={d.kinds} appearance={d.appearance} className="size-4" />
                      </span>
                      <span className="min-w-0 flex-1 truncate text-sm font-medium">{d.name || t('common.unnamedDevice')}</span>
                      <StateTag tone={stateTone(d.state) as Tone} className="text-[11px]">{stateText(d.state)}</StateTag>
                    </li>
                  ))}
                </ul>
              ) : (
                <p className="px-1 text-sm text-muted-foreground">{t('tray.noInputs')}</p>
              )}
            </section>
          </div>
        )}

        <div className="flex items-center gap-1 border-t px-2 py-2">
          {ready && s.hosts.size < 3 && (
            <Button size="sm" variant="ghost" className="rounded-full" onClick={() => { a.openHostWindow(); invoke('window_show'); }}>
              <Plus /> {t('tray.addHost')}
            </Button>
          )}
          <Button size="sm" variant="ghost" className="rounded-full" onClick={() => invoke('window_show')}>
            {t('panel.settings')}
          </Button>
          <Button size="sm" variant="ghost" className="ml-auto rounded-full text-muted-foreground" onClick={() => invoke('app_quit')}>
            {t('panel.quit')}
          </Button>
        </div>
      </div>
    </div>
  );
}
