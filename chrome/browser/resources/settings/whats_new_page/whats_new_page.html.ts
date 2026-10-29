// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {SettingsWhatsNewPageElement} from './whats_new_page.js';

export function getHtml(this: SettingsWhatsNewPageElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
      <settings-section page-title="What's new">
        <div class="intro">
          You're using REBA ${this.runningVersion_}. REBA updates itself in
          the background; About REBA checks for an update right away.
        </div>
        ${this.releases_.map(release => html`
          <div class="release">
            <div class="heading">
              <span class="version">${release.version}</span>
              <span class="date">${this.formatDate_(release.date)}</span>
              ${release.version === this.runningVersion_ ? html`
                <span class="badge">This version</span>` : ''}
            </div>
            <ul>
              ${release.changes.map(change => html`<li>${change}</li>`)}
            </ul>
          </div>`)}
      </settings-section>
<!--_html_template_end_-->`;
  // clang-format on
}
