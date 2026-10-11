import { Button } from '@/components/ui/button';
import { DropdownMenuItem } from '@/components/ui/dropdown-menu';
import { Group, Keycaps, RowMenu, StateTag } from '@/components/parts';
import { hostStateText, hostStateTone, t, tParts } from '@/lib/board/i18n.js';
import type { Actions, BoardState, Host, Tone } from '@/lib/board/types';
import { cn } from '@/lib/utils';

type Confirm = (opts: { title: string; body: string; action: () => void }) => void;

const ROW = 52;

/*
 * The three host slots as rows, with the baton: a short bar at the left edge of the host that receives the input.
 * It slides to the new host on a switch (a transition on --to). Off with reduced motion.
 */
export function HostsPanel({ s, a, confirm }: { s: BoardState; a: Actions; confirm: Confirm }) {
  const active = s.hosts.get(s.activeHost);
  return (
    <Group
      title={t('hosts.title')}
      aside={
        active
          ? tParts('hosts.sendingTo', { name: <b key="n" className="font-medium text-foreground">{active.name || t('hosts.unnamed')}</b> })
          : s.hosts.size ? undefined : t('hosts.noneActive')
      }
      footer={t('hosts.hint')}
    >
      <ul
        role="radiogroup"
        aria-label={t('hosts.groupLabel')}
        className="relative list-none divide-y p-0"
        style={{ ['--to' as string]: s.activeHost - 1 }}
      >
        {s.hosts.has(s.activeHost) && (
          <li
            aria-hidden="true"
            className="pointer-events-none absolute left-0 top-0 z-10 flex h-[52px] items-center translate-y-[calc(var(--to)*52px)] transition-[translate] duration-500 ease-[cubic-bezier(0.22,0.9,0.24,1)] motion-reduce:transition-none"
          >
            <span className="h-7 w-[3px] rounded-r-full bg-primary" />
          </li>
        )}
        {[1, 2, 3].map((n) => (
          <HostRow key={n} n={n} s={s} a={a} host={s.hosts.get(n)} confirm={confirm} />
        ))}
      </ul>
    </Group>
  );
}

function HostRow({ n, s, a, host, confirm }: { n: number; s: BoardState; a: Actions; host?: Host; confirm: Confirm }) {
  const on = !!host && s.activeHost === n;
  return (
    <li
      data-host={n}
      className={cn('flex items-center gap-2 pr-2 transition-colors', on && 'bg-primary/[0.07]', 'data-live:bg-primary/[0.12]')}
      style={{ height: ROW }}
    >
      <button
        type="button"
        role="radio"
        aria-checked={on}
        disabled={!host}
        title={host ? t('hosts.pickTitle') : undefined}
        onClick={() => a.selectHost(n)}
        className="flex h-full min-w-0 flex-1 items-center gap-3 pl-4 text-left outline-none focus-visible:bg-accent enabled:hover:bg-accent/60"
      >
        <span
          className={cn(
            'grid size-5 shrink-0 place-items-center rounded-[5px] text-[11px] font-semibold',
            on ? 'bg-primary text-primary-foreground' : 'bg-muted text-muted-foreground',
          )}
        >
          {n}
        </span>
        <span className="min-w-0 flex-1 leading-tight">
          <span className={cn('block truncate', host ? 'font-medium' : 'text-muted-foreground')}>
            {host ? host.name || t('hosts.unnamed') : t('hosts.empty')}
          </span>
          {host && <StateTag tone={hostStateTone(host.state) as Tone}>{hostStateText(host.state)}</StateTag>}
        </span>
        <Keycaps combo={s.combos[n - 1]} className="pr-1" />
      </button>
      {host ? (
        <RowMenu label={t('common.more')}>
          <DropdownMenuItem
            variant="destructive"
            onSelect={() =>
              confirm({
                title: t('common.remove'),
                body: t('confirm.removeHost', { name: host.name || t('confirm.hostName', { n }) }),
                action: () => a.removeHost(n),
              })
            }
          >
            {t('hosts.removeItem')}
          </DropdownMenuItem>
        </RowMenu>
      ) : (
        <Button size="xs" variant="outline" className="h-6 px-2.5 text-xs" onClick={a.openHostWindow} disabled={s.hostWindow.open}>
          {t('hosts.add')}
        </Button>
      )}
    </li>
  );
}
