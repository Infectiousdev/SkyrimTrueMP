import { Injectable } from '@angular/core';
import { TranslocoService } from '@ngneat/transloco';
import { BehaviorSubject, firstValueFrom } from 'rxjs';
import { Sound, SoundService } from './sound.service';

export interface ErrorEvent {
  error:
    | 'wrong_version'
    | 'mods_mismatch'
    | 'client_mods_disallowed'
    | 'wrong_password'
    | 'server_full'
    | 'no_reason'
    | 'bad_uGridsToLoad'
    | 'non_default_install'
    | 'set_time_public_server'
    | 'steam_unavailable';
  data?: Record<any, any>;
}

export interface WrongVersionErrorEvent extends ErrorEvent {
  error: 'wrong_version';
  data: { version: string; expectedVersion: string };
}

/**
 * Why one of the player's mods differs from the first player on the server.
 * Mirrors `ModManifest::Issue` in the server.
 */
export interface ModsMismatchIssue {
  /** 0 plugin, 1 SKSE plugin, 2 archive. */
  kind: number;
  /** 0 missing, 1 extra, 2 different version/content, 3 different load order. */
  reason: number;
  name: string;
}

export interface ModsMismatchErrorEvent extends ErrorEvent {
  error: 'mods_mismatch';
  data: {
    mods: [filename: string, id: string][];
    /** Present when the server compares mod contents; absent on older servers. */
    issues?: ModsMismatchIssue[];
  };
}

export interface SteamUnavailableErrorEvent extends ErrorEvent {
  error: 'steam_unavailable';
  data: { reason: string };
}

/**
 * Mod names come from another player's install, so they must not be able to inject markup
 * into the message, which is rendered as HTML.
 */
function escapeHtml(text: string): string {
  return text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

export interface ClientModsDisallowedErrorEvent extends ErrorEvent {
  error: 'client_mods_disallowed';
  data: { mods: ('SKSE' | 'MO2')[] };
}

export type ErrorEvents =
  | WrongVersionErrorEvent
  | ModsMismatchErrorEvent
  | ClientModsDisallowedErrorEvent
  | SteamUnavailableErrorEvent
  | ErrorEvent;

@Injectable({
  providedIn: 'root',
})
export class ErrorService {
  private error = new BehaviorSubject<string>('');
  public error$ = this.error.asObservable();

  constructor(
    private readonly sound: SoundService,
    private readonly translocoService: TranslocoService,
  ) {}

  getError() {
    return this.error.getValue();
  }

  async setError(error: ErrorEvents | string) {
    this.sound.play(Sound.Fail);
    let message: string;
    if (typeof error === 'string') {
      message = error;
    } else {
      let data: Record<string, any> = error.data;
      switch (error.error) {
        case 'mods_mismatch':
          let mods = '';
          const issues: ModsMismatchIssue[] | undefined = error.data.issues;
          if (issues && issues.length > 0) {
            // The server said exactly what differs from the first player's mods.
            const names = (reason: number) =>
              issues
                .filter(issue => issue.reason === reason)
                .map(issue => escapeHtml(issue.name));
            const sections: [key: string, names: string[]][] = [
              ['MODS_MISMATCH_INSTALL', names(0)],
              ['MODS_MISMATCH_DIFFERENT', names(2)],
              ['MODS_MISMATCH_REMOVE', names(1)],
              ['MODS_MISMATCH_ORDER', names(3)],
            ];
            for (const [key, list] of sections) {
              if (list.length === 0) {
                continue;
              }
              mods += '\n\n<hr>\n';
              mods += await firstValueFrom(
                this.translocoService.selectTranslate(
                  'SERVICE.ERROR.ERRORS.' + key,
                  { mods: `<strong>${list.join(', ')}</strong>` },
                ),
              );
            }
            data = { mods };
            break;
          }
          const install = error.data.mods
            .filter(mod => mod[1] === '0')
            .map(mod => escapeHtml(mod[0]));
          if (install.length > 0) {
            mods += '\n\n<hr>\n';
            mods += await firstValueFrom(
              this.translocoService.selectTranslate(
                'SERVICE.ERROR.ERRORS.MODS_MISMATCH_INSTALL',
                { mods: `<strong>${install.join(', ')}</strong>` },
              ),
            );
          }
          const remove = error.data.mods
            .filter(mod => mod[1] !== '0')
            .map(mod => escapeHtml(mod[0]));
          if (remove.length > 0) {
            mods += '\n\n<hr>\n';
            mods += await firstValueFrom(
              this.translocoService.selectTranslate(
                'SERVICE.ERROR.ERRORS.MODS_MISMATCH_REMOVE',
                { mods: `<strong>${remove.join(', ')}</strong>` },
              ),
            );
          }
          data = { mods };
          break;
        case 'client_mods_disallowed':
          data = { mods: error.data.mods.join(', ') };
          break;
      }
      message = await firstValueFrom(
        this.translocoService.selectTranslate(
          'SERVICE.ERROR.ERRORS.' + error.error.toUpperCase(),
          data,
        ),
      );
    }
    this.error.next(message);
  }

  removeError() {
    this.sound.play(Sound.Ok);
    this.error.next('');
  }
}
