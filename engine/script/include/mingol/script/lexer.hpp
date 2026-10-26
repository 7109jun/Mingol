// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string_view>
#include <vector>

#include "mingol/script/token.hpp"

namespace mingol::script {

// Tokenises a whole file. Never throws and never reads outside `source`; lexical errors are
// reported to `diags` and lexing continues (the offending bytes are skipped). The result always
// ends with a Tok::End token.
std::vector<Token> lex(std::string_view source, std::uint32_t fileIndex, Diagnostics& diags);

}  // namespace mingol::script
