// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {CSSResultGroup} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import {css} from 'chrome://resources/lit/v3_0/lit.rollup.js';

/** Styles shared by the tabs of the "Ad blocker" section. */
export const adblockSharedStyles: CSSResultGroup = css`
  :host {
    display: block;
  }

  .tab-body {
    padding: 0 var(--cr-section-padding, 20px) 16px;
  }

  .intro {
    color: var(--cr-secondary-text-color);
    line-height: 1.5;
    margin: 12px 0 16px;
  }

  .row {
    align-items: center;
    border-top: var(--cr-separator-line);
    display: flex;
    gap: 12px;
    min-height: 48px;
    padding: 8px 0;
  }

  .row:first-of-type {
    border-top: none;
  }

  .row .grow {
    flex: 1;
    min-width: 0;
  }

  .label {
    color: var(--cr-primary-text-color);
  }

  .sub-label {
    color: var(--cr-secondary-text-color);
    font-size: 12px;
    line-height: 1.4;
    margin-top: 2px;
  }

  .toolbar {
    align-items: center;
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin: 8px 0 12px;
  }

  .toolbar .grow {
    flex: 1;
  }

  .mono {
    font-family: ui-monospace, 'Cascadia Mono', Consolas, monospace;
    font-size: 12px;
  }

  .error {
    color: var(--cr-error-color, var(--google-red-600));
  }

  .empty {
    color: var(--cr-secondary-text-color);
    padding: 24px 0;
    text-align: center;
  }

  a {
    color: var(--cr-link-color);
  }
`;
