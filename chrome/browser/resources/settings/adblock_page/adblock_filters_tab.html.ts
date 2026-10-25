// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {SettingsAdblockFiltersTabElement} from './adblock_filters_tab.js';

export function getHtml(this: SettingsAdblockFiltersTabElement) {
  if (!this.state) {
    return html``;
  }
  // clang-format off
  return html`
      <div class="tab-body">
        <div class="intro">
          Your own filters, one per line. They work like the ones in the lists
          and take effect as soon as you apply them. Lines that start with !
          are comments.
        </div>
        <textarea .value="${this.text_}" spellcheck="false"
            aria-label="My filters" @input="${this.onInput_}"
            placeholder="! Block everything from a site&#10;||ads.example.com^&#10;&#10;! Hide an element&#10;example.com##.sidebar-ad"></textarea>
        <div class="toolbar">
          <cr-button class="action-button" ?disabled="${!this.dirty_}"
              @click="${this.onApply_}">Apply changes</cr-button>
          <cr-button ?disabled="${!this.dirty_}" @click="${this.onRevert_}">
            Revert</cr-button>
          ${this.error_ ? html`
            <span class="error" role="alert">${this.error_}</span>` : html`
            <span class="saved">${this.saved_ ? 'Saved.' : ''}</span>`}
          <span class="grow"></span>
          <cr-button @click="${this.onImportClick_}">Import...</cr-button>
          <cr-button @click="${this.onExport_}">Export...</cr-button>
          <input id="file" type="file" accept=".txt,text/plain"
              @change="${this.onFileChosen_}">
        </div>
        <div class="examples">
          <div><code>||ads.example.com^</code> blocks a site and all of its
            subdomains</div>
          <div><code>/banner/*$image</code> blocks images with a banner folder
            in the address</div>
          <div><code>||tracker.example^$third-party</code> blocks it only when
            another site loads it</div>
          <div><code>@@||example.com/ok.js</code> lets a request through</div>
          <div><code>example.com##.ad-box</code> hides an element on a site
          </div>
          <div><code>example.com#@#.ad-box</code> shows it again</div>
          <div><code>example.com##div:has-text(Sponsored)</code> hides
            elements that contain a text</div>
          <div><code>example.com##+js(set-constant, adsEnabled, false)</code>
            runs a scriptlet</div>
        </div>
      </div>`;
  // clang-format on
}
