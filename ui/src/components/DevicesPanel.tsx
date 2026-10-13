// SPDX-License-Identifier: GPL-3.0-only
import { Button } from '@/components/ui/button';
import { DropdownMenuItem, DropdownMenuLabel, DropdownMenuSeparator } from '@/components/ui/dropdown-menu';
import { DeviceIcon, Group, RowMenu, StateTag } from '@/components/parts';
import { FAIL_STAGE, kindText, reasonText, stateText, stateTone, t } from '@/lib/board/i18n.js';
import type { Actions, BoardState, Device, Tone } from '@/lib/board/types';
import { cn } from '@/lib/utils';

type Confirm = (opts: { title: string; body: string; action: () => void }) => void;

function subline(d: Device) {
  if (d.state === 'failed') return FAIL_STAGE[d.error as string] || t('devices.failed');
  if (d.state === 'ready') return d.bond === 'restored' ? t('devices.restored') : d.bond === 'new' ? t('devices.newBond') : '';
  if (d.state === 'offline') return d.auto ? reasonText(d.lastReason) || t('devices.autoHint') : t('devices.autoOff');
  return '';
}

export function DevicesPanel({ s, a, confirm }: { s: BoardState; a: Actions; confirm: Confirm }) {
  const list = [...s.devices.values()].sort((x, y) => (y.lastSeq || 0) - (x.lastSeq || 0));
  const inputs = s.inputs || list.filter((d) => d.paired).length;
  const full = inputs >= s.inputsMax;
  return (
    <div className="grid gap-2">
      <Group n={2} title={t('devices.title')} aside={t('devices.summary', { n: inputs, max: s.inputsMax })}>
        {list.length ? (
          <ul className="list-none divide-y p-0">
            {list.map((d) => (
              <DeviceRow key={d.id} d={d} a={a} confirm={confirm} />
            ))}
          </ul>
        ) : (
          <div className="px-4 py-6 text-center">
            <p className="font-medium">{t('devices.empty')}</p>
            <p className="mx-auto mt-1 max-w-[46ch] text-xs text-muted-foreground">{t('devices.emptyHint')}</p>
          </div>
        )}
      </Group>
      <div className="flex justify-end">
        <Button
          size="sm"
          variant="outline"
          onClick={a.openAdd}
          disabled={s.hostWindow.open || full}
          title={full ? t('devices.full') : s.hostWindow.open ? t('devices.addBlocked') : undefined}
        >
          {t('devices.addItem')}
        </Button>
      </div>
    </div>
  );
}

function DeviceRow({ d, a, confirm }: { d: Device; a: Actions; confirm: Confirm }) {
  const connected = d.state !== 'offline' && d.state !== 'failed';
  const tone = stateTone(d.state) as Tone;
  const sub = [kindText(d.kinds, d.appearance), subline(d)].filter(Boolean).join(' · ');
  /* The one action worth a button; the rest sit in the row menu */
  const primary =
    d.state === 'failed'
      ? { label: t('devices.retry'), run: () => a.connectDevice(d.id) }
      : !connected && d.paired && !d.auto
        ? { label: t('devices.autoOn'), run: () => a.connectDevice(d.id) }
        : null;
  return (
    <li data-device={d.addr} className="group flex min-h-[52px] items-center gap-3 py-2 pl-4 pr-2">
      <span
        className={cn(
          'grid size-7 shrink-0 place-items-center rounded-md bg-muted text-muted-foreground transition-shadow',
          tone === 'ok' && 'bg-primary text-primary-foreground',
          'group-data-live:shadow-[0_0_0_3px_var(--glow)]',
        )}
      >
        <DeviceIcon kinds={d.kinds} appearance={d.appearance} className="size-4" />
      </span>
      <div className="min-w-0 flex-1 leading-tight">
        <div className="truncate font-medium">{d.name || t('common.unnamedDevice')}</div>
        {sub && <div className="line-clamp-2 text-xs text-muted-foreground">{sub}</div>}
      </div>
      <StateTag tone={tone} className="shrink-0">{stateText(d.state)}</StateTag>
      {primary && (
        <Button size="xs" variant="outline" onClick={primary.run}>
          {primary.label}
        </Button>
      )}
      <RowMenu label={t('common.more')}>
        <DropdownMenuLabel className="font-mono text-2xs font-normal text-muted-foreground">{d.id}</DropdownMenuLabel>
        {(connected || d.paired) && <DropdownMenuSeparator />}
        {connected && <DropdownMenuItem onSelect={() => a.disconnectDevice(d.addr)}>{t('devices.disconnect')}</DropdownMenuItem>}
        {d.paired && (
          <DropdownMenuItem
            variant="destructive"
            onSelect={() =>
              confirm({ title: t('common.remove'), body: t('confirm.removeDevice', { name: d.name || d.id }), action: () => a.removeDevice(d) })
            }
          >
            {t('devices.removeItem')}
          </DropdownMenuItem>
        )}
      </RowMenu>
    </li>
  );
}
