// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {SettingsAdblockSitesTabElement} from './adblock_sites_tab.js';

export function getHtml(this: SettingsAdblockSitesTabElement) {
  const state = this.state;
  if (!state) {
    return html``;
  }
  // clang-format off
  return html`
      <div class="tab-body">
        <div class="intro">
          Nothing is blocked or hidden on these sites, nor on their
          subdomains. You can also turn the ad blocker off for the site you are
          on from the shield in the toolbar.
        </div>
        <div class="add-form">
          <cr-input id="site" label="Site, for example example.com"
              ?invalid="${!!this.error_}" error-message="${this.error_}"
              @keydown="${this.onKeydown_}"></cr-input>
          <cr-button class="action-button" @click="${this.onAdd_}">
            Add
          </cr-button>
        </div>
        ${state.trustedSites.length === 0 ? html`
          <div class="empty">No trusted sites.</div>` : html`
          <div style="margin-top: 12px;">
            ${state.trustedSites.map(site => html`
              <div class="row">
                <div class="grow label">${site}</div>
                <cr-button
                    @click="${() => this.proxy_.removeTrustedSite(site)}">
                  Remove
                </cr-button>
              </div>`)}
          </div>`}
      </div>`;
  // clang-format on
}
