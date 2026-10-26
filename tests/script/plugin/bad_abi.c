/* SPDX-License-Identifier: GPL-2.0-only */
#include <mingol/script/plugin_abi.h>
MS_PLUGIN_EXPORT uint32_t mingol_plugin_abi(void) { return 999u; }
MS_PLUGIN_EXPORT int32_t mingol_plugin_init(const MsHostApi* api, const MsRegistrar* reg) { (void)api; (void)reg; return 0; }
