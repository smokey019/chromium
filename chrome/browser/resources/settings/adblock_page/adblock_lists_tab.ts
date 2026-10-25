// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_input/cr_input.js';
import 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';

import type {CrInputElement} from 'chrome://resources/cr_elements/cr_input/cr_input.js';
import type {CSSResultGroup} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import {CrLitElement, css} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {AdblockState, FilterListInfo} from './adblock_browser_proxy.js';
import {AdblockBrowserProxyImpl} from './adblock_browser_proxy.js';
import {getHtml} from './adblock_lists_tab.html.js';
import {adblockSharedStyles} from './adblock_shared_styles.js';

/** The "Filter lists" tab. */
export class SettingsAdblockListsTabElement extends CrLitElement {
  static get is() {
    return 'settings-adblock-lists-tab';
  }

  static override get styles(): CSSResultGroup {
    return [
      adblockSharedStyles,
      css`
        details {
          border-top: var(--cr-separator-line);
        }
        summary {
          align-items: center;
          cursor: pointer;
          display: flex;
          gap: 8px;
          list-style: none;
          padding: 12px 0;
        }
        summary::-webkit-details-marker {
          display: none;
        }
        summary::before {
          content: '\\25B8';
          display: inline-block;
          transition: transform 0.1s;
          width: 14px;
        }
        details[open] > summary::before {
          transform: rotate(90deg);
        }
        .group-title {
          font-weight: 500;
        }
        .group-count {
          color: var(--cr-secondary-text-color);
          font-size: 12px;
        }
        .group-description {
          color: var(--cr-secondary-text-color);
          font-size: 12px;
          margin: -4px 0 8px 22px;
        }
        .list-row {
          margin-left: 22px;
        }
        .add-form {
          align-items: flex-end;
          display: flex;
          flex-wrap: wrap;
          gap: 8px;
        }
        .add-form cr-input {
          flex: 1;
          min-width: 200px;
        }
        .search {
          max-width: 360px;
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
      filter_: {type: String, state: true},
      addError_: {type: String, state: true},
    };
  }

  accessor state: AdblockState|null = null;
  protected accessor filter_: string = '';
  protected accessor addError_: string = '';

  protected proxy_ = AdblockBrowserProxyImpl.getInstance();

  protected onUpdate_() {
    this.proxy_.updateLists();
  }

  protected onReset_() {
    this.proxy_.resetLists();
  }

  protected onFilter_(e: Event) {
    this.filter_ = (e.target as CrInputElement).value.trim().toLowerCase();
  }

  protected async onAdd_() {
    const urlInput = this.shadowRoot.querySelector<CrInputElement>('#listUrl');
    const titleInput =
        this.shadowRoot.querySelector<CrInputElement>('#listTitle');
    if (!urlInput || !titleInput) {
      return;
    }
    const result = await this.proxy_.addList(
        urlInput.value.trim(), titleInput.value.trim());
    if (result.ok) {
      urlInput.value = '';
      titleInput.value = '';
      this.addError_ = '';
    } else {
      this.addError_ = result.error || 'Could not add the list.';
    }
  }

  protected matches_(list: FilterListInfo): boolean {
    if (!this.filter_) {
      return true;
    }
    return `${list.title} ${list.lang} ${list.group2}`.toLowerCase().includes(
        this.filter_);
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-adblock-lists-tab': SettingsAdblockListsTabElement;
  }
}

customElements.define(
    SettingsAdblockListsTabElement.is, SettingsAdblockListsTabElement);
