import { Usb } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Logo, StateTag } from '@/components/parts';
import { RADIO, t } from '@/lib/board/i18n.js';
import type { Actions, BoardState, Controller } from '@/lib/board/types';

/* A quiet toolbar: the name, and the board link with what the radio is doing when it is more than waiting */
export function Header({ s, a, ctl }: { s: BoardState; a: Actions; ctl: Controller }) {
  const connected = s.link === 'connected';
  const radioNote = connected && (s.radio === 'scan' || s.radio === 'connect') ? RADIO[s.radio] : '';
  return (
    <header className="sticky top-0 z-20 border-b bg-background/85 backdrop-blur-xl backdrop-saturate-150">
      <div className="mx-auto flex h-12 max-w-[680px] items-center justify-between gap-4 px-4">
        <div className="flex min-w-0 items-center gap-2">
          <Logo className="size-6 shrink-0" />
          <span className="text-[13px] font-semibold">{t('app.name')}</span>
        </div>
        <div className="flex min-w-0 items-center gap-3">
          {connected ? (
            <StateTag tone="ok" className="min-w-0 truncate text-muted-foreground [&>span:first-child]:bg-ok">
              {radioNote || t('link.info', { transport: s.transport, fw: s.fw || '?' })}
            </StateTag>
          ) : (
            <StateTag tone={s.link === 'connecting' ? 'busy' : 'idle'}>
              {s.link === 'connecting' ? t('link.connecting') : t('link.disconnected')}
            </StateTag>
          )}
          {!connected && ctl.needsSerial && (
            <Button size="sm" className="h-7 px-3 text-[13px]" onClick={a.connect} disabled={s.link === 'connecting'}>
              <Usb /> {t('link.connect')}
            </Button>
          )}
          {connected && !ctl.inApp && (
            <Button size="sm" variant="ghost" className="h-7 px-2 text-[13px] text-muted-foreground" onClick={a.disconnectLink} title={t('link.releaseTitle')}>
              {t('link.release')}
            </Button>
          )}
        </div>
      </div>
    </header>
  );
}
