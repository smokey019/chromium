// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html, nothing} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {BlockedRequestInfo} from './adblock_browser_proxy.js';
import type {SettingsAdblockLogTabElement} from './adblock_log_tab.js';

function renderActions(
    this: SettingsAdblockLogTabElement, item: BlockedRequestInfo) {
  // clang-format off
  switch (item.kind) {
    case 'popup':
      if (item.reason === 'guard') {
        return html`
            <div class="actions">
              ${item.pageHost ? html`
                <cr-button
                    @click="${() => this.onAction_('allowPopupsOnSite', item)}">
                  Allow pop-ups on ${item.pageHost}</cr-button>` : nothing}
            </div>`;
      }
      break;
    case 'download':
      return html`
          <div class="actions">
            <cr-button @click="${this.onOpenDownloads_}">
              Open downloads</cr-button>
            ${item.pageHost ? html`
              <cr-button
                  @click="${() => this.onAction_('stopHoldingOnSite', item)}">
                Stop holding downloads from ${item.pageHost}</cr-button>` :
                nothing}
          </div>`;
    case 'antivirus':
    case 'safebrowsing':
      return html`<div class="note">${this.noteText_(item)}</div>`;
    default:
      break;
  }
  return html`
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
        ${item.kind === 'ads' ? html`
          <cr-button @click="${() => this.onAction_('blockAlways', item)}">
            Always block</cr-button>` : nothing}
      </div>`;
  // clang-format on
}

function renderItem(
    this: SettingsAdblockLogTabElement, item: BlockedRequestInfo) {
  const key = this.keyOf_(item);
  const open = this.expanded_ === key;
  const what = item.file || item.url;
  // clang-format off
  return html`
      <div class="item">
        <div class="item-summary" title="${item.url}"
            @click="${() => this.toggleExpanded_(key)}">
          <span class="url">
            ${item.kind !== 'ads' ? html`
              <span class="kind kind-${item.kind}">
                ${this.kindLabel_(item.kind)}</span>` : nothing}
            ${what}
          </span>
          <span class="count">${item.count}</span>
          <span class="list-name">${this.reasonText_(item)}</span>
          <span>${this.formatTime_(item.time)}</span>
        </div>
        ${open ? html`
          <div class="detail">
            <dl>
              ${item.file ? html`
                <dt>File</dt><dd>${item.file}</dd>` : nothing}
              <dt>Address</dt><dd class="mono">${item.url}</dd>
              <dt>Why</dt><dd>${this.reasonText_(item)}</dd>
              ${item.filter ? html`
                <dt>Filter</dt><dd class="mono">${item.filter}</dd>` : nothing}
              ${item.type ? html`
                <dt>Kind</dt><dd>${item.type}</dd>` : nothing}
              <dt>Site</dt><dd>${item.pageHost || 'unknown'}</dd>
              <dt>Times</dt><dd>${item.count}</dd>
              ${item.first && item.count > 1 ? html`
                <dt>First</dt>
                <dd>${new Date(item.first).toLocaleString()}</dd>` : nothing}
            </dl>
            ${renderActions.call(this, item)}
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
          What was stopped: blocked ads and trackers (the last 500, until the
          browser closes), and refused pop-ups, held downloads and threats
          (the last 30 days; clearing your history clears them too). Open an
          entry to see why, and to allow it if it was wrong.
        </div>
        <div class="toolbar">
          <cr-input class="search" placeholder="Search"
              @input="${this.onQuery_}"></cr-input>
          <select class="md-select kind-filter" aria-label="Show"
              @change="${this.onKindChange_}">
            <option value="" ?selected="${this.kind_ === ''}">Everything</option>
            <option value="ads" ?selected="${this.kind_ === 'ads'}">
              Ads and trackers</option>
            <option value="popup" ?selected="${this.kind_ === 'popup'}">
              Pop-ups</option>
            <option value="download" ?selected="${this.kind_ === 'download'}">
              Downloads</option>
            <option value="antivirus"
                ?selected="${this.kind_ === 'antivirus'}">Antivirus</option>
            <option value="safebrowsing"
                ?selected="${this.kind_ === 'safebrowsing'}">
              Safe Browsing</option>
          </select>
          <span class="grow"></span>
          <cr-button @click="${() => this.refresh_()}">Refresh</cr-button>
          <cr-button @click="${this.onClear_}">Clear</cr-button>
        </div>
        <div class="head">
          <span>What</span>
          <button class="${this.sortBy_ === 'count' ? 'active' : ''}"
              title="Sort by how many times it happened"
              @click="${() => this.setSort_(
      this.sortBy_ === 'count' ? 'recent' : 'count')}">Times</button>
          <span>Why</span>
          <button class="${this.sortBy_ === 'recent' ? 'active' : ''}"
              title="Sort by the last time it happened"
              @click="${() => this.setSort_('recent')}">Last</button>
        </div>
        ${items.length === 0 ? html`
          <div class="empty">
            ${this.items_.length === 0 ?
        'Nothing was stopped yet.' : 'Nothing matches.'}
          </div>` : items.map(i => renderItem.call(this, i))}
      </div>`;
  // clang-format on
}
