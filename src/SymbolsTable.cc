#include "SymbolsTable.hpp"
#include "IResultStream.hpp"
SQLITE_EXTENSION_INIT3
#include "VirtualTableCursor.hpp"

#include <string>
#include <vector>
#include <queue>

using namespace clang::clangd::remote;
using clang::clangd::remote::v1::SymbolIndex;

class FuzzyFindStream final : public IResultStream<Symbol> {
  grpc::ClientContext m_ctx;
  std::unique_ptr<grpc::ClientReader<FuzzyFindReply>> m_replyReader;
  FuzzyFindReply m_reply;
public:
  FuzzyFindStream(SymbolIndex::Stub& stub, FuzzyFindRequest& req)
    : m_replyReader(stub.FuzzyFind(&m_ctx, req)) { }

  virtual const Symbol& Current() override {
    return m_reply.stream_result();
  }

  virtual bool Next() override {
    return m_replyReader->Read(&m_reply) && m_reply.has_stream_result();
  }
};

class LookupStream final : public IResultStream<Symbol> {
  grpc::ClientContext m_ctx;
  std::unique_ptr<grpc::ClientReader<LookupReply>> m_replyReader;
  LookupReply m_reply;
public:
  LookupStream(SymbolIndex::Stub& stub, LookupRequest& req)
    : m_replyReader(stub.Lookup(&m_ctx, req)) { }

  virtual const Symbol& Current() override {
    return m_reply.stream_result();
  }

  virtual bool Next() override {
    return m_replyReader->Read(&m_reply) && m_reply.has_stream_result();
  }
};

class MultiFuzzyFindStream final : public IResultStream<Symbol> {
  std::unique_ptr<FuzzyFindStream> m_stream = nullptr;
  SymbolIndex::Stub& m_stub;
  std::vector<std::string> m_names;
  FuzzyFindRequest m_req;

public:
  MultiFuzzyFindStream(SymbolIndex::Stub& stub, FuzzyFindRequest& req, std::vector<std::string> names)
    : m_stub(stub)
    , m_names(names)
    , m_req(req) { }

  virtual const Symbol & Current() override {
    return m_stream->Current();
  }

  virtual bool Next() override {
    if(m_stream == nullptr) {
      if(m_names.size() == 0) {
        return false;
      }
      auto name = m_names.back();
      m_req.set_query(name);
      m_names.pop_back();
      m_stream = std::make_unique<FuzzyFindStream>(m_stub, m_req);

      return Next();
    }

    if(m_stream->Next()) return true;
    m_stream = nullptr;
    return Next();
  }
};

enum {
  SEARCH_NONE = 0,
  SEARCH_FUZZYNAME = 1,
  SEARCH_SCOPE = 2,
  SEARCH_ID = 4,
};

class SymbolsCursor final : public VirtualTableCursor {
  SymbolIndex::Stub& m_stub;
  bool m_eof = false;
  std::unique_ptr<IResultStream<Symbol>> m_stream = nullptr;
  grpc::ClientContext m_ctx;

  std::vector<std::string> m_ids;
  std::vector<std::string> m_scopes;
  std::vector<std::string> m_names;

public:
  SymbolsCursor(SymbolIndex::Stub& stub) : m_stub(stub) {}

  int Close() override {
    return SQLITE_OK;
  }
  int Filter(int idxNum, const char *idxStr, int argc, sqlite3_value **argv) override {
    if(idxNum == SEARCH_ID) {
      m_ids.push_back((const char*)sqlite3_value_text(argv[0]));
    } else {
      FuzzyFindRequest req;
      int argi = 0;
      if(idxNum & SEARCH_FUZZYNAME) {
        m_names.push_back((const char*)sqlite3_value_text(argv[argi++]));
      }

      if(idxNum & SEARCH_SCOPE) {
        m_scopes.push_back((const char*)sqlite3_value_text(argv[argi++]));
      }
    }

    if(m_ids.size() > 0) {
      LookupRequest req;
      for(const auto& id : m_ids) {
        req.add_ids(id);
      }
      m_stream = std::make_unique<LookupStream>(m_stub, req);
    } else {
      FuzzyFindRequest req;
      if(m_scopes.size() > 0) {
        for(const auto& scope : m_scopes) {
          req.add_scopes(scope);
        }
      } else {
        req.set_any_scope(true);
      }

      m_stream = std::make_unique<MultiFuzzyFindStream>(m_stub, req, m_names);
    }

    Next();
    return SQLITE_OK;
  }
  int Next() override {
    m_eof = !m_stream->Next();
    return SQLITE_OK;
  }
  int Eof() override {
    return m_eof;
  }
  int Column(sqlite3_context* ctx, int idxCol) override {
    auto& Current = m_stream->Current();
    switch(idxCol) {
      #define SET_RES(field) do { \
        if(Current.has_ ## field()) { \
          sqlite3_result_text(ctx, Current.field().c_str(), -1, SQLITE_TRANSIENT); \
        } else { sqlite3_result_null(ctx); } } while(0)
      #define SET_RES2(field1, field2) do { \
        if(Current.has_ ## field1() && Current.field1().has_ ## field2()) { \
          sqlite3_result_text(ctx, Current.field1().field2().c_str(), -1, SQLITE_TRANSIENT); \
        } else { sqlite3_result_null(ctx); } } while(0)
      #define SET_RES3(field1, field2, field3) do { \
        if(Current.has_ ## field1() && Current.field1().has_ ## field2() && Current.field1().field2().has_ ## field3()) { \
          sqlite3_result_int(ctx, Current.field1().field2().field3()); \
        } else { sqlite3_result_null(ctx); } } while(0)
      case 0: SET_RES(id); break;
      case 1: SET_RES(name); break;
      case 2: SET_RES(scope); break;
      case 3: SET_RES(signature); break;
      case 4: SET_RES(documentation); break;
      case 5: SET_RES(return_type); break;
      case 6: SET_RES(type); break;
      case 7: SET_RES2(definition, file_path); break;
      case 8: SET_RES3(definition, start, line); break;
      case 9: SET_RES3(definition, start, column); break;
      case 10: SET_RES3(definition, end, line); break;
      case 11: SET_RES3(definition, end, column); break;
      case 12: SET_RES2(canonical_declaration, file_path); break;
      case 13: SET_RES3(canonical_declaration, start, line); break;
      case 14: SET_RES3(canonical_declaration, start, column); break;
      case 15: SET_RES3(canonical_declaration, end, line); break;
      case 16: SET_RES3(canonical_declaration, end, column); break;
      #undef SET_RES
      #undef SET_RES2
      #undef SET_RES3
    }
    return SQLITE_OK;
  }
  sqlite3_int64 RowId() override {
    auto res = strtoll(m_stream->Current().id().c_str(), nullptr, 16);

    errno = 0;
    if(errno) {
      throw std::exception();
    }

    return res;
  }
};

constexpr const char* schema =
  "CREATE TABLE vtable(Id TEXT, Name TEXT, Scope TEXT, " \
  "Signature TEXT, Documentation TEXT, ReturnType TEXT, " \
  "Type TEXT, DefPath TEXT, DefStartLine INT, DefStartCol INT, " \
  "DefEndLine INT, DefEndCol INT, DeclPath TEXT, " \
  "DeclStartLine INT, DeclStartCol INT, DeclEndLine INT, DeclEndCol INT)";

SymbolsTable::SymbolsTable(sqlite3* db, std::unique_ptr<SymbolIndex::Stub> stub)
  : m_stub(std::move(stub)) {
  int err = sqlite3_declare_vtab(db, schema);
  if(err != SQLITE_OK) throw std::exception();
}

int SymbolsTable::BestIndex(sqlite3_index_info* info) {
  // First check if we have a search by id
  for(int i = 0; i < info->nConstraint; i++) {
    auto constraint = info->aConstraint[i];
    if(!constraint.usable) continue;
    if(constraint.iColumn == 0 && constraint.op == SQLITE_INDEX_CONSTRAINT_EQ) {
      info->aConstraintUsage[i].argvIndex = 1;
      info->idxNum = SEARCH_ID;
      info->estimatedCost = 1;
      return SQLITE_OK;
    }
  }

  int argvIndex = 0;

  // Look for a search by name
  for(int i = 0; i < info->nConstraint; i++) {
    auto constraint = info->aConstraint[i];
    if(!constraint.usable) continue;
    if(constraint.iColumn == 1 && constraint.op == SQLITE_INDEX_CONSTRAINT_EQ) {
      info->aConstraintUsage[i].argvIndex = ++argvIndex;
      info->idxNum |= SEARCH_FUZZYNAME;
      info->estimatedCost = 10;
      break;
    }
  }

  // Look for a search by scope
  for(int i = 0; i < info->nConstraint; i++) {
    auto constraint = info->aConstraint[i];
    if(!constraint.usable) continue;
    if(constraint.iColumn == 2 && constraint.op == SQLITE_INDEX_CONSTRAINT_EQ) {
      info->aConstraintUsage[i].argvIndex = ++argvIndex;
      info->idxNum |= SEARCH_SCOPE;
      info->estimatedCost = 10;
      break;
    }
  }

  return SQLITE_OK;
}

std::unique_ptr<VirtualTableCursor> SymbolsTable::Open() {
  return std::make_unique<SymbolsCursor>(*m_stub);
}

int SymbolsTable::Disconnect() {
  return SQLITE_OK;
}

int SymbolsTable::Destroy() {
  return SQLITE_OK;
}

int SymbolsTable::FindFunction(int nArg, const std::string& name, void (**pxFunc)(sqlite3_context*,int,sqlite3_value**), void **ppArg) {
  return 0;
}