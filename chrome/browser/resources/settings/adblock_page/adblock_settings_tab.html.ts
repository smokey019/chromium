// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {BoolSetting} from './adblock_browser_proxy.js';
import type {SettingsAdblockSettingsTabElement} from './adblock_settings_tab.js';

function renderToggle(
    this: SettingsAdblockSettingsTabElement, name: BoolSetting, label: string,
    subLabel: string, checked: boolean) {
  // clang-format off
  return html`
      <div class="row">
        <div class="grow">
          <div class="label">${label}</div>
          <div class="sub-label">${subLabel}</div>
        </div>
        <cr-toggle ?checked="${checked}" aria-label="${label}"
            @change="${(e: CustomEvent<boolean>) => this.onToggle_(name, e)}">
        </cr-toggle>
      </div>`;
  // clang-format on
}

export function getHtml(this: SettingsAdblockSettingsTabElement) {
  const state = this.state;
  if (!state) {
    return html``;
  }
  // clang-format off
  return html`
      <div class="tab-body">
        <div class="intro">
          Blocks ads, trackers and malicious sites using filter lists, the same
          way uBlock Origin does, and protects you from pop-ups, downloads and
          files that pages push on you. Everything runs inside the browser,
          and the log shows what was stopped and lets you allow it.
        </div>
        ${renderToggle.call(this, 'enabled', 'Block ads and trackers',
        'The main switch. Turn it off to stop all blocking everywhere.',
        state.enabled)}
        ${renderToggle.call(this, 'cosmetic', 'Hide elements and run scriptlets',
        'Cosmetic filtering removes the empty boxes ads leave behind, and ' +
            'scriptlets stop ads that are served as part of the page itself ' +
            '(video ads, for example).',
        state.cosmetic)}
        ${renderToggle.call(this, 'popupGuard',
        'Stop pop-ups opened by clicking the page',
        'Some sites open an ad in a new tab when you click anywhere on them, ' +
            'even on an unrelated link. This refuses a pop-up when the click was ' +
            'not on a link or button for it, whether or not a filter list ' +
            'knows the ad. Blocked pop-ups show up in the log.',
        state.popupGuard)}
        ${renderToggle.call(this, 'downloadGuard',
        'Hold risky downloads that start on their own',
        'Programs, installers, scripts and archives are held for you to keep ' +
            'or delete when a page starts them without you clicking a ' +
            'download link, when an ad on the page starts them, or when they ' +
            'come from a pop-up another site opened. Held downloads show up ' +
            'in the log.',
        state.downloadGuard)}
        ${renderToggle.call(this, 'antivirus',
        'Check programs and archives with Windows antivirus',
        'Before a page gets a program, installer or archive, it is handed to ' +
            'the antivirus Windows uses (Microsoft Defender, or the one you ' +
            'installed). A file it finds a threat in never reaches the page ' +
            'or the disk. Such files are not kept in the browser cache.',
        state.antivirus)}
        <div class="row">
          <div class="grow">
            <div class="label">Quieter notification prompts</div>
            <div class="sub-label">
              Sites can't pop up "Allow notifications?"; a bell in the address
              bar lets you allow a site you want. Change it under
              <a href="chrome://settings/content/notifications"
                  target="_blank">Notifications</a>.
            </div>
          </div>
        </div>
        ${renderToggle.call(this, 'bestVideoQuality',
        'Play videos in the best quality',
        'Streaming players start low and work their way up, and some stay ' +
            'low. This gives them only the best quality the site offers, so ' +
            'videos play at it from the start. On a slow connection they may ' +
            'pause to buffer. Sites decide how high protected shows go in ' +
            'this browser, often below HD.',
        state.bestVideoQuality)}
        ${renderToggle.call(this, 'autoUpdate', 'Update the lists automatically',
        'Download new versions of the filter lists when they are out of date.',
        state.autoUpdate)}
        ${renderToggle.call(this, 'showBlockedCount',
        'Show the number of blocked requests on the icon',
        'The shield in the toolbar shows how many requests were blocked on ' +
            'the current page.',
        state.showBlockedCount)}
        ${renderToggle.call(this, 'contextMenu', 'Add commands to the page menu',
        'Adds "Block element..." and "Turn the ad blocker off for this site" ' +
            'to the menu you get when you right-click a page.',
        state.contextMenu)}
        <div class="row">
          <div class="grow">
            <div class="label">Requests blocked so far</div>
            <div class="sub-label">
              ${state.totalBlocked.toLocaleString()} in total, over all the
              time this browser has been used.
            </div>
          </div>
        </div>
      </div>`;
  // clang-format on
}
