// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html, nothing} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {BlockedRequestInfo} from './adblock_browser_proxy.js';
import type {SettingsAdblockLogTabElement} from './adblock_log_tab.js';

function renderItem(
    this: SettingsAdblockLogTabElement, item: BlockedRequestInfo) {
  const open = this.expanded_ === item.url;
  // clang-format off
  return html`
      <div class="item">
        <div class="item-summary" title="${item.url}"
            @click="${() => this.toggleExpanded_(item.url)}">
          <span class="url">${item.url}</span>
          <span class="count">${item.count}</span>
          <span class="list-name">${item.list || '—'}</span>
          <span>${this.formatTime_(item.time)}</span>
        </div>
        ${open ? html`
          <div class="detail">
            <dl>
              <dt>Address</dt><dd class="mono">${item.url}</dd>
              <dt>Blocked by</dt><dd>${item.list || 'unknown'}</dd>
              ${item.filter ? html`
                <dt>Filter</dt><dd class="mono">${item.filter}</dd>` : nothing}
              <dt>Kind</dt><dd>${item.type}</dd>
              <dt>Page</dt><dd>${item.pageHost || 'unknown'}</dd>
              <dt>Times blocked</dt><dd>${item.count}</dd>
            </dl>
            <div class="actions">
              <cr-button @click="${() => this.onAction_('allowAlways', item)}">
                Allow always</cr-button>
              ${item.pageHost ? html`
                <cr-button
                    @click="${() => this.onAction_('allowOnSite', item)}">
                  Allow on ${item.pageHost}</cr-button>` : nothing}
              <cr-button
                  @click="${() => this.onAction_('allowSession', item)}">
                Allow for this session</cr-button>
              <cr-button @click="${() => this.onAction_('blockAlways', item)}">
                Always block</cr-button>
            </div>
          </div>` : nothing}
      </div>`;
  // clang-format on
}

export function getHtml(this: SettingsAdblockLogTabElement) {
  const items = this.visibleItems_();
  // clang-format off
  return html`
      <div class="tab-body">
        <div class="intro">
          The last 500 addresses that were blocked. If a page doesn't work,
          look here for what was blocked on it, and which list blocked it,
          then allow it.
        </div>
        <div class="toolbar">
          <cr-input class="search" placeholder="Search"
              @input="${this.onQuery_}"></cr-input>
          <span class="grow"></span>
          <cr-button @click="${() => this.refresh_()}">Refresh</cr-button>
          <cr-button @click="${this.onClear_}">Clear</cr-button>
        </div>
        <div class="head">
          <span>Address</span>
          <button class="${this.sortBy_ === 'count' ? 'active' : ''}"
              title="Sort by how many times it was blocked"
              @click="${() => this.setSort_(
      this.sortBy_ === 'count' ? 'recent' : 'count')}">Times</button>
          <span>List</span>
          <button class="${this.sortBy_ === 'recent' ? 'active' : ''}"
              title="Sort by the time it was last blocked"
              @click="${() => this.setSort_('recent')}">Last</button>
        </div>
        ${items.length === 0 ? html`
          <div class="empty">
            ${this.items_.length === 0 ?
        'Nothing was blocked yet.' : 'Nothing matches your search.'}
          </div>` : items.map(i => renderItem.call(this, i))}
      </div>`;
  // clang-format on
}
