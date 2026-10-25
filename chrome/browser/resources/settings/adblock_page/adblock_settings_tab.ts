// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';

import type {CSSResultGroup} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {AdblockState, BoolSetting} from './adblock_browser_proxy.js';
import {AdblockBrowserProxyImpl} from './adblock_browser_proxy.js';
import {getHtml} from './adblock_settings_tab.html.js';
import {adblockSharedStyles} from './adblock_shared_styles.js';

/** The "Settings" tab: on/off switches and totals. */
export class SettingsAdblockSettingsTabElement extends CrLitElement {
  static get is() {
    return 'settings-adblock-settings-tab';
  }

  static override get styles(): CSSResultGroup {
    return adblockSharedStyles;
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      state: {type: Object},
    };
  }

  accessor state: AdblockState|null = null;

  private proxy_ = AdblockBrowserProxyImpl.getInstance();

  protected onToggle_(name: BoolSetting, e: CustomEvent<boolean>) {
    this.proxy_.setBool(name, e.detail);
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-adblock-settings-tab': SettingsAdblockSettingsTabElement;
  }
}

customElements.define(
    SettingsAdblockSettingsTabElement.is, SettingsAdblockSettingsTabElement);
