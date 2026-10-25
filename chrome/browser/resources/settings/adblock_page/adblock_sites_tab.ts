// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_input/cr_input.js';

import type {CrInputElement} from 'chrome://resources/cr_elements/cr_input/cr_input.js';
import type {CSSResultGroup} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import {CrLitElement, css} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {AdblockState} from './adblock_browser_proxy.js';
import {AdblockBrowserProxyImpl} from './adblock_browser_proxy.js';
import {adblockSharedStyles} from './adblock_shared_styles.js';
import {getHtml} from './adblock_sites_tab.html.js';

/** The "Trusted sites" tab: sites where nothing is blocked. */
export class SettingsAdblockSitesTabElement extends CrLitElement {
  static get is() {
    return 'settings-adblock-sites-tab';
  }

  static override get styles(): CSSResultGroup {
    return [
      adblockSharedStyles,
      css`
        .add-form {
          align-items: flex-start;
          display: flex;
          gap: 8px;
        }
        .add-form cr-input {
          flex: 1;
          max-width: 420px;
        }
        .add-form cr-button {
          margin-top: 10px;
        }
      `,
    ];
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      state: {type: Object},
      error_: {type: String, state: true},
    };
  }

  accessor state: AdblockState|null = null;
  protected accessor error_: string = '';

  protected proxy_ = AdblockBrowserProxyImpl.getInstance();

  protected async onAdd_() {
    const input = this.shadowRoot.querySelector<CrInputElement>('#site');
    if (!input || !input.value.trim()) {
      return;
    }
    const ok = await this.proxy_.addTrustedSite(input.value);
    if (ok) {
      input.value = '';
      this.error_ = '';
    } else {
      this.error_ = 'That is not a website address.';
    }
  }

  protected onKeydown_(e: KeyboardEvent) {
    if (e.key === 'Enter') {
      this.onAdd_();
    }
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-adblock-sites-tab': SettingsAdblockSitesTabElement;
  }
}

customElements.define(
    SettingsAdblockSitesTabElement.is, SettingsAdblockSitesTabElement);
