#include "RelationsTable.hpp"
#include "IResultStream.hpp"
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT3
#include "VirtualTableCursor.hpp"

#include <string>
#include <vector>
#include <queue>

using namespace clang::clangd::remote;
using clang::clangd::remote::v1::SymbolIndex;

class RelationStream final : public IResultStream<Relation> {
  grpc::ClientContext m_ctx;
  std::unique_ptr<grpc::ClientReader<RelationsReply>> m_replyReader;
  RelationsReply m_reply;
public:
  RelationStream(SymbolIndex::Stub& stub, RelationsRequest& req)
    : m_replyReader(stub.Relations(&m_ctx, req)) { }

  virtual const Relation& Current() override {
    return m_reply.stream_result();
  }

  virtual bool Next() override {
    return m_replyReader->Read(&m_reply) && m_reply.has_stream_result();
  }
};

class RelationsCursor final : public VirtualTableCursor {
  SymbolIndex::Stub& m_stub;
  RelationKind m_kind;
  bool m_eof = false;
  std::unique_ptr<IResultStream<Relation>> m_stream = nullptr;
  grpc::ClientContext m_ctx;

  std::vector<std::string> m_subjects;
public:
  RelationsCursor(SymbolIndex::Stub& stub, RelationKind kind)
    : m_stub(stub)
    , m_kind(kind) {}

  int Eof() override {
    return m_eof;
  }

  int Next() override {
    m_eof = !m_stream->Next();
    return SQLITE_OK;
  }

  int Close() override {
    return SQLITE_OK;
  }

  sqlite3_int64 RowId() override {
    auto a = strtoll(m_stream->Current().subject_id().c_str(), nullptr, 16);
    auto b = strtoll(m_stream->Current().object().id().c_str(), nullptr, 16);

    // Use Cantor's pairing function to generate a new unique id from two unique ids
    auto t = a + b;
    return (t >> 1) * (t + 1) + b;
  }

  int Column(sqlite3_context *ctx, int idxCol) override {
    switch(idxCol) {
      case 0: sqlite3_result_text(ctx, m_stream->Current().subject_id().c_str(), -1, SQLITE_TRANSIENT); break;
      case 1: sqlite3_result_text(ctx, m_stream->Current().object().id().c_str(), -1, SQLITE_TRANSIENT); break;
    }
    return SQLITE_OK;
  }

  int Filter(int idxNum, const char *idxStr, int argc, sqlite3_value **argv) override {
    if(idxNum) {
      m_subjects.push_back((const char*)sqlite3_value_text(argv[0]));
    }

    RelationsRequest req;
    req.set_predicate(m_kind);
    for(const auto& subj : m_subjects) {
      req.add_subjects(subj);
    }

    m_stream = std::make_unique<RelationStream>(m_stub, req);
    Next();
    return SQLITE_OK;
  }
};

constexpr const char* schema = "CREATE TABLE vtable(Subject TEXT, Object TEXT)";

RelationsTable::RelationsTable(sqlite3* db, std::unique_ptr<SymbolIndex::Stub> stub, RelationKind kind)
  : m_stub(std::move(stub))
  , m_kind(kind) {
  if(sqlite3_declare_vtab(db, schema) != SQLITE_OK) {
    throw std::exception();
  }
}

int RelationsTable::BestIndex(sqlite3_index_info* info) {
  for(int i = 0; i < info->nConstraint; i++) {
    auto constraint = info->aConstraint[i];
    if(constraint.usable && constraint.iColumn == 0 && constraint.op == SQLITE_INDEX_CONSTRAINT_EQ) {
      info->aConstraintUsage[i].argvIndex = 1;
      info->estimatedCost = 1;
      info->idxNum = 1;
      return SQLITE_OK;
    }
  }

  return SQLITE_OK;
}
std::unique_ptr<VirtualTableCursor> RelationsTable::Open() {
  return std::make_unique<RelationsCursor>(*m_stub, m_kind);
}
int RelationsTable::FindFunction(int nArg, const std::string& name, void (**pxFunc)(sqlite3_context*,int,sqlite3_value**), void **ppArg) { return 0; }
int RelationsTable::Disconnect() {
  return SQLITE_OK;
}
int RelationsTable::Destroy() {
  return SQLITE_OK;
}