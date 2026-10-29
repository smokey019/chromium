// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-whats-new-page' is the "What's new" section: the release notes of
 * this version and the ones before it (release_notes.ts). The REBA menu opens
 * it too ("What's new in REBA").
 */
import '../settings_page/settings_section.js';

import {getCss as getCrSharedStyle} from 'chrome://resources/cr_elements/cr_shared_style_lit.css.js';
import type {CSSResultGroup} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import {CrLitElement, css} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {loadTimeData} from '../i18n_setup.js';
import type {SearchResult} from '../search_settings.js';
import type {SettingsPlugin} from '../settings_main/settings_plugin.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import type {ReleaseNote} from './release_notes.js';
import {RELEASE_NOTES} from './release_notes.js';
import {getHtml} from './whats_new_page.html.js';

// The version of this browser, such as 159.0.8131.0.
function getRunningVersion(): string {
  const match = loadTimeData.getString('aboutBrowserVersion')
                    .match(/\d+\.\d+\.\d+\.\d+/);
  return match ? match[0] : '';
}

export class SettingsWhatsNewPageElement extends CrLitElement implements
    SettingsPlugin {
  static get is() {
    return 'settings-whats-new-page';
  }

  static override get styles(): CSSResultGroup {
    return [
      getCrSharedStyle(),
      getSettingsSharedLit(),
      css`
        .intro {
          color: var(--cr-secondary-text-color);
          line-height: 1.5;
          padding: 16px var(--cr-section-padding, 20px) 4px;
        }
        .release {
          border-top: var(--cr-separator-line);
          padding: 16px var(--cr-section-padding, 20px);
        }
        .heading {
          align-items: baseline;
          display: flex;
          flex-wrap: wrap;
          gap: 12px;
        }
        .version {
          color: var(--cr-primary-text-color);
          font-size: 15px;
          font-weight: 500;
        }
        .date {
          color: var(--cr-secondary-text-color);
          font-size: 12px;
        }
        .badge {
          background: var(--color-sys-primary-container,
              var(--cr-fallback-color-primary-container));
          border-radius: 10px;
          color: var(--color-sys-on-primary-container,
              var(--cr-fallback-color-on-primary-container));
          font-size: 11px;
          font-weight: 500;
          padding: 2px 8px;
        }
        ul {
          line-height: 1.5;
          margin: 8px 0 0;
          padding-inline-start: 20px;
        }
        li + li {
          margin-top: 4px;
        }
      `,
    ];
  }

  override render() {
    return getHtml.bind(this)();
  }

  protected releases_: ReleaseNote[] = RELEASE_NOTES;
  protected runningVersion_: string = getRunningVersion();

  protected formatDate_(date: string): string {
    const [year, month, day] = date.split('-').map(Number);
    return new Date(year!, month! - 1, day!).toLocaleDateString(
        undefined, {year: 'numeric', month: 'long', day: 'numeric'});
  }

  // SettingsPlugin implementation. Old release notes would turn up for many
  // searches without being what anyone looked for, so this section stays out
  // of the results.
  searchContents(query: string): Promise<SearchResult> {
    return Promise.resolve(
        {canceled: false, matchCount: 0, wasClearSearch: query === ''});
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-whats-new-page': SettingsWhatsNewPageElement;
  }
}

customElements.define(
    SettingsWhatsNewPageElement.is, SettingsWhatsNewPageElement);
