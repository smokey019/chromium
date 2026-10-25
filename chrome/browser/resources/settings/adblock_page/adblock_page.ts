// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-adblock-page' is the "Ad blocker" section: the settings of the
 * built-in ad blocker, its filter lists, the user's own filters, the sites it
 * is turned off for, and the log of what it blocked.
 */
import 'chrome://resources/cr_elements/cr_tabs/cr_tabs.js';
import 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';
import '../settings_page/settings_section.js';
import './adblock_settings_tab.js';
import './adblock_lists_tab.js';
import './adblock_filters_tab.js';
import './adblock_sites_tab.js';
import './adblock_log_tab.js';

import {getCss as getCrSharedStyle} from 'chrome://resources/cr_elements/cr_shared_style_lit.css.js';
import {WebUiListenerMixinLit} from 'chrome://resources/cr_elements/web_ui_listener_mixin_lit.js';
import type {CSSResultGroup} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import {CrLitElement, css} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getSearchManager} from '../search_settings.js';
import type {SettingsPlugin} from '../settings_main/settings_plugin.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import type {AdblockState} from './adblock_browser_proxy.js';
import {AdblockBrowserProxyImpl} from './adblock_browser_proxy.js';
import {getHtml} from './adblock_page.html.js';

// Names of the tabs in the address, in the order of the tabs.
const TAB_IDS = ['settings', 'lists', 'filters', 'sites', 'log'];

const SettingsAdblockPageElementBase = WebUiListenerMixinLit(CrLitElement);

export class SettingsAdblockPageElement extends
    SettingsAdblockPageElementBase implements SettingsPlugin {
  static get is() {
    return 'settings-adblock-page';
  }

  static override get styles(): CSSResultGroup {
    return [
      getCrSharedStyle(),
      getSettingsSharedLit(),
      css`
        .master {
          align-items: center;
          display: flex;
          gap: 12px;
          padding: 16px var(--cr-section-padding, 20px) 8px;
        }
        .master .title {
          font-size: 15px;
          font-weight: 500;
        }
        .master .status {
          color: var(--cr-secondary-text-color);
          font-size: 12px;
          margin-top: 2px;
        }
        .grow {
          flex: 1;
        }
        cr-tabs {
          --cr-tabs-height: 40px;
          padding: 0 var(--cr-section-padding, 20px);
        }
        .loading {
          color: var(--cr-secondary-text-color);
          padding: 24px var(--cr-section-padding, 20px);
        }
      `,
    ];
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      state_: {type: Object, state: true},
      tab_: {type: Number, state: true},
    };
  }

  protected accessor state_: AdblockState|null = null;
  protected accessor tab_: number = 0;

  private proxy_ = AdblockBrowserProxyImpl.getInstance();

  override connectedCallback() {
    super.connectedCallback();
    this.addWebUiListener(
        'reba-adblock-state-changed',
        (state: AdblockState) => this.state_ = state);
    this.proxy_.getState().then(state => this.state_ = state);

    // chrome://settings/adblock?tab=log (from the toolbar pop-up) opens the
    // log of blocked requests.
    const tab = new URLSearchParams(window.location.search).get('tab');
    const index = tab ? TAB_IDS.indexOf(tab) : -1;
    if (index >= 0) {
      this.tab_ = index;
    }
  }

  protected onTabChanged_(e: CustomEvent<{value: number}>) {
    this.tab_ = e.detail.value;
  }

  protected onMasterToggle_(e: CustomEvent<boolean>) {
    this.proxy_.setBool('enabled', e.detail);
  }

  protected statusText_(): string {
    const state = this.state_;
    if (!state) {
      return '';
    }
    if (!state.enabled) {
      return 'Off. Nothing is being blocked.';
    }
    if (!state.ready) {
      return state.updating ?
          'Downloading the filter lists for the first time...' :
          'Starting...';
    }
    const enabledLists = state.lists.filter(l => l.enabled).length;
    return `On, with ${enabledLists} filter list${
        enabledLists === 1 ? '' : 's'}.`;
  }

  // SettingsPlugin implementation
  async searchContents(query: string) {
    const searchRequest = await getSearchManager().search(query, this);
    return searchRequest.getSearchResult();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-adblock-page': SettingsAdblockPageElement;
  }
}

customElements.define(
    SettingsAdblockPageElement.is, SettingsAdblockPageElement);
