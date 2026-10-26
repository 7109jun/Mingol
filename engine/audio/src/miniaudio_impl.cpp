// SPDX-License-Identifier: GPL-2.0-only
// Compiles miniaudio's device layer only: decoding is done by mingol_assets, mixing by the Mixer.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
