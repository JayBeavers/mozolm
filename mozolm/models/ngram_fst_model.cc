// Copyright 2021 MozoLM Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mozolm/models/ngram_fst_model.h"

#include "mozolm/stubs/logging.h"
#include "fst/fst.h"
#include "fst/symbol-table.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"

using fst::StdArc;
using fst::StdVectorFst;
using fst::SymbolTable;

namespace mozolm {
namespace models {
namespace {

// Label that maps to unknown symbols.
const char kUnknownSymbol[] = "<unk>";

}  // namespace

absl::Status NGramFstModel::Read(const ModelStorage &storage) {
  if (storage.model_file().empty()) {
    return absl::InvalidArgumentError("Model file not specified");
  }
  GOOGLE_LOG(INFO) << "Initializing from " << storage.model_file() << " ...";
  std::unique_ptr<fst::StdVectorFst> fst;
  fst.reset(StdVectorFst::Read(storage.model_file()));
  if (!fst) {
    return absl::NotFoundError(absl::StrCat("Failed to read FST from ",
                                            storage.model_file()));
  }
  const SymbolTable *input_symbols = fst->InputSymbols();
  if (input_symbols == nullptr) {
    if (storage.vocabulary_file().empty()) {
      return absl::NotFoundError("FST is missing an input symbol table");
    }
    // Read symbol table from configuration.
    input_symbols = SymbolTable::Read(storage.vocabulary_file());
    if (input_symbols == nullptr) {
      return absl::NotFoundError(absl::StrCat("Failed to read symbols from ",
                                              storage.vocabulary_file()));
    }
    fst->SetInputSymbols(input_symbols);
  }
  oov_label_ = input_symbols->Find(kUnknownSymbol);
  fst_ = std::move(fst);
  model_ = absl::make_unique<const ngram::NGramModel<StdArc>>(*fst_);
  return CheckModel();
}

bool NGramFstModel::UpdateLMCounts(int32 state,
                                       const std::vector<int> &utf8_syms,
                                       int64 count) {
  // Updating counts on read-only model is not supported.
  return true;  // Treat as a no-op.
}

absl::Status NGramFstModel::CheckModel() const {
  if (model_->Error()) {
    return absl::InternalError("Model initialization failed");
  } else if (!model_->CheckTopology()) {
    return absl::InternalError(
        "FST topology does not correspond to a valid language model");
  } else if (!model_->CheckNormalization()) {
    return absl::InternalError("FST states are not fully normalized");
  }
  return absl::OkStatus();
}

}  // namespace models
}  // namespace mozolm