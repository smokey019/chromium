// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html, nothing} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {FilterListInfo} from './adblock_browser_proxy.js';
import type {SettingsAdblockListsTabElement} from './adblock_lists_tab.js';

interface Group {
  id: string;
  title: string;
  description: string;
}

const GROUPS: Group[] = [
  {
    id: 'default',
    title: 'Built-in',
    description:
        'The lists maintained by uBlock Origin itself. They are the core of ' +
        'the ad blocker.',
  },
  {
    id: 'ads',
    title: 'Ads',
    description: 'More lists that block ads.',
  },
  {
    id: 'privacy',
    title: 'Privacy',
    description: 'Lists that block trackers and tracking parameters.',
  },
  {
    id: 'malware',
    title: 'Malware domains',
    description: 'Lists of websites that spread malware or phishing.',
  },
  {
    id: 'multipurpose',
    title: 'Multipurpose',
    description: 'Lists that block ads, trackers and malware at once.',
  },
  {
    id: 'annoyances',
    title: 'Annoyances',
    description:
        'Cookie notices, pop-ups, social media buttons, chat widgets and ' +
        'other things that get in the way. Off by default.',
  },
  {
    id: 'regions',
    title: 'Regions and languages',
    description: 'Lists for websites in specific countries and languages.',
  },
  {
    id: 'custom',
    title: 'Your lists',
    description: 'Lists you added by their address.',
  },
];

function formatAge(updated: number): string {
  if (!updated) {
    return 'not downloaded yet';
  }
  const seconds = Math.max(0, (Date.now() - updated) / 1000);
  if (seconds < 90) {
    return 'updated just now';
  }
  const minutes = seconds / 60;
  if (minutes < 90) {
    return `updated ${Math.round(minutes)} min ago`;
  }
  const hours = minutes / 60;
  if (hours < 36) {
    return `updated ${Math.round(hours)} h ago`;
  }
  return `updated ${Math.round(hours / 24)} days ago`;
}

function formatSize(bytes: number): string {
  if (bytes < 1024) {
    return `${bytes} B`;
  }
  if (bytes < 1024 * 1024) {
    return `${Math.round(bytes / 1024)} KB`;
  }
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

function renderList(this: SettingsAdblockListsTabElement, list: FilterListInfo) {
  const details: string[] = [];
  if (list.enabled) {
    if (list.updating) {
      details.push('downloading...');
    } else {
      if (list.rules) {
        details.push(`${list.rules.toLocaleString()} rules`);
      }
      details.push(formatAge(list.updated));
      if (list.size) {
        details.push(formatSize(list.size));
      }
    }
  }
  // clang-format off
  return html`
      <div class="row list-row">
        <div class="grow">
          <div class="label">${list.title}</div>
          <div class="sub-label">
            ${details.join(' • ')}
            ${list.error && list.enabled ? html`
              <span class="error">${list.error}</span>` : nothing}
            ${list.supportUrl ? html`
              <a href="${list.supportUrl}" target="_blank" rel="noopener">
                About</a>` : nothing}
          </div>
        </div>
        ${list.custom ? html`
          <cr-button @click="${() => this.proxy_.removeList(list.id)}">
            Remove
          </cr-button>` : nothing}
        <cr-toggle ?checked="${list.enabled}" aria-label="${list.title}"
            @change="${(e: CustomEvent<boolean>) =>
        this.proxy_.setListEnabled(list.id, e.detail)}">
        </cr-toggle>
      </div>`;
  // clang-format on
}

function renderGroup(
    this: SettingsAdblockListsTabElement, group: Group,
    lists: FilterListInfo[]) {
  const visible = lists.filter(l => this.matches_(l));
  if (visible.length === 0 && group.id !== 'custom') {
    return nothing;
  }
  const enabled = lists.filter(l => l.enabled).length;
  // clang-format off
  return html`
      <details ?open="${group.id === 'default' || !!this.filter_}">
        <summary>
          <span class="group-title">${group.title}</span>
          <span class="group-count">${enabled} of ${lists.length} on</span>
        </summary>
        <div class="group-description">${group.description}</div>
        ${visible.map(l => renderList.call(this, l))}
        ${group.id === 'custom' && lists.length === 0 ? html`
          <div class="group-description">
            You haven't added any lists. Add one below.</div>` : nothing}
      </details>`;
  // clang-format on
}

export function getHtml(this: SettingsAdblockListsTabElement) {
  const state = this.state;
  if (!state) {
    return html``;
  }
  // clang-format off
  return html`
      <div class="tab-body">
        <div class="intro">
          Filter lists tell the ad blocker what to block. They are downloaded
          from the address they come from and checked for updates every few
          days. The built-in lists are what uBlock Origin uses by default;
          turn on more for stronger blocking, at the cost of more rules to
          check.
        </div>
        <div class="toolbar">
          <cr-button class="action-button" @click="${this.onUpdate_}"
              ?disabled="${state.updating}">
            ${state.updating ? 'Updating...' : 'Update now'}
          </cr-button>
          <cr-button @click="${this.onReset_}">Reset to defaults</cr-button>
          <span class="grow"></span>
          <cr-input class="search" id="search" placeholder="Search lists"
              @input="${this.onFilter_}"></cr-input>
        </div>
        ${GROUPS.map(g => renderGroup.call(
        this, g, state.lists.filter(l => l.group === g.id)))}
        <div class="group-description" style="margin: 16px 0 4px;">
          Add a list by its address
        </div>
        <div class="add-form">
          <cr-input id="listUrl" label="Address (https://...)"
              ?invalid="${!!this.addError_}"
              error-message="${this.addError_}"></cr-input>
          <cr-input id="listTitle" label="Name (optional)"></cr-input>
          <cr-button class="action-button" @click="${this.onAdd_}">
            Add
          </cr-button>
        </div>
      </div>`;
  // clang-format on
}
