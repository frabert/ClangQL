#include "RefsTable.hpp"
#include "IResultStream.hpp"
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT3
#include "VirtualTableCursor.hpp"

#include <string>
#include <vector>

using namespace clang::clangd::remote;
using clang::clangd::remote::v1::SymbolIndex;

enum RefKind {
  Kind_Unknown = 0,
  Kind_Declaration = 1 << 0,
  Kind_Definition = 1 << 1,
  Kind_Reference = 1 << 2,
  Kind_Spelled = 1 << 3,
  Kind_All = Kind_Declaration | Kind_Definition | Kind_Reference | Kind_Spelled
};

class RefStream final : public IResultStream<Ref> {
  grpc::ClientContext m_ctx;
  std::unique_ptr<grpc::ClientReader<RefsReply>> m_replyReader;
  RefsRequest m_req;
  RefsReply m_reply;

public:
  RefStream(SymbolIndex::Stub &stub, RefsRequest &req)
    : m_replyReader(stub.Refs(&m_ctx, req)), m_req(req) {}

  const Ref &Current() override { return m_reply.stream_result(); }

  bool Next() override {
    return m_replyReader->Read(&m_reply) && m_reply.has_stream_result();
  }

  const std::string &Id() { return m_req.ids(0); }
};

enum {
  CONSTR_ID = 1,
  CONSTR_DEF = 2,
  CONSTR_DECL = 4,
  CONSTR_REF = 8,
  CONSTR_SPE = 16
};

class RefsCursor final : public VirtualTableCursor {
  SymbolIndex::Stub &m_stub;
  bool m_eof = false;
  std::unique_ptr<RefStream> m_stream = nullptr;

public:
  RefsCursor(SymbolIndex::Stub &stub) : m_stub(stub) {}

  int Eof() override { return m_eof; }
  int Next() override {
    m_eof = !m_stream->Next();
    return SQLITE_OK;
  }
  sqlite3_int64 RowId() override { return 0; }
  int Column(sqlite3_context *ctx, int idxCol) override {
    auto &Current = m_stream->Current();
    switch (idxCol) {
#define SET_RES2(field1, field2)                                               \
  do {                                                                         \
    if (Current.has_##field1() && Current.field1().has_##field2()) {           \
      sqlite3_result_text(ctx, Current.field1().field2().c_str(), -1,          \
                          SQLITE_TRANSIENT);                                   \
    } else {                                                                   \
      sqlite3_result_null(ctx);                                                \
    }                                                                          \
  } while (0)
#define SET_RES3(field1, field2, field3)                                       \
  do {                                                                         \
    if (Current.has_##field1() && Current.field1().has_##field2() &&           \
        Current.field1().field2().has_##field3()) {                            \
      sqlite3_result_int(ctx, Current.field1().field2().field3());             \
    } else {                                                                   \
      sqlite3_result_null(ctx);                                                \
    }                                                                          \
  } while (0)

    case 0:
      sqlite3_result_text(ctx, m_stream->Id().c_str(), -1, SQLITE_TRANSIENT);
      break;
    case 1:
      sqlite3_result_int(ctx, (Current.kind() & Kind_Declaration) ==
                               Kind_Declaration);
      break;
    case 2:
      sqlite3_result_int(ctx,
                         (Current.kind() & Kind_Definition) == Kind_Definition);
      break;
    case 3:
      sqlite3_result_int(ctx,
                         (Current.kind() & Kind_Reference) == Kind_Reference);
      break;
    case 4:
      sqlite3_result_int(ctx, (Current.kind() & Kind_Spelled) == Kind_Spelled);
      break;
    case 5:
      SET_RES2(location, file_path);
      break;
    case 6:
      SET_RES3(location, start, line);
      break;
    case 7:
      SET_RES3(location, start, column);
      break;
    case 8:
      SET_RES3(location, end, line);
      break;
    case 9:
      SET_RES3(location, end, column);
      break;
#undef SET_RES2
#undef SET_RES3
    }
    return SQLITE_OK;
  }
  int Filter(int idxNum, const char *idxStr, int argc,
             sqlite3_value **argv) override {
    if (idxNum) {
      RefsRequest req;
      int argvIndex = 0;
      int kind = Kind_All;

      if (idxNum & CONSTR_ID) {
        req.add_ids((const char *)sqlite3_value_text(argv[argvIndex++]));
      }

      if (idxNum & CONSTR_DEF) {
        if (!sqlite3_value_int(argv[argvIndex++])) {
          kind &= !Kind_Definition;
        }
      }

      if (idxNum & CONSTR_DECL) {
        if (!sqlite3_value_int(argv[argvIndex++])) {
          kind &= !Kind_Declaration;
        }
      }

      if (idxNum & CONSTR_REF) {
        if (!sqlite3_value_int(argv[argvIndex++])) {
          kind &= !Kind_Reference;
        }
      }

      if (idxNum & CONSTR_SPE) {
        if (!sqlite3_value_int(argv[argvIndex++])) {
          kind &= !Kind_Spelled;
        }
      }

      req.set_filter(kind);
      m_stream = std::make_unique<RefStream>(m_stub, req);
      return Next();
    } else {
      m_eof = true;
      return SQLITE_OK;
    }
  }
};

constexpr const char *schema = R"cpp(
  CREATE TABLE vtable(SymbolId TEXT, Declaration INT,
    Definition INT, Reference INT, Spelled INT,
    Path TEXT, StartLine INT, StartCol INT,
    EndLine INT, EndCol INT,
    PRIMARY KEY (
      SymbolId, Declaration, Definition, Reference, Spelled,
      Path, StartLine, StartCol, EndLine, EndCol))
  WITHOUT ROWID)cpp";

RefsTable::RefsTable(sqlite3 *db, std::unique_ptr<SymbolIndex::Stub> stub)
  : m_stub(std::move(stub)) {
  int err = sqlite3_declare_vtab(db, schema);
  if (err != SQLITE_OK) {
    auto errmsg = sqlite3_errmsg(db);
    throw std::runtime_error(errmsg);
  }
}

int RefsTable::BestIndex(sqlite3_index_info *info) {
  int argvIndex = 0;

  // Check for id
  for (int i = 0; i < info->nConstraint; i++) {
    auto &constraint = info->aConstraint[i];
    if (!constraint.usable || constraint.op != SQLITE_INDEX_CONSTRAINT_EQ)
      continue;
    if (constraint.iColumn == 0) {
      info->aConstraintUsage[i].argvIndex = ++argvIndex;
      info->idxNum |= CONSTR_ID;
      info->estimatedCost = 1;
      break;
    }
  }

  // Check for def
  for (int i = 0; i < info->nConstraint; i++) {
    auto &constraint = info->aConstraint[i];
    if (!constraint.usable || constraint.op != SQLITE_INDEX_CONSTRAINT_EQ)
      continue;
    if (constraint.iColumn == 1) {
      info->aConstraintUsage[i].argvIndex = ++argvIndex;
      info->idxNum |= CONSTR_DEF;
      break;
    }
  }

  // Check for decl
  for (int i = 0; i < info->nConstraint; i++) {
    auto &constraint = info->aConstraint[i];
    if (!constraint.usable || constraint.op != SQLITE_INDEX_CONSTRAINT_EQ)
      continue;
    if (constraint.iColumn == 2) {
      info->aConstraintUsage[i].argvIndex = ++argvIndex;
      info->idxNum |= CONSTR_DEF;
      break;
    }
  }

  // Check for ref
  for (int i = 0; i < info->nConstraint; i++) {
    auto &constraint = info->aConstraint[i];
    if (!constraint.usable || constraint.op != SQLITE_INDEX_CONSTRAINT_EQ)
      continue;
    if (constraint.iColumn == 3) {
      info->aConstraintUsage[i].argvIndex = ++argvIndex;
      info->idxNum |= CONSTR_REF;
      break;
    }
  }

  // Check for spelled
  for (int i = 0; i < info->nConstraint; i++) {
    auto &constraint = info->aConstraint[i];
    if (!constraint.usable || constraint.op != SQLITE_INDEX_CONSTRAINT_EQ)
      continue;
    if (constraint.iColumn == 4) {
      info->aConstraintUsage[i].argvIndex = ++argvIndex;
      info->idxNum |= CONSTR_SPE;
      break;
    }
  }

  return SQLITE_OK;
}

std::unique_ptr<VirtualTableCursor> RefsTable::Open() {
  return std::make_unique<RefsCursor>(*m_stub);
}
