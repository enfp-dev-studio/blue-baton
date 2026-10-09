import { useState } from 'react';
import { Lock, Usb } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { t } from '@/lib/board/i18n.js';
import type { Actions, BoardState, Controller } from '@/lib/board/types';

export function LockCard({ s, a }: { s: BoardState; a: Actions }) {
  const [pin, setPin] = useState('');
  const setting = !s.pinSet;
  return (
    <Card className="mx-auto mt-[8vh] max-w-md text-center">
      <CardContent className="px-8 py-6">
        <form
          onSubmit={(e) => {
            e.preventDefault();
            if (setting) a.setPin(pin);
            else a.unlock(pin);
          }}
        >
          <div className="mx-auto mb-4 grid size-13 place-items-center rounded-2xl bg-primary/10 text-primary">
            <Lock className="size-6" />
          </div>
          <h2 className="text-lg font-semibold">{setting ? t('lock.setTitle') : t('lock.lockedTitle')}</h2>
          <p className="mb-5 mt-1 text-sm text-muted-foreground">{setting ? t('lock.setHint') : t('lock.unlockHint')}</p>
          <div className="flex justify-center gap-2">
            <Input
              type="password"
              inputMode="numeric"
              autoComplete="off"
              maxLength={8}
              placeholder={t('lock.pin')}
              aria-label={t('lock.pin')}
              value={pin}
              onChange={(e) => setPin(e.target.value.replace(/\D/g, ''))}
              className="w-44 rounded-full text-center font-mono tracking-[0.3em]"
            />
            <Button type="submit" className="rounded-full">{setting ? t('lock.set') : t('lock.unlock')}</Button>
          </div>
        </form>
      </CardContent>
    </Card>
  );
}

/* Before the board is linked: the app looks for it by itself; a web page needs a click (Web Serial picker) */
export function ConnectCard({ s, a, ctl }: { s: BoardState; a: Actions; ctl: Controller }) {
  return (
    <Card className="mx-auto mt-[8vh] max-w-md text-center">
      <CardContent className="px-8 py-6">
        <div className="mx-auto mb-4 grid size-13 place-items-center rounded-2xl bg-muted text-muted-foreground">
          <Usb className="size-6" />
        </div>
        <h2 className="text-lg font-semibold">{t('link.disconnected')}</h2>
        <p className="mb-5 mt-1 text-sm text-muted-foreground">{ctl.inApp ? t('link.searching') : t('lock.connectHint')}</p>
        {!ctl.inApp && (
          <Button className="rounded-full" onClick={a.connect} disabled={s.link === 'connecting'}>
            <Usb /> {t('link.connect')}
          </Button>
        )}
      </CardContent>
    </Card>
  );
}
