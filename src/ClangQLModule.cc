#include "ClangQLModule.hpp"
SQLITE_EXTENSION_INIT3
#include "RefsTable.hpp"
#include "RelationsTable.hpp"
#include "SymbolsTable.hpp"
#include <grpcpp/grpcpp.h>

#include <string>

using namespace clang::clangd::remote;
using clang::clangd::remote::v1::SymbolIndex;

static std::unique_ptr<VirtualTable> InitTable(sqlite3 *db, int argc,
                                               const char *const *argv) {
  auto table_type = std::string{argv[3]};
  auto server_addr = std::string{argv[4]};

  auto channel =
   grpc::CreateChannel(server_addr, grpc::InsecureChannelCredentials());
  if (table_type == "symbols") {
    return std::make_unique<SymbolsTable>(db, SymbolIndex::NewStub(channel));
  } else if (table_type == "base_of") {
    return std::make_unique<RelationsTable>(db, SymbolIndex::NewStub(channel),
                                            BaseOf);
  } else if (table_type == "overridden_by") {
    return std::make_unique<RelationsTable>(db, SymbolIndex::NewStub(channel),
                                            OverriddenBy);
  } else if (table_type == "refs") {
    return std::make_unique<RefsTable>(db, SymbolIndex::NewStub(channel));
  } else {
    return nullptr;
  }
}

std::unique_ptr<VirtualTable> ClangQLModule::Create(sqlite3 *db, int argc,
                                                    const char *const *argv) {
  return InitTable(db, argc, argv);
}

std::unique_ptr<VirtualTable> ClangQLModule::Connect(sqlite3 *db, int argc,
                                                     const char *const *argv) {
  return InitTable(db, argc, argv);
}