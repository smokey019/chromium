// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Brand-specific types and constants for Chromium.

#ifndef CHROME_INSTALL_STATIC_CHROMIUM_INSTALL_MODES_H_
#define CHROME_INSTALL_STATIC_CHROMIUM_INSTALL_MODES_H_

#include <array>

#include "chrome/app/chrome_dll_resource.h"
#include "chrome/common/chrome_icon_resources_win.h"
#include "chrome/install_static/install_constants.h"

namespace install_static {

// The brand-specific company name to be included as a component of the install
// and user data directory paths. May be empty if no such dir is to be used.
inline constexpr wchar_t kCompanyPathName[] = L"";

// The brand-specific product name to be included as a component of the install
// and user data directory paths.
inline constexpr wchar_t kProductPathName[] = L"REBA Browser";

// The brand-specific safe browsing client name.
inline constexpr char kSafeBrowsingName[] = "chromium";

// Note: This list of indices must be kept in sync with the brand-specific
// resource strings in chrome/installer/util/prebuild/create_string_rc.
enum InstallConstantIndex {
  CHROMIUM_INDEX,
  NUM_INSTALL_MODES,
};

inline constexpr auto kInstallModes = std::to_array<InstallConstants>({
    // The primary (and only) install mode for Chromium.
    {
        .size = sizeof(InstallConstants),
        .index = CHROMIUM_INDEX,  // The one and only mode for Chromium.
        .install_switch =
            "",  // No install switch for the primary install mode.
        .install_suffix =
            L"",  // Empty install_suffix for the primary install mode.
        .logo_suffix = L"",  // No logo suffix for the primary install mode.
        // REBA: must match browser_appid in chrome/updater/branding.gni.
        .app_guid = L"{FFF3E393-9DAE-4711-958F-8F1B32972F9B}",
        .base_app_name = L"REBA Browser",      // A distinct base_app_name.
        .base_app_id = L"REBABrowser",         // A distinct base_app_id.
        .browser_prog_id_prefix = L"REBAHTM",  // Browser ProgID prefix.
        .browser_prog_id_description =
            L"REBA HTML Document",  // Browser ProgID description.
        .direct_launch_url_scheme = "rebabrowser",
        .pdf_prog_id_prefix = L"REBAPDF",  // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"REBA PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{C424A6A8-3C7C-4600-8427-64C74CA034FC}",  // Active Setup
                                                        // GUID.
        .toast_activator_clsid = {0x3876F57F,
                                  0xE1A6,
                                  0x4B9B,
                                  {0x84, 0xA3, 0x72, 0x33, 0x67, 0xFE, 0x35,
                                   0x12}},  // Toast Activator CLSID.
        .elevator_clsid = {0xFEBB2EE4,
                           0x6F68,
                           0x485C,
                           {0x99, 0x80, 0xF0, 0xC2, 0xE8, 0x33, 0x2E,
                            0x5F}},  // Elevator CLSID.
        .elevator_iid = {0xbb19a0e5,
                         0xc6,
                         0x4966,
                         {0x94, 0xb2, 0x5a, 0xfe, 0xc6, 0xfe, 0xd9,
                          0x3a}},  // IElevator IID and TypeLib
        // {BB19A0E5-00C6-4966-94B2-5AFEC6FED93A}.
        .old_elevator_iids = {},
        .tracing_service_clsid = {0xEF645E45,
                                  0x9427,
                                  0x4F31,
                                  {0x90, 0xB7, 0x98, 0xCB, 0xB0, 0x76, 0x20,
                                   0xFD}},  // SystemTraceSession CLSID.
        .tracing_service_iid = {0xe0b03e2d,
                                0x7682,
                                0x4d83,
                                {0xb9, 0xff, 0x45, 0x74, 0xaf, 0x72, 0x05,
                                 0x00}},  // ISystemTraceSessionChromium IID and
                                          // TypeLib
        .old_tracing_service_iids = {},
        .default_channel_name = L"",  // Empty default channel name (stable).
        .channel_strategy = ChannelStrategy::FLOATING,
        .supports_system_level = true,  // Supports system-level installs.
        .supports_set_as_default_browser =
            true,  // Supports in-product set as default browser UX.
        .app_icon_resource_index =
            icon_resources::kApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_MAINFRAME,  // App icon resource id.
        .html_doc_icon_resource_index =
            icon_resources::kHtmlDocIndex,  // HTML doc icon resource index.
        .pdf_doc_icon_resource_index =
            icon_resources::kPDFDocIndex,  // PDF doc icon resource index.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-"
            L"1938553385-"
            L"924012160-",  // App container sid prefix for sandbox.
    },
});

}  // namespace install_static

#endif  // CHROME_INSTALL_STATIC_CHROMIUM_INSTALL_MODES_H_
