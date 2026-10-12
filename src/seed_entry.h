#pragma once
#include "seed_ocr.h"
#include "terminal.h"

namespace td {
SecretBytes ReadRecoveryWords(Terminal& terminal);
// Empty result requests a fresh capture. Every accepted phrase has been reviewed and validated.
std::optional<SecretBytes> ReviewSeedWords(Terminal& terminal, SeedDraft& draft);
}
