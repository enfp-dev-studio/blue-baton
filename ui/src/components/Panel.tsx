import { useEffect, useLayoutEffect, useRef } from 'react';
import { LogicalSize } from '@tauri-apps/api/dpi';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { Plus, Settings } from 'lucide-react';
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
      <div ref={card} className="overflow-hidden rounded-xl border bg-popover text-[13px] text-popover-foreground shadow-[0_10px_32px_rgb(0_0_0/0.22)]">
        <div className="flex items-center gap-2 px-3.5 pb-2 pt-3">
          <Logo className="size-5 shrink-0" />
          <span className="font-semibold">{t('app.name')}</span>
          <span className="min-w-0 flex-1" />
          {ready ? (
            <StateTag tone="ok" className="min-w-0 truncate text-[11px] text-muted-foreground [&>span:first-child]:bg-ok">{t('link.info', { transport: s.transport, fw: s.fw || '?' })}</StateTag>
          ) : (
            <StateTag tone={s.link === 'connecting' ? 'busy' : 'idle'} className="text-[11px]">{t('link.disconnected')}</StateTag>
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
              <div className="px-2 pb-1 text-[11px] font-medium text-muted-foreground">{t('panel.sendTo')}</div>
              {hosts.length ? (
                <ul
                  role="radiogroup"
                  aria-label={t('hosts.groupLabel')}
                  className="relative grid list-none p-0"
                  style={{ ['--to' as string]: Math.max(at, 0) }}
                >
                  {at >= 0 && (
                    <li
                      aria-hidden="true"
                      className="pointer-events-none absolute left-0 top-0 h-[44px] w-full translate-y-[calc(var(--to)*44px)] rounded-md bg-primary/10 transition-[translate] duration-500 ease-[cubic-bezier(0.22,0.9,0.24,1)] motion-reduce:transition-none"
                    >
                      <span className="absolute left-0 top-1/2 h-6 w-[3px] -translate-y-1/2 rounded-r-full bg-primary" />
                    </li>
                  )}
                  {hosts.map((n) => {
                    const h = s.hosts.get(n)!;
                    const on = s.activeHost === n;
                    return (
                      <li key={n} data-host={n} style={{ height: ROW }}>
                        <button
                          type="button"
                          role="radio"
                          aria-checked={on}
                          onClick={() => a.selectHost(n)}
                          className="relative flex size-full items-center gap-2.5 rounded-md px-2.5 text-left outline-none hover:bg-accent focus-visible:bg-accent"
                        >
                          <span className={cn('grid size-5 shrink-0 place-items-center rounded-[5px] text-[11px] font-semibold', on ? 'bg-primary text-primary-foreground' : 'bg-muted text-muted-foreground')}>
                            {n}
                          </span>
                          <span className="min-w-0 flex-1 leading-tight">
                            <span className={cn('block truncate', on && 'font-medium')}>{h.name || t('hosts.unnamed')}</span>
                            {h.state !== 'ready' && <StateTag tone={hostStateTone(h.state) as Tone} className="text-[11px]">{hostStateText(h.state)}</StateTag>}
                          </span>
                          <Keycaps combo={s.combos[n - 1]} />
                        </button>
                      </li>
                    );
                  })}
                </ul>
              ) : (
                <p className="px-2 py-1 text-xs text-muted-foreground">{t('hosts.noneActive')}</p>
              )}
            </section>
            <section>
              <div className="px-2 pb-1 text-[11px] font-medium text-muted-foreground">{t('devices.title')}</div>
              {inputs.length ? (
                <ul className="grid list-none p-0">
                  {inputs.map((d) => (
                    <li key={d.id} data-device={d.addr} className="group flex h-9 items-center gap-2.5 rounded-md px-2.5">
                      <span className="grid size-5 shrink-0 place-items-center rounded-[5px] text-muted-foreground transition-shadow group-data-live:text-primary">
                        <DeviceIcon kinds={d.kinds} appearance={d.appearance} className="size-4" />
                      </span>
                      <span className="min-w-0 flex-1 truncate">{d.name || t('common.unnamedDevice')}</span>
                      <StateTag tone={stateTone(d.state) as Tone} className="text-[11px]">{stateText(d.state)}</StateTag>
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
            <Button size="sm" variant="ghost" className="h-7 px-2.5 text-[13px] font-normal" onClick={() => { a.openHostWindow(); invoke('window_show'); }}>
              <Plus /> {t('tray.addHost')}
            </Button>
          )}
          <Button size="sm" variant="ghost" className="h-7 px-2.5 text-[13px] font-normal" onClick={() => invoke('window_show')}>
            {t('panel.settings')}
          </Button>
          <Button size="sm" variant="ghost" className="ml-auto h-7 px-2.5 text-[13px] font-normal text-muted-foreground" onClick={() => invoke('app_quit')}>
            {t('panel.quit')}
          </Button>
        </div>
      </div>
    </div>
  );
}
