#include "SymbolsTable.hpp"
#include "IResultStream.hpp"
SQLITE_EXTENSION_INIT3
#include "VirtualTableCursor.hpp"

#include <string>
#include <vector>

using namespace clang::clangd::remote;
using clang::clangd::remote::v1::SymbolIndex;

class FuzzyFindStream final : public IResultStream<Symbol> {
  grpc::ClientContext m_ctx;
  std::unique_ptr<grpc::ClientReader<FuzzyFindReply>> m_replyReader;
  FuzzyFindReply m_reply;

public:
  FuzzyFindStream(SymbolIndex::Stub &stub, FuzzyFindRequest &req)
    : m_replyReader(stub.FuzzyFind(&m_ctx, req)) {}

  virtual const Symbol &Current() override { return m_reply.stream_result(); }

  virtual bool Next() override {
    return m_replyReader->Read(&m_reply) && m_reply.has_stream_result();
  }
};

class LookupStream final : public IResultStream<Symbol> {
  grpc::ClientContext m_ctx;
  std::unique_ptr<grpc::ClientReader<LookupReply>> m_replyReader;
  LookupReply m_reply;

public:
  LookupStream(SymbolIndex::Stub &stub, LookupRequest &req)
    : m_replyReader(stub.Lookup(&m_ctx, req)) {}

  virtual const Symbol &Current() override { return m_reply.stream_result(); }

  virtual bool Next() override {
    return m_replyReader->Read(&m_reply) && m_reply.has_stream_result();
  }
};

enum {
  // No constraints
  SEARCH_NONE = 0,
  // Constraint value for `query` in FuzzyFindRequest
  SEARCH_FUZZYNAME = 1,
  // Constraint value for `scope` in FuzzyFindRequest
  SEARCH_SCOPE = 2,
  // Constraint value for `id` in LookupRequest
  SEARCH_ID = 4,
  // Constraint value for `proximity_paths` in FuzzyFindRequest
  SEARCH_PATH = 8,
  // The scope constraint is EQ and not LIKE
  SEARCH_SCOPE_EXACT = 16
};

struct SymbolProperty {
  enum {
    Generic = 1 << 0,
    TemplatePartialSpecialization = 1 << 1,
    TemplateSpecialization = 1 << 2,
    UnitTest = 1 << 3,
    IBAnnotated = 1 << 4,
    IBOutletCollection = 1 << 5,
    GKInspectable = 1 << 6,
    Local = 1 << 7,
    ProtocolInterface = 1 << 8,
  };
};

class SymbolsCursor final : public VirtualTableCursor {
  SymbolIndex::Stub &m_stub;
  bool m_eof = false;
  std::unique_ptr<IResultStream<Symbol>> m_stream = nullptr;

public:
  SymbolsCursor(SymbolIndex::Stub &stub) : m_stub(stub) {}

  int Close() override { return SQLITE_OK; }
  int Filter(int idxNum, const char *idxStr, int argc,
             sqlite3_value **argv) override {
    if (idxNum == SEARCH_ID) {
      LookupRequest req;
      req.add_ids((const char *)sqlite3_value_text(argv[0]));
      m_stream = std::make_unique<LookupStream>(m_stub, req);
    } else {
      FuzzyFindRequest req;
      req.set_any_scope(true);
      int argIndex = 0;

      bool has_name = idxNum & SEARCH_FUZZYNAME;
      bool has_scope = idxNum & SEARCH_SCOPE;
      bool has_path = idxNum & SEARCH_PATH;
      bool has_exact_scope = idxNum & SEARCH_SCOPE_EXACT;

      if (has_name) {
        req.set_query((const char *)sqlite3_value_text(argv[argIndex++]));
      }

      if (has_scope) {
        req.add_scopes((const char *)sqlite3_value_text(argv[argIndex++]));
      }

      if (has_path) {
        req.add_proximity_paths(
         (const char *)sqlite3_value_text(argv[argIndex++]));
      }

      if (has_exact_scope) {
        req.set_any_scope(false);
      }

      m_stream = std::make_unique<FuzzyFindStream>(m_stub, req);
    }
    return Next();
  }
  int Next() override {
    m_eof = !m_stream->Next();
    return SQLITE_OK;
  }
  int Eof() override { return m_eof; }
  int Column(sqlite3_context *ctx, int idxCol) override {
    auto &Current = m_stream->Current();
    switch (idxCol) {
#define SET_RES(field)                                                         \
  do {                                                                         \
    if (Current.has_##field()) {                                               \
      sqlite3_result_text(ctx, Current.field().c_str(), -1, SQLITE_TRANSIENT); \
    } else {                                                                   \
      sqlite3_result_null(ctx);                                                \
    }                                                                          \
  } while (0)
#define SET_RES2(field1, field2)                                               \
  do {                                                                         \
    if (Current.has_##field1() && Current.field1().has_##field2()) {           \
      sqlite3_result_text(ctx, Current.field1().field2().c_str(), -1,          \
                          SQLITE_TRANSIENT);                                   \
    } else {                                                                   \
      sqlite3_result_null(ctx);                                                \
    }                                                                          \
  } while (0)
#define SET_RES2_INT(field1, field2)                                           \
  do {                                                                         \
    if (Current.has_##field1() && Current.field1().has_##field2()) {           \
      sqlite3_result_int(ctx, Current.field1().field2());                      \
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
#define SET_RES_PROP(name)                                                     \
  do {                                                                         \
    if (Current.has_info() && Current.info().has_properties()) {               \
      sqlite3_result_int(ctx, (Current.info().properties() &                   \
                               SymbolProperty::name) == SymbolProperty::name); \
    } else {                                                                   \
      sqlite3_result_null(ctx);                                                \
    }                                                                          \
  } while (0)
    case 0:
      SET_RES(id);
      break;
    case 1:
      SET_RES(name);
      break;
    case 2:
      SET_RES(scope);
      break;
    case 3:
      SET_RES(signature);
      break;
    case 4:
      SET_RES(documentation);
      break;
    case 5:
      SET_RES(return_type);
      break;
    case 6:
      SET_RES(type);
      break;
    case 7:
      SET_RES2(definition, file_path);
      break;
    case 8:
      SET_RES3(definition, start, line);
      break;
    case 9:
      SET_RES3(definition, start, column);
      break;
    case 10:
      SET_RES3(definition, end, line);
      break;
    case 11:
      SET_RES3(definition, end, column);
      break;
    case 12:
      SET_RES2(canonical_declaration, file_path);
      break;
    case 13:
      SET_RES3(canonical_declaration, start, line);
      break;
    case 14:
      SET_RES3(canonical_declaration, start, column);
      break;
    case 15:
      SET_RES3(canonical_declaration, end, line);
      break;
    case 16:
      SET_RES3(canonical_declaration, end, column);
      break;
    case 17:
      SET_RES2_INT(info, kind);
      break;
    case 18:
      SET_RES2_INT(info, subkind);
      break;
    case 19:
      SET_RES2_INT(info, language);
      break;
    case 20:
      SET_RES_PROP(Generic);
      break;
    case 21:
      SET_RES_PROP(TemplatePartialSpecialization);
      break;
    case 22:
      SET_RES_PROP(TemplateSpecialization);
      break;
    case 23:
      SET_RES_PROP(TemplateSpecialization);
      break;
    case 24:
      SET_RES_PROP(UnitTest);
      break;
    case 25:
      SET_RES_PROP(IBAnnotated);
      break;
    case 26:
      SET_RES_PROP(IBOutletCollection);
      break;
    case 27:
      SET_RES_PROP(GKInspectable);
      break;
    case 28:
      SET_RES_PROP(Local);
      break;
    case 29:
      SET_RES_PROP(ProtocolInterface);
      break;
#undef SET_RES
#undef SET_RES2
#undef SET_RES3
    }
    return SQLITE_OK;
  }
  sqlite3_int64 RowId() override {
    auto res = strtoll(m_stream->Current().id().c_str(), nullptr, 16);

    errno = 0;
    if (errno) {
      throw std::exception();
    }

    return res;
  }
};

constexpr const char *schema = R"cpp(
  CREATE TABLE vtable(Id TEXT, Name TEXT, Scope TEXT,
    Signature TEXT, Documentation TEXT, ReturnType TEXT,
    Type TEXT, DefPath TEXT, DefStartLine INT, DefStartCol INT,
    DefEndLine INT, DefEndCol INT, DeclPath TEXT,
    DeclStartLine INT, DeclStartCol INT, DeclEndLine INT, DeclEndCol INT,
    Kind INT, SubKind INT, Language INT,
    Generic INT, TemplatePartialSpecialization INT, TemplateSpecialization INT,
    UnitTest INT, IBAnnotated INT, IBOutletCollection INT, GKInspectable INT,
    Local INT, ProtocolInterface INT)
  )cpp";

SymbolsTable::SymbolsTable(sqlite3 *db, std::unique_ptr<SymbolIndex::Stub> stub)
  : m_stub(std::move(stub)) {
  int err = sqlite3_declare_vtab(db, schema);
  if (err != SQLITE_OK)
    throw std::exception();
}

int SymbolsTable::BestIndex(sqlite3_index_info *info) {
  // First check if we have a search by id
  for (int i = 0; i < info->nConstraint; i++) {
    auto constraint = info->aConstraint[i];
    if (!constraint.usable)
      continue;
    if (constraint.iColumn == 0 &&
        constraint.op == SQLITE_INDEX_CONSTRAINT_EQ) {
      info->aConstraintUsage[i].argvIndex = 1;
      info->aConstraintUsage[i].omit = 1;
      info->idxNum = SEARCH_ID;
      info->estimatedCost = 1;
      return SQLITE_OK;
    }
  }

  int argvIndex = 0;

  // Look for a search by name
  for (int i = 0; i < info->nConstraint; i++) {
    auto constraint = info->aConstraint[i];
    if (!constraint.usable)
      continue;
    if (constraint.iColumn == 1 &&
        (constraint.op == SQLITE_INDEX_CONSTRAINT_EQ ||
         constraint.op == SQLITE_INDEX_CONSTRAINT_LIKE)) {
      info->aConstraintUsage[i].argvIndex = ++argvIndex;
      info->aConstraintUsage[i].omit =
       constraint.op == SQLITE_INDEX_CONSTRAINT_LIKE;
      info->idxNum |= SEARCH_FUZZYNAME;
      info->estimatedCost = 1;
      break;
    }
  }

  // Look for a search by scope
  for (int i = 0; i < info->nConstraint; i++) {
    auto constraint = info->aConstraint[i];
    if (!constraint.usable)
      continue;
    if (constraint.iColumn == 2 &&
        (constraint.op == SQLITE_INDEX_CONSTRAINT_EQ ||
         constraint.op == SQLITE_INDEX_CONSTRAINT_LIKE)) {
      info->aConstraintUsage[i].argvIndex = ++argvIndex;
      info->aConstraintUsage[i].omit =
       constraint.op == SQLITE_INDEX_CONSTRAINT_LIKE;
      info->idxNum |= SEARCH_SCOPE;
      if (constraint.op == SQLITE_INDEX_CONSTRAINT_EQ) {
        info->idxNum |= SEARCH_SCOPE_EXACT;
      }
      info->estimatedCost = 1;
      break;
    }
  }

  // Look for a search by path
  for (int i = 0; i < info->nConstraint; i++) {
    auto constraint = info->aConstraint[i];
    if (!constraint.usable)
      continue;
    if ((constraint.iColumn == 7 || constraint.iColumn == 12) &&
        constraint.op == SQLITE_INDEX_CONSTRAINT_LIKE) {
      info->aConstraintUsage[i].argvIndex = ++argvIndex;
      info->aConstraintUsage[i].omit = 1;
      info->idxNum |= SEARCH_PATH;
      info->estimatedCost = 1;
      break;
    }
  }

  return SQLITE_OK;
}

std::unique_ptr<VirtualTableCursor> SymbolsTable::Open() {
  return std::make_unique<SymbolsCursor>(*m_stub);
}

int SymbolsTable::Destroy() { return SQLITE_OK; }

static void dummy_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
  sqlite3_result_int(ctx, 1);
}

int SymbolsTable::FindFunction(int nArg, const std::string &name,
                               void (**pxFunc)(sqlite3_context *, int,
                                               sqlite3_value **),
                               void **ppArg) {
  if (name == "like") {
    *pxFunc = dummy_func;
    return 1;
  }
  return 0;
}