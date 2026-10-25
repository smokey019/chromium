// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_button/cr_button.js';

import type {CSSResultGroup, PropertyValues} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import {CrLitElement, css} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {AdblockState} from './adblock_browser_proxy.js';
import {AdblockBrowserProxyImpl, MAX_USER_FILTERS_BYTES} from './adblock_browser_proxy.js';
import {getHtml} from './adblock_filters_tab.html.js';
import {adblockSharedStyles} from './adblock_shared_styles.js';

const TOO_LARGE_MESSAGE =
    'That is more than My filters can hold (1 MB). Remove some filters, then ' +
    'apply.';

// The size as the browser counts it, in UTF-8.
function byteLength(text: string): number {
  return new TextEncoder().encode(text).length;
}

/** The "My filters" tab: the user's own rules. */
export class SettingsAdblockFiltersTabElement extends CrLitElement {
  static get is() {
    return 'settings-adblock-filters-tab';
  }

  static override get styles(): CSSResultGroup {
    return [
      adblockSharedStyles,
      css`
        textarea {
          background: var(--cr-input-background-color, transparent);
          border: 1px solid var(--cr-input-border-color, #888);
          border-radius: 6px;
          box-sizing: border-box;
          color: var(--cr-primary-text-color);
          font-family: ui-monospace, 'Cascadia Mono', Consolas, monospace;
          font-size: 12px;
          height: 320px;
          line-height: 1.5;
          padding: 8px;
          resize: vertical;
          tab-size: 4;
          white-space: pre;
          width: 100%;
        }
        textarea:focus {
          border-color: var(--cr-focus-outline-color, #1a73e8);
          outline: none;
        }
        .examples {
          color: var(--cr-secondary-text-color);
          font-size: 12px;
          line-height: 1.7;
          margin-top: 16px;
        }
        .examples code {
          background: var(--cr-hover-background-color);
          border-radius: 3px;
          padding: 1px 4px;
        }
        .saved {
          color: var(--cr-secondary-text-color);
        }
        input[type=file] {
          display: none;
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
      text_: {type: String, state: true},
      dirty_: {type: Boolean, state: true},
      saved_: {type: Boolean, state: true},
      error_: {type: String, state: true},
    };
  }

  accessor state: AdblockState|null = null;
  protected accessor text_: string = '';
  protected accessor dirty_: boolean = false;
  protected accessor saved_: boolean = false;
  protected accessor error_: string = '';

  private proxy_ = AdblockBrowserProxyImpl.getInstance();

  override willUpdate(changedProperties: PropertyValues<this>) {
    super.willUpdate(changedProperties);
    // Take the text from the browser, unless the user is in the middle of
    // editing it.
    if (changedProperties.has('state') && this.state && !this.dirty_) {
      this.text_ = this.state.userFilters;
    }
  }

  protected onInput_(e: Event) {
    this.text_ = (e.target as HTMLTextAreaElement).value;
    this.dirty_ = true;
    this.saved_ = false;
  }

  protected async onApply_() {
    const text = this.text_;
    if (byteLength(text) > MAX_USER_FILTERS_BYTES) {
      this.error_ = TOO_LARGE_MESSAGE;
      this.saved_ = false;
      return;
    }
    if (!await this.proxy_.setUserFilters(text)) {
      this.error_ = 'Could not save My filters. Try again.';
      this.saved_ = false;
      return;
    }
    // Keep what was typed while the browser saved.
    if (this.text_ === text) {
      this.dirty_ = false;
    }
    this.saved_ = true;
    this.error_ = '';
  }

  protected onRevert_() {
    this.text_ = this.state ? this.state.userFilters : '';
    this.dirty_ = false;
    this.saved_ = false;
    this.error_ = '';
  }

  protected onExport_() {
    const blob = new Blob([this.text_], {type: 'text/plain'});
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = 'my-filters.txt';
    link.click();
    URL.revokeObjectURL(url);
  }

  protected onImportClick_() {
    this.shadowRoot.querySelector<HTMLInputElement>('#file')!.click();
  }

  protected async onFileChosen_(e: Event) {
    const input = e.target as HTMLInputElement;
    const file = input.files && input.files[0];
    if (!file) {
      return;
    }
    // Don't read a file that can't fit.
    if (file.size > MAX_USER_FILTERS_BYTES) {
      input.value = '';
      this.error_ =
          `${file.name} is larger than 1 MB, more than My filters can hold.`;
      return;
    }
    const text = await file.text();
    input.value = '';
    const separator = this.text_ && !this.text_.endsWith('\n') ? '\n' : '';
    this.text_ = this.text_ + separator + text;
    this.dirty_ = true;
    this.saved_ = false;
    this.error_ = '';
    if (byteLength(this.text_) > MAX_USER_FILTERS_BYTES) {
      this.error_ = TOO_LARGE_MESSAGE;
    }
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-adblock-filters-tab': SettingsAdblockFiltersTabElement;
  }
}

customElements.define(
    SettingsAdblockFiltersTabElement.is, SettingsAdblockFiltersTabElement);
