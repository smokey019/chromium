// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview The "Ad blocker" section's connection to the browser.
 */

// clang-format off
import {sendWithPromise} from 'chrome://resources/js/cr.js';
// clang-format on

export interface FilterListInfo {
  id: string;
  title: string;
  // "default", "ads", "privacy", "malware", "multipurpose", "annoyances",
  // "regions" or "custom".
  group: string;
  group2: string;
  parent: string;
  supportUrl: string;
  lang: string;
  custom: boolean;
  url: string;
  enabled: boolean;
  defaultEnabled: boolean;
  // Milliseconds since the epoch, 0 if never downloaded.
  updated: number;
  size: number;
  rules: number;
  version: string;
  error: string;
  updating: boolean;
}

export interface AdblockState {
  enabled: boolean;
  cosmetic: boolean;
  autoUpdate: boolean;
  showBlockedCount: boolean;
  contextMenu: boolean;
  popupGuard: boolean;
  downloadGuard: boolean;
  antivirus: boolean;
  bestVideoQuality: boolean;
  // Sites where the pop-up guard / the download guard is off.
  popupGuardAllowedSites: string[];
  downloadGuardAllowedSites: string[];
  totalBlocked: number;
  updating: boolean;
  ready: boolean;
  lists: FilterListInfo[];
  userFilters: string;
  trustedSites: string[];
}

// What an entry of the protection log is about: a blocked request ("ads"),
// a refused pop-up, a held download, an antivirus detection or a Safe Browsing
// warning.
export type LogKind = 'ads'|'popup'|'download'|'antivirus'|'safebrowsing';

export interface BlockedRequestInfo {
  kind: LogKind;
  // Downloads: the file name.
  file?: string;
  // Why, as a code (see reasonText_() in adblock_log_tab.ts).
  reason?: string;
  detail?: string;
  // Milliseconds since the epoch: the first time (protection events only).
  first?: number;
  url: string;
  type: string;
  pageHost: string;
  filter: string;
  list: string;
  count: number;
  // Milliseconds since the epoch.
  time: number;
}

// The largest My filters the browser takes, in UTF-8 bytes
// (kMaxUserFiltersBytes in reba_adblock_service.h).
export const MAX_USER_FILTERS_BYTES = 1024 * 1024;

export type BoolSetting = 'enabled'|'cosmetic'|'autoUpdate'|'showBlockedCount'|
    'contextMenu'|'popupGuard'|'downloadGuard'|'antivirus'|'bestVideoQuality';

export type LogAction = 'allowAlways'|'allowOnSite'|'allowSession'|
    'blockAlways'|'allowPopupsOnSite'|'stopHoldingOnSite';

export type ExceptionKind = 'popup'|'download';

export interface AddListResult {
  ok: boolean;
  error?: string;
}

export interface AdblockBrowserProxy {
  getState(): Promise<AdblockState>;
  setBool(name: BoolSetting, value: boolean): void;
  setListEnabled(id: string, enabled: boolean): void;
  addList(url: string, title: string): Promise<AddListResult>;
  removeList(id: string): void;
  updateLists(): void;
  resetLists(): void;
  // Resolves to false if the browser didn't take the text (it is larger than
  // MAX_USER_FILTERS_BYTES).
  setUserFilters(text: string): Promise<boolean>;
  addTrustedSite(site: string): Promise<boolean>;
  removeTrustedSite(site: string): void;
  getLog(): Promise<BlockedRequestInfo[]>;
  clearLog(): void;
  logAction(action: LogAction, url: string, pageHost: string): void;
  // Resolves to false if `site` is not a host name.
  addExceptionSite(kind: ExceptionKind, site: string): Promise<boolean>;
  removeExceptionSite(kind: ExceptionKind, site: string): void;
}

export class AdblockBrowserProxyImpl implements AdblockBrowserProxy {
  getState() {
    return sendWithPromise<AdblockState>('rebaAdblockGetState');
  }

  setBool(name: BoolSetting, value: boolean) {
    chrome.send('rebaAdblockSetBool', [name, value]);
  }

  setListEnabled(id: string, enabled: boolean) {
    chrome.send('rebaAdblockSetListEnabled', [id, enabled]);
  }

  addList(url: string, title: string) {
    return sendWithPromise<AddListResult>('rebaAdblockAddList', url, title);
  }

  removeList(id: string) {
    chrome.send('rebaAdblockRemoveList', [id]);
  }

  updateLists() {
    chrome.send('rebaAdblockUpdateLists');
  }

  resetLists() {
    chrome.send('rebaAdblockResetLists');
  }

  setUserFilters(text: string) {
    return sendWithPromise<boolean>('rebaAdblockSetUserFilters', text);
  }

  addTrustedSite(site: string) {
    return sendWithPromise<boolean>('rebaAdblockAddTrustedSite', site);
  }

  removeTrustedSite(site: string) {
    chrome.send('rebaAdblockRemoveTrustedSite', [site]);
  }

  getLog() {
    return sendWithPromise<BlockedRequestInfo[]>('rebaAdblockGetLog');
  }

  clearLog() {
    chrome.send('rebaAdblockClearLog');
  }

  logAction(action: LogAction, url: string, pageHost: string) {
    chrome.send('rebaAdblockLogAction', [action, url, pageHost]);
  }

  addExceptionSite(kind: ExceptionKind, site: string) {
    return sendWithPromise<boolean>('rebaAdblockAddExceptionSite', kind, site);
  }

  removeExceptionSite(kind: ExceptionKind, site: string) {
    chrome.send('rebaAdblockRemoveExceptionSite', [kind, site]);
  }

  static getInstance(): AdblockBrowserProxy {
    return instance || (instance = new AdblockBrowserProxyImpl());
  }

  static setInstance(obj: AdblockBrowserProxy) {
    instance = obj;
  }
}

let instance: AdblockBrowserProxy|null = null;
