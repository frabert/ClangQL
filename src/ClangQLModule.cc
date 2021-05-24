#include "ClangQLModule.hpp"
SQLITE_EXTENSION_INIT3
#include "RefsTable.hpp"
#include "RelationsTable.hpp"
#include "SymbolsTable.hpp"
#include <grpcpp/grpcpp.h>

#include <stdexcept>
#include <string>
#include <unordered_map>

using namespace clang::clangd::remote;
using clang::clangd::remote::v1::SymbolIndex;

static std::shared_ptr<grpc::Channel> get_channel(std::string addr) {
  static std::unordered_map<std::string, std::shared_ptr<grpc::Channel>>
   channels;

  auto it = channels.find(addr);
  if (it != channels.end()) {
    return it->second;
  } else {
    auto channel =
     grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());

    channels[addr] = channel;

    return channel;
  }
}

std::unique_ptr<VirtualTable> ClangQLModule::Create(sqlite3 *db, int argc,
                                                    const char *const *argv) {
  if (argc != 5) {
    throw std::runtime_error("Invalid number of arguments for table creation");
  }

  auto table_type = std::string{argv[3]};
  auto server_addr = std::string{argv[4]};
  auto channel = get_channel(server_addr);
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
    throw std::runtime_error("Invalid table `" + table_type + "' requested");
  }
}