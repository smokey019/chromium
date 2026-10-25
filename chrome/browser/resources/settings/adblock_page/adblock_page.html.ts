// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html, nothing} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {SettingsAdblockPageElement} from './adblock_page.js';

const TAB_NAMES = [
  'Settings',
  'Filter lists',
  'My filters',
  'Trusted sites',
  'Blocked requests',
];

// The index of "My filters" in TAB_NAMES.
const FILTERS_TAB = 2;

function renderTab(this: SettingsAdblockPageElement) {
  const state = this.state_;
  switch (this.tab_) {
    case 0:
      return html`<settings-adblock-settings-tab .state="${state}">
          </settings-adblock-settings-tab>`;
    case 1:
      return html`<settings-adblock-lists-tab .state="${state}">
          </settings-adblock-lists-tab>`;
    case FILTERS_TAB:
      // Always there; see getHtml().
      return nothing;
    case 3:
      return html`<settings-adblock-sites-tab .state="${state}">
          </settings-adblock-sites-tab>`;
    default:
      return html`<settings-adblock-log-tab .active="${true}">
          </settings-adblock-log-tab>`;
  }
}

export function getHtml(this: SettingsAdblockPageElement) {
  const state = this.state_;
  // My filters is only hidden while another tab is shown, the way
  // cr-page-selector keeps its pages, so that edits that weren't applied yet
  // are still there when the user comes back.
  // clang-format off
  return html`<!--_html_template_start_-->
      <settings-section page-title="Ad blocker">
        ${state ? html`
          <div class="master">
            <div class="grow">
              <div class="title">Ad blocker</div>
              <div class="status">${this.statusText_()}</div>
            </div>
            <cr-toggle ?checked="${state.enabled}" aria-label="Ad blocker"
                @change="${this.onMasterToggle_}"></cr-toggle>
          </div>
          <cr-tabs .tabNames="${TAB_NAMES}" .selected="${this.tab_}"
              @selected-changed="${this.onTabChanged_}"></cr-tabs>
          ${renderTab.call(this)}
          <settings-adblock-filters-tab .state="${state}"
              ?hidden="${this.tab_ !== FILTERS_TAB}">
          </settings-adblock-filters-tab>` : html`
          <div class="loading">Loading...</div>`}
      </settings-section>
<!--_html_template_end_-->`;
  // clang-format on
}
