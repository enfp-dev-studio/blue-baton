import { Plus } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import { DeviceIcon, StateTag } from '@/components/parts';
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
  return (
    <Card className="gap-0 py-0">
      <CardContent className="p-5">
        <div className="mb-3 flex flex-wrap items-end justify-between gap-x-3 gap-y-1">
          <div>
            <span className="block text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">{t('devices.eyebrow')}</span>
            <h2 className="text-lg font-semibold tracking-tight">{t('devices.title')}</h2>
          </div>
          <span className="text-sm text-muted-foreground">{t('devices.summary', { bonds: s.bonds, max: s.bondsMax || '?' })}</span>
        </div>
        {list.length ? (
          <ul className="grid gap-1">
            {list.map((d) => (
              <DeviceRow key={d.id} d={d} a={a} confirm={confirm} />
            ))}
          </ul>
        ) : (
          <div className="py-6 text-center">
            <p>{t('devices.empty')}</p>
            <p className="mt-1 text-sm text-muted-foreground">{t('devices.emptyHint')}</p>
          </div>
        )}
        <Button
          variant="outline"
          className="mt-3 rounded-full"
          onClick={a.openAdd}
          disabled={s.hostWindow.open}
          title={s.hostWindow.open ? t('devices.addBlocked') : undefined}
        >
          <Plus /> {t('devices.add')}
        </Button>
      </CardContent>
    </Card>
  );
}

function DeviceRow({ d, a, confirm }: { d: Device; a: Actions; confirm: Confirm }) {
  const connected = d.state !== 'offline' && d.state !== 'failed';
  const tone = stateTone(d.state) as Tone;
  const sub = subline(d);
  return (
    <li data-device={d.addr} className="group grid grid-cols-[44px_minmax(0,1fr)] items-center gap-x-3.5 gap-y-2 rounded-xl p-2 transition-colors hover:bg-muted/60 sm:grid-cols-[44px_minmax(0,1fr)_auto]">
      <span
        className={cn(
          'grid size-11 place-items-center rounded-[13px] bg-muted text-muted-foreground transition-shadow',
          tone === 'ok' && 'bg-primary/10 text-primary',
          tone === 'busy' && 'text-busy',
          tone === 'bad' && 'text-destructive',
          'group-data-live:shadow-[0_0_0_4px_var(--glow)]',
        )}
      >
        <DeviceIcon kinds={d.kinds} appearance={d.appearance} className="size-5.5" />
      </span>
      <div className="min-w-0">
        <div className="flex flex-wrap items-center gap-2.5">
          <span className="break-words font-semibold">{d.name || t('common.unnamedDevice')}</span>
          <StateTag tone={tone}>{stateText(d.state)}</StateTag>
        </div>
        <div className="flex flex-wrap items-baseline gap-3 text-sm text-muted-foreground">
          <span>{kindText(d.kinds, d.appearance)}</span>
          <span className="font-mono text-xs text-muted-foreground/70">{d.id}</span>
        </div>
        {sub && <div className="text-sm text-muted-foreground/80">{sub}</div>}
      </div>
      <div className="col-start-2 flex gap-1 sm:col-start-auto">
        {connected ? (
          <Button size="sm" variant="outline" className="rounded-full" onClick={() => a.disconnectDevice(d.addr)}>
            {t('devices.disconnect')}
          </Button>
        ) : d.state === 'failed' ? (
          <Button size="sm" variant="outline" className="rounded-full" onClick={() => a.connectDevice(d.id)}>
            {t('devices.retry')}
          </Button>
        ) : d.paired && !d.auto ? (
          <Button size="sm" variant="outline" className="rounded-full" onClick={() => a.connectDevice(d.id)}>
            {t('devices.autoOn')}
          </Button>
        ) : null}
        {d.paired && (
          <Button
            size="sm"
            variant="ghost"
            className="rounded-full text-destructive hover:bg-destructive/10 hover:text-destructive"
            onClick={() =>
              confirm({ title: t('common.remove'), body: t('confirm.removeDevice', { name: d.name || d.id }), action: () => a.removeDevice(d) })
            }
          >
            {t('common.remove')}
          </Button>
        )}
      </div>
    </li>
  );
}
