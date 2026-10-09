import { ArrowRight, Monitor, Plus } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import { Keycaps, StateTag } from '@/components/parts';
import { hostStateText, hostStateTone, t, tParts } from '@/lib/board/i18n.js';
import type { Actions, BoardState, Host, Tone } from '@/lib/board/types';
import { cn } from '@/lib/utils';

type Confirm = (opts: { title: string; body: string; action: () => void }) => void;

/*
 * Three host tiles with the baton: a capsule under the host that receives the input. It slides to the new
 * host on a switch (a transition on --to; vertical on narrow screens). Off with reduced motion.
 */
export function HostsPanel({ s, a, confirm }: { s: BoardState; a: Actions; confirm: Confirm }) {
  const active = s.hosts.get(s.activeHost);
  return (
    <Card className="gap-0 py-0">
      <CardContent className="p-5">
        <div className="mb-4 flex flex-wrap items-end justify-between gap-x-3 gap-y-1">
          <div>
            <span className="block text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">{t('hosts.eyebrow')}</span>
            <h2 className="text-lg font-semibold tracking-tight">{t('hosts.title')}</h2>
          </div>
          {active ? (
            <span className="inline-flex items-center gap-2 text-sm text-muted-foreground">
              <ArrowRight className="size-4 text-primary" />
              <span>{tParts('hosts.sendingTo', { name: <b key="n" className="font-semibold text-foreground">{active.name || t('hosts.unnamed')}</b> })}</span>
            </span>
          ) : (
            <span className="max-w-[46ch] text-sm text-muted-foreground">{s.hosts.size ? t('hosts.hint') : t('hosts.noneActive')}</span>
          )}
        </div>
        <ul
          role="radiogroup"
          aria-label={t('hosts.groupLabel')}
          className="relative grid list-none grid-cols-1 gap-[var(--gap)] p-0 [--gap:10px] sm:grid-cols-3"
          style={{ ['--to' as string]: s.activeHost - 1 }}
        >
          <li
            aria-hidden="true"
            className={cn(
              'pointer-events-none absolute left-0 top-0 rounded-xl',
              'bg-gradient-to-b from-primary/12 to-transparent ring-[1.5px] ring-primary shadow-[0_10px_30px_var(--glow)]',
              'transition-[translate] duration-500 ease-[cubic-bezier(0.22,0.9,0.24,1)] motion-reduce:transition-none',
              'h-[calc((100%-2*var(--gap))/3)] w-full translate-y-[calc(var(--to)*(100%+var(--gap)))]',
              'sm:h-full sm:w-[calc((100%-2*var(--gap))/3)] sm:translate-x-[calc(var(--to)*(100%+var(--gap)))] sm:translate-y-0',
            )}
          />
          {[1, 2, 3].map((n) => (
            <HostTile key={n} n={n} s={s} a={a} host={s.hosts.get(n)} confirm={confirm} />
          ))}
        </ul>
      </CardContent>
    </Card>
  );
}

function HostTile({ n, s, a, host, confirm }: { n: number; s: BoardState; a: Actions; host?: Host; confirm: Confirm }) {
  const on = s.activeHost === n;
  return (
    <li
      data-host={n}
      className={cn(
        'relative flex min-w-0 flex-row items-center rounded-xl border transition-[background-color,box-shadow] sm:min-h-37 sm:flex-col sm:items-stretch',
        on ? 'border-transparent bg-transparent' : host ? 'bg-muted/50' : 'border-dashed bg-transparent',
        'data-live:shadow-[0_0_0_4px_color-mix(in_oklch,var(--primary)_18%,transparent)]',
      )}
    >
      <button
        type="button"
        role="radio"
        aria-checked={on}
        title={t('hosts.pickTitle')}
        onClick={() => a.selectHost(n)}
        className="flex w-full flex-1 flex-col items-start gap-2 rounded-xl p-3.5 text-left outline-none focus-visible:ring-2 focus-visible:ring-ring"
      >
        <span className="flex w-full items-center gap-2">
          <span
            className={cn(
              'grid size-6 shrink-0 place-items-center rounded-lg text-xs font-bold transition-colors',
              on ? 'bg-primary text-primary-foreground' : 'bg-muted text-muted-foreground',
            )}
          >
            {n}
          </span>
          {host && <StateTag tone={hostStateTone(host.state) as Tone}>{hostStateText(host.state)}</StateTag>}
          <Monitor className={cn('ml-auto size-4.5', on ? 'text-primary' : 'text-muted-foreground/70')} />
        </span>
        <span className={cn('max-w-full truncate text-[15px]', host ? 'font-semibold' : 'font-medium text-muted-foreground')}>
          {host ? host.name || t('hosts.unnamed') : t('hosts.empty')}
        </span>
        <Keycaps combo={s.combos[n - 1]} />
      </button>
      <div className="flex shrink-0 items-center justify-end gap-1.5 px-2.5 sm:pb-2.5">
        {host ? (
          <Button
            size="sm"
            variant="ghost"
            className="rounded-full text-destructive hover:bg-destructive/10 hover:text-destructive"
            onClick={() =>
              confirm({
                title: t('common.remove'),
                body: t('confirm.removeHost', { name: host.name || t('confirm.hostName', { n }) }),
                action: () => a.removeHost(n),
              })
            }
          >
            {t('common.remove')}
          </Button>
        ) : (
          <Button size="sm" variant="outline" className="rounded-full" onClick={a.openHostWindow} disabled={s.hostWindow.open}>
            <Plus /> {t('hosts.add')}
          </Button>
        )}
      </div>
    </li>
  );
}
