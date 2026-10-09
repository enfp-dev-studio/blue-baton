import { Usb } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Logo, StateTag } from '@/components/parts';
import { RADIO, t } from '@/lib/board/i18n.js';
import type { Actions, BoardState, Controller } from '@/lib/board/types';
import { cn } from '@/lib/utils';

export function Header({ s, a, ctl }: { s: BoardState; a: Actions; ctl: Controller }) {
  const connected = s.link === 'connected';
  const busyRadio = s.radio !== 'idle';
  return (
    <header className="sticky top-0 z-20 border-b bg-background/80 backdrop-blur-xl backdrop-saturate-150">
      <div className="mx-auto flex max-w-[900px] flex-wrap items-center justify-between gap-x-4 gap-y-2 px-4 py-3">
        <div className="flex min-w-0 items-center gap-2.5">
          <Logo className="size-8 shrink-0" />
          <div className="flex min-w-0 flex-col leading-tight">
            <span className="text-base font-bold tracking-tight">{t('app.name')}</span>
            <span className="hidden text-xs text-muted-foreground md:block">{t('app.tagline')}</span>
          </div>
        </div>
        <div className="flex flex-wrap items-center gap-1.5">
          <span className="inline-flex items-center rounded-full border bg-muted/60 px-3 py-1">
            {connected ? (
              <StateTag tone="ok">{t('link.info', { transport: s.transport, fw: s.fw || '?' })}</StateTag>
            ) : (
              <StateTag tone={s.link === 'connecting' ? 'busy' : 'idle'}>
                {s.link === 'connecting' ? t('link.connecting') : t('link.disconnected')}
              </StateTag>
            )}
          </span>
          {connected && (
            <span className={cn('hidden rounded-full px-2.5 py-1 text-xs sm:inline', busyRadio ? 'bg-primary/10 text-primary' : 'text-muted-foreground')}>
              {RADIO[s.radio] || s.radio}
            </span>
          )}
          {!connected && ctl.needsSerial && (
            <Button size="sm" className="rounded-full" onClick={a.connect} disabled={s.link === 'connecting'}>
              <Usb /> {t('link.connect')}
            </Button>
          )}
          {connected && !ctl.inApp && (
            <Button size="sm" variant="ghost" className="rounded-full text-muted-foreground" onClick={a.disconnectLink} title={t('link.releaseTitle')}>
              {t('link.release')}
            </Button>
          )}
        </div>
      </div>
    </header>
  );
}
