// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_input/cr_input.js';

import type {CrInputElement} from 'chrome://resources/cr_elements/cr_input/cr_input.js';
import {WebUiListenerMixinLit} from 'chrome://resources/cr_elements/web_ui_listener_mixin_lit.js';
import type {CSSResultGroup, PropertyValues} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import {CrLitElement, css} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {BlockedRequestInfo, LogAction, LogKind} from './adblock_browser_proxy.js';
import {AdblockBrowserProxyImpl} from './adblock_browser_proxy.js';
import {getHtml} from './adblock_log_tab.html.js';
import {adblockSharedStyles} from './adblock_shared_styles.js';

export type SortBy = 'recent'|'count';

const SettingsAdblockLogTabElementBase = WebUiListenerMixinLit(CrLitElement);

/**
 * The protection log tab: the requests the ad blocker blocked most recently
 * (at most 500, in memory), and the pop-ups, downloads and threats of the last
 * 30 days (kept on disk by the browser), with why and what to do about each.
 */
export class SettingsAdblockLogTabElement extends
    SettingsAdblockLogTabElementBase {
  static get is() {
    return 'settings-adblock-log-tab';
  }

  static override get styles(): CSSResultGroup {
    return [
      adblockSharedStyles,
      css`
        .head, .item-summary {
          align-items: center;
          display: grid;
          gap: 8px;
          grid-template-columns: minmax(0, 1fr) 64px 160px 92px;
          padding: 6px 4px;
        }
        .head {
          border-bottom: var(--cr-separator-line);
          color: var(--cr-secondary-text-color);
          font-size: 12px;
          font-weight: 500;
        }
        .head button {
          background: none;
          border: none;
          color: inherit;
          cursor: pointer;
          font: inherit;
          padding: 0;
          text-align: start;
        }
        .head button.active {
          color: var(--cr-primary-text-color);
          text-decoration: underline;
        }
        .item {
          border-bottom: var(--cr-separator-line);
        }
        .item-summary {
          cursor: pointer;
        }
        .item-summary:hover {
          background: var(--cr-hover-background-color);
        }
        .url, .list-name {
          overflow: hidden;
          text-overflow: ellipsis;
          white-space: nowrap;
        }
        .count {
          font-variant-numeric: tabular-nums;
        }
        .detail {
          background: var(--cr-hover-background-color);
          padding: 8px 12px 12px;
        }
        .detail dl {
          display: grid;
          gap: 4px 12px;
          grid-template-columns: max-content minmax(0, 1fr);
          margin: 0 0 8px;
        }
        .detail dt {
          color: var(--cr-secondary-text-color);
        }
        .detail dd {
          margin: 0;
          overflow-wrap: anywhere;
        }
        .actions {
          display: flex;
          flex-wrap: wrap;
          gap: 6px;
        }
        .search {
          max-width: 320px;
        }
        .kind-filter {
          background: transparent;
          border: var(--cr-separator-line);
          border-radius: 4px;
          color: inherit;
          font: inherit;
          padding: 4px 6px;
        }
        .kind {
          border-radius: 4px;
          font-size: 11px;
          font-weight: 500;
          margin-inline-end: 6px;
          padding: 1px 6px;
        }
        .kind-popup {
          background: color-mix(in srgb, var(--google-blue-500, #1a73e8) 18%,
              transparent);
        }
        .kind-download {
          background: color-mix(in srgb, var(--google-yellow-500, #f9ab00) 25%,
              transparent);
        }
        .kind-antivirus, .kind-safebrowsing {
          background: color-mix(in srgb, var(--google-red-500, #d93025) 22%,
              transparent);
        }
        .note {
          color: var(--cr-secondary-text-color);
        }
        @media (max-width: 760px) {
          .head, .item-summary {
            grid-template-columns: minmax(0, 1fr) 48px 84px;
          }
          .head :nth-child(3), .item-summary :nth-child(3) {
            display: none;
          }
        }
      `,
    ];
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      active: {type: Boolean},
      items_: {type: Array, state: true},
      query_: {type: String, state: true},
      sortBy_: {type: String, state: true},
      expanded_: {type: String, state: true},
      kind_: {type: String, state: true},
    };
  }

  /** Whether the tab is showing. The log is only fetched then. */
  accessor active: boolean = false;
  protected accessor items_: BlockedRequestInfo[] = [];
  protected accessor query_: string = '';
  protected accessor sortBy_: SortBy = 'recent';
  protected accessor expanded_: string = '';
  // The kind of entries shown; empty for all.
  protected accessor kind_: LogKind|'' = '';

  private proxy_ = AdblockBrowserProxyImpl.getInstance();
  private refreshTimer_: number|null = null;

  override connectedCallback() {
    super.connectedCallback();
    this.addWebUiListener('reba-adblock-log-changed', () => {
      this.refreshSoon_();
    });
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    if (this.refreshTimer_ !== null) {
      clearTimeout(this.refreshTimer_);
      this.refreshTimer_ = null;
    }
  }

  override willUpdate(changedProperties: PropertyValues<this>) {
    super.willUpdate(changedProperties);
    if (changedProperties.has('active') && this.active) {
      this.refresh_();
    }
  }

  private refreshSoon_() {
    if (!this.active || this.refreshTimer_ !== null) {
      return;
    }
    this.refreshTimer_ = window.setTimeout(() => {
      this.refreshTimer_ = null;
      this.refresh_();
    }, 800);
  }

  protected async refresh_() {
    this.items_ = await this.proxy_.getLog();
  }

  protected onQuery_(e: Event) {
    this.query_ = (e.target as CrInputElement).value.trim().toLowerCase();
  }

  protected visibleItems_(): BlockedRequestInfo[] {
    let items = this.items_;
    if (this.kind_) {
      items = items.filter(i => i.kind === this.kind_);
    }
    if (this.query_) {
      items = items.filter(
          i => `${i.url} ${i.file || ''} ${i.list} ${i.filter} ${i.pageHost} ${
                   i.type} ${this.reasonText_(i)}`
                   .toLowerCase()
                   .includes(this.query_));
    }
    // The two logs come one after the other; most recent first, and for
    // "Times" equal counts stay in order of recency (the sort is stable).
    items = [...items].sort((a, b) => b.time - a.time);
    if (this.sortBy_ === 'count') {
      items.sort((a, b) => b.count - a.count);
    }
    return items;
  }

  protected keyOf_(item: BlockedRequestInfo): string {
    return `${item.kind}|${item.url}|${item.pageHost}`;
  }

  protected kindLabel_(kind: LogKind): string {
    switch (kind) {
      case 'popup':
        return 'Pop-up';
      case 'download':
        return 'Download';
      case 'antivirus':
        return 'Antivirus';
      case 'safebrowsing':
        return 'Safe Browsing';
      default:
        return 'Request';
    }
  }

  protected reasonText_(item: BlockedRequestInfo): string {
    switch (item.kind) {
      case 'popup':
        return item.reason === 'guard' ?
            'Pop-up guard: opened by a click that was not on a link for it' :
            `Filter list: ${item.list || 'unknown'}`;
      case 'download':
        switch (item.reason) {
          case 'no-click':
            return 'Held: started without a click';
          case 'other-site-frame':
            return 'Held: started by another site on the page (an ad)';
          case 'popup-tab':
            return 'Held: started in a pop-up another site opened';
          case 'file-type':
            return 'Held: a program from a site you have not visited before';
          default:
            return 'Held';
        }
      case 'antivirus':
        switch (item.reason) {
          case 'download':
            return 'Windows antivirus blocked the download';
          case 'too-large':
            return 'Blocked: too large to check (over 64 MB)';
          default:
            return 'Windows antivirus found a threat';
        }
      case 'safebrowsing':
        switch (item.detail) {
          case 'dangerous-url':
          case 'dangerous-host':
            return 'Safe Browsing: from a dangerous site';
          case 'dangerous-content':
            return 'Safe Browsing: known malware';
          case 'unwanted':
            return 'Safe Browsing: unwanted software';
          case 'uncommon':
            return 'Safe Browsing: rarely downloaded file';
          default:
            return 'Safe Browsing warning';
        }
      default:
        return item.list || '—';
    }
  }

  protected noteText_(item: BlockedRequestInfo): string {
    if (item.kind === 'safebrowsing') {
      return 'The downloads page lets you keep the file if you are sure it ' +
          'is safe.';
    }
    if (item.reason === 'too-large') {
      return 'Files this large that a page fetches in pieces cannot be ' +
          'checked, so they are blocked. Downloading the file normally still ' +
          'works.';
    }
    return 'Windows antivirus found a threat in this file, so it never ' +
        'reached the page or the disk. This cannot be overridden; if you are ' +
        'sure it is a mistake, report it to Microsoft.';
  }

  protected onKindChange_(e: Event) {
    this.kind_ = (e.target as HTMLSelectElement).value as LogKind | '';
  }

  protected onOpenDownloads_() {
    window.open('chrome://downloads');
  }

  protected onClear_() {
    this.proxy_.clearLog();
    this.items_ = [];
    this.expanded_ = '';
  }

  protected onAction_(action: LogAction, item: BlockedRequestInfo) {
    this.proxy_.logAction(action, item.url, item.pageHost);
  }

  protected toggleExpanded_(key: string) {
    this.expanded_ = this.expanded_ === key ? '' : key;
  }

  protected setSort_(sortBy: SortBy) {
    this.sortBy_ = sortBy;
  }

  protected formatTime_(time: number): string {
    const seconds = Math.max(0, Math.round((Date.now() - time) / 1000));
    if (seconds < 60) {
      return `${seconds} s ago`;
    }
    if (seconds < 3600) {
      return `${Math.round(seconds / 60)} min ago`;
    }
    return new Date(time).toLocaleTimeString();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-adblock-log-tab': SettingsAdblockLogTabElement;
  }
}

customElements.define(
    SettingsAdblockLogTabElement.is, SettingsAdblockLogTabElement);
