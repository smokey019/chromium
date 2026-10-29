// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview The release notes shown in Settings > What's new. Add an
 * entry at the top for every release, written for the people using the
 * browser: what they will notice, not how it was done.
 */

export interface ReleaseNote {
  // The version the release was published as.
  version: string;
  // The day it was published, as YYYY-MM-DD.
  date: string;
  // What changed, most important first.
  changes: string[];
}

export const RELEASE_NOTES: ReleaseNote[] = [
  {
    version: '159.0.8131.0',
    date: '2026-10-29',
    changes: [
      'Updated to Chromium 159, with the latest security fixes.',
      'This "What\'s new" page. It is in Settings, and in the REBA menu ' +
          'as "What\'s new in REBA".',
    ],
  },
  {
    version: '158.0.8127.3',
    date: '2026-10-29',
    changes: [
      'New setting in Ad blocker: "Play videos in the best quality". ' +
          'Streaming players start at the best quality the site offers ' +
          'instead of working their way up. Off unless you turn it on.',
      'discovery+ plays without ads. This came through the REBA filters ' +
          'list, so it reached every version.',
    ],
  },
  {
    version: '158.0.8127.2',
    date: '2026-10-28',
    changes: [
      'Protection log, in Ad blocker: everything REBA stopped, with the ' +
          'reason, and a way to allow it again for that site.',
      'Download guard: a program or archive that a page starts downloading ' +
          'without a click, or from an ad frame, waits until you choose to ' +
          'keep it.',
      'Programs and archives that pages fetch are checked by your antivirus ' +
          'before the page gets them, and are never kept in the browser ' +
          'cache.',
      'Sites can no longer pop up a request to show notifications. A bell ' +
          'in the address bar shows that a site asked.',
      'Pop-ups opened through invisible frames laid over the page are ' +
          'stopped.',
      'New "REBA filters" list with our own fixes for sites. It updates on ' +
          'its own, without a browser update.',
    ],
  },
  {
    version: '158.0.8127.0',
    date: '2026-10-26',
    changes: [
      'Updated to Chromium 158.0.8127.',
      'The "Google Search AI Mode" side panel that came with this Chromium ' +
          'version is turned off.',
    ],
  },
  {
    version: '158.0.8119.7',
    date: '2026-10-24',
    changes: [
      'The pop-up guard also stops the pop-ups that streaming sites open ' +
          'when you click the video or a player button.',
    ],
  },
  {
    version: '158.0.8119.6',
    date: '2026-10-24',
    changes: [
      'Pop-up guard: stops ads that open a new tab when you click anywhere ' +
          'on a page. You can turn it off in Ad blocker ("Stop pop-ups ' +
          'opened by clicking the page").',
      'Pop-up rules in the filter lists now work, and a blocked pop-up ' +
          'never flashes open.',
    ],
  },
  {
    version: '158.0.8119.5',
    date: '2026-10-24',
    changes: [
      'The publisher, company and copyright say REBA everywhere: in ' +
          'Installed apps, in file properties and in About REBA.',
    ],
  },
  {
    version: '158.0.8119.4',
    date: '2026-10-23',
    changes: [
      'Fixed a crash when closing a window.',
      'About 30 ad blocker fixes from a full review, such as pages that ' +
          'never finished loading and forms that lost what was sent.',
    ],
  },
  {
    version: '158.0.8119.3',
    date: '2026-10-23',
    changes: [
      'Fixed: searching for selected text from the right-click menu turned ' +
          'the ad blocker off for the site.',
    ],
  },
  {
    version: '158.0.8119.2',
    date: '2026-10-23',
    changes: [
      'A built-in ad blocker in the style of uBlock Origin: filter lists ' +
          'that update themselves, element hiding, a shield button in the ' +
          'toolbar, "Block element..." in the right-click menu, your own ' +
          'filters, a per-site off switch and a log of what was blocked. ' +
          'Find it in Settings > Ad blocker.',
    ],
  },
  {
    version: '158.0.8119.1',
    date: '2026-10-23',
    changes: [
      'LEET Browser is now REBA Browser, with a new red angus icon.',
    ],
  },
];
