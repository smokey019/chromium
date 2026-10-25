// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Thin wrapper around the `adblock` crate (Brave's adblock-rust) exposing
//! just what the browser needs: building an engine out of filter lists, saving
//! and loading its compiled form, checking network requests, and asking for
//! the cosmetic filtering resources of a page.

use adblock::engine::Engine as AdblockEngine;
use adblock::lists::{FilterFormat, FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::resources::{PermissionMask, Resource};
use adblock::url_parser::{set_domain_resolver, ResolvesDomain};
use std::collections::HashSet;
use std::sync::Once;

/// Permission bit that lets a list use scriptlets which need to be trusted
/// (those named `trusted-*` in uBlock Origin).
const TRUSTED_PERMISSION: PermissionMask = PermissionMask::from_bits(0b0000_0001);

#[cxx::bridge(namespace = "reba_adblock::rust")]
mod ffi {
    /// One filter list to compile into an engine.
    struct ListInput {
        /// Raw text of the list.
        text: String,
        /// True if `text` is in hosts-file format.
        is_hosts: bool,
        /// True if the list may use trusted scriptlets.
        trusted: bool,
    }

    /// What to do with a network request.
    struct NetworkResult {
        /// The request should be blocked.
        blocked: bool,
        /// A blocking filter matched, even if an exception then overrode it.
        matched: bool,
        /// A matching `$important` filter, which no exception can override.
        important: bool,
        /// An exception filter matched. When `blocked` is false and this is
        /// true, a blocking filter was (or would have been) overridden.
        has_exception: bool,
        /// Raw text of the blocking filter, if the engine kept it.
        filter: String,
        /// Raw text of the exception filter, if the engine kept it.
        exception_filter: String,
        /// Index (in the order lists were added) of the list holding
        /// `filter`, or -1 if unknown.
        filter_list: i32,
        /// Same as `filter_list` for `exception_filter`.
        exception_list: i32,
        /// A `data:` URL to serve instead of the real resource (`$redirect`).
        redirect: String,
        /// The URL with tracking parameters removed (`$removeparam`), or
        /// empty if it should be left alone.
        rewritten_url: String,
    }

    /// Cosmetic filtering resources for one page.
    struct CosmeticResult {
        /// CSS selectors to hide with `display: none !important`.
        hide_selectors: Vec<String>,
        /// JSON-encoded procedural filters and filters with actions.
        procedural_actions: Vec<String>,
        /// Class and id selectors excepted from generic hiding.
        exceptions: Vec<String>,
        /// JavaScript of the scriptlets to run in the page.
        injected_script: String,
        /// Generic hiding is disabled for this page.
        generichide: bool,
    }

    unsafe extern "C++" {
        include!("components/reba_adblock/engine/domain_resolver.h");

        /// Returns the offset in `host` at which its registrable domain
        /// (eTLD+1) starts, or 0 if it has none (for example an IP address).
        fn HostDomainStart(host: &str) -> usize;
    }

    extern "Rust" {
        type Engine;

        /// Compiles `lists` into an engine. `resources_json` is a JSON array
        /// of redirect resources and scriptlets and may be empty. Lists that
        /// don't parse are skipped. The resources are bytes rather than a
        /// `&str` because they come from a downloaded file: text that isn't
        /// UTF-8 is ignored instead of aborting at the bridge.
        fn build_engine(lists: Vec<ListInput>, resources_json: &[u8]) -> Box<Engine>;

        /// Loads an engine saved with `serialize`. The result is not valid
        /// (see `is_valid`) if `data` was written by another version.
        fn deserialize_engine(data: &[u8], resources_json: &[u8]) -> Box<Engine>;

        fn is_valid(self: &Engine) -> bool;

        /// Compiled form of the engine, for caching on disk.
        fn serialize(self: &Engine) -> Vec<u8>;

        /// Checks a request. `request_type` uses uBlock Origin's names
        /// ("script", "image", "sub_frame", "main_frame", "xhr", ...).
        /// `method` is the HTTP method ("GET", "POST", ...), which `$method`
        /// filters need. `previously_matched` and `force_exceptions` are for
        /// stacking a second engine on top of another one; see adblock's
        /// `check_network_request_subset`.
        fn check_network(
            self: &Engine,
            url: &str,
            source_url: &str,
            request_type: &str,
            method: &str,
            previously_matched: bool,
            force_exceptions: bool,
        ) -> NetworkResult;

        /// Extra CSP directives to add to the response of a document request
        /// (`$csp`), or an empty string.
        fn csp_directives(self: &Engine, url: &str, source_url: &str, request_type: &str) -> String;

        fn url_cosmetic_resources(self: &Engine, url: &str) -> CosmeticResult;

        /// Selectors for generic hide rules that apply to the given classes
        /// and ids.
        fn hidden_class_id_selectors(
            self: &Engine,
            classes: &Vec<String>,
            ids: &Vec<String>,
            exceptions: &Vec<String>,
        ) -> Vec<String>;
    }
}

struct CxxDomainResolver;

impl ResolvesDomain for CxxDomainResolver {
    fn get_host_domain(&self, host: &str) -> (usize, usize) {
        if host.is_empty() {
            return (0, 0);
        }
        let start = ffi::HostDomainStart(host);
        (if start < host.len() { start } else { 0 }, host.len())
    }
}

fn ensure_domain_resolver() {
    static INIT: Once = Once::new();
    INIT.call_once(|| {
        // Only fails if a resolver was already set.
        let _ = set_domain_resolver(Box::new(CxxDomainResolver));
    });
}

pub struct Engine {
    inner: Option<AdblockEngine>,
}

fn load_resources(engine: &mut AdblockEngine, resources_json: &[u8]) {
    if resources_json.is_empty() {
        return;
    }
    if let Ok(resources) = serde_json::from_slice::<Vec<Resource>>(resources_json) {
        engine.use_resources(resources);
    }
}

fn build_engine(lists: Vec<ffi::ListInput>, resources_json: &[u8]) -> Box<Engine> {
    ensure_domain_resolver();

    // `debug` keeps the original line of every filter, which is how a blocked
    // request is traced back to the list and rule that caused it.
    let mut set = FilterSet::new(true);
    for list in lists {
        let permissions =
            if list.trusted { TRUSTED_PERMISSION } else { PermissionMask::default() };
        let format = if list.is_hosts { FilterFormat::Hosts } else { FilterFormat::Standard };
        set.add_filter_list(list.text, ParseOptions { format, permissions, ..Default::default() });
    }
    let mut engine = AdblockEngine::new_with_filter_set(set);
    load_resources(&mut engine, resources_json);
    Box::new(Engine { inner: Some(engine) })
}

fn deserialize_engine(data: &[u8], resources_json: &[u8]) -> Box<Engine> {
    ensure_domain_resolver();

    let mut engine = AdblockEngine::default();
    if engine.deserialize(data).is_err() {
        return Box::new(Engine { inner: None });
    }
    load_resources(&mut engine, resources_json);
    Box::new(Engine { inner: Some(engine) })
}

fn debug_line(info: &Option<adblock::sourcemap::FilterRuleDebugInfo>) -> (String, i32) {
    match info {
        Some(info) => (
            info.raw_line.clone().unwrap_or_default(),
            info.source_location.as_ref().map_or(-1, |l| l.source_index as i32),
        ),
        None => (String::new(), -1),
    }
}

impl Engine {
    fn is_valid(&self) -> bool {
        self.inner.is_some()
    }

    fn serialize(&self) -> Vec<u8> {
        self.inner.as_ref().map(|e| e.serialize()).unwrap_or_default()
    }

    fn check_network(
        &self,
        url: &str,
        source_url: &str,
        request_type: &str,
        method: &str,
        previously_matched: bool,
        force_exceptions: bool,
    ) -> ffi::NetworkResult {
        let mut out = ffi::NetworkResult {
            blocked: false,
            matched: false,
            important: false,
            has_exception: false,
            filter: String::new(),
            exception_filter: String::new(),
            filter_list: -1,
            exception_list: -1,
            redirect: String::new(),
            rewritten_url: String::new(),
        };
        let Some(engine) = &self.inner else { return out };
        let Ok(request) = Request::new(url, source_url, request_type, method) else { return out };

        let result = engine.check_network_request_subset(
            &request,
            previously_matched,
            force_exceptions,
        );
        out.blocked = result.should_block();
        out.matched = result.filter.is_some();
        out.important = result.important;
        out.has_exception = result.exception.is_some();
        (out.filter, out.filter_list) = debug_line(&result.filter);
        (out.exception_filter, out.exception_list) = debug_line(&result.exception);
        out.redirect = result.redirect.unwrap_or_default();
        out.rewritten_url = result.rewritten_url.unwrap_or_default();
        out
    }

    fn csp_directives(&self, url: &str, source_url: &str, request_type: &str) -> String {
        let Some(engine) = &self.inner else { return String::new() };
        let Ok(request) = Request::new(url, source_url, request_type, "") else {
            return String::new();
        };
        engine.get_csp_directives(&request).unwrap_or_default()
    }

    fn url_cosmetic_resources(&self, url: &str) -> ffi::CosmeticResult {
        let mut out = ffi::CosmeticResult {
            hide_selectors: Vec::new(),
            procedural_actions: Vec::new(),
            exceptions: Vec::new(),
            injected_script: String::new(),
            generichide: false,
        };
        let Some(engine) = &self.inner else { return out };
        let resources = engine.url_cosmetic_resources(url);
        out.hide_selectors = resources.hide_selectors.into_iter().collect();
        out.procedural_actions = resources.procedural_actions.into_iter().collect();
        out.exceptions = resources.exceptions.into_iter().collect();
        out.injected_script = resources.injected_script;
        out.generichide = resources.generichide;
        out
    }

    fn hidden_class_id_selectors(
        &self,
        classes: &Vec<String>,
        ids: &Vec<String>,
        exceptions: &Vec<String>,
    ) -> Vec<String> {
        let Some(engine) = &self.inner else { return Vec::new() };
        let exceptions: HashSet<String> = exceptions.iter().cloned().collect();
        engine.hidden_class_id_selectors(classes, ids, &exceptions)
    }
}
