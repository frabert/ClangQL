#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#include "ClangQLModule.hpp"

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif

static const char *symbol_kinds[] = {"Unknown",
                                     "Module",
                                     "Namespace",
                                     "NamespaceAlias",
                                     "Macro",
                                     "Enum",
                                     "Struct",
                                     "Class",
                                     "Protocol",
                                     "Extension",
                                     "Union",
                                     "TypeAlias",
                                     "Function",
                                     "Variable",
                                     "Field",
                                     "EnumConstant",
                                     "InstanceMethod",
                                     "ClassMethod",
                                     "StaticMethod",
                                     "InstanceProperty",
                                     "ClassProperty",
                                     "StaticProperty",
                                     "Constructor",
                                     "Destructor",
                                     "ConversionFunction",
                                     "Parameter",
                                     "Using",
                                     "TemplateTypeParm",
                                     "TemplateTemplateParm",
                                     "NonTypeTemplateParm"};

static void symbol_kind(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
  int idx = sqlite3_value_int(argv[0]);
  int num_kinds = sizeof(symbol_kinds) / sizeof(*symbol_kinds);
  if (idx < 0 || idx >= num_kinds) {
    sqlite3_result_error(ctx, "Invalid symbol kind", -1);
  } else {
    sqlite3_result_text(ctx, symbol_kinds[idx], -1, nullptr);
  }
}

static const char *symbol_subkinds[] = {
 "None",           "CXXCopyConstructor", "CXXMoveConstructor", "AccessorGetter",
 "AccessorSetter", "UsingTypename",      "UsingValue"};

static void symbol_subkind(sqlite3_context *ctx, int argc,
                           sqlite3_value **argv) {
  int idx = sqlite3_value_int(argv[0]);
  int num_kinds = sizeof(symbol_subkinds) / sizeof(*symbol_subkinds);
  if (idx < 0 || idx >= num_kinds) {
    sqlite3_result_error(ctx, "Invalid symbol subkind", -1);
  } else {
    sqlite3_result_text(ctx, symbol_subkinds[idx], -1, nullptr);
  }
}

static const char *symbol_languages[] = {"C", "ObjC", "CXX", "Swift"};

static void symbol_language(sqlite3_context *ctx, int argc,
                            sqlite3_value **argv) {
  int idx = sqlite3_value_int(argv[0]);
  int num_kinds = sizeof(symbol_languages) / sizeof(*symbol_languages);
  if (idx < 0 || idx >= num_kinds) {
    sqlite3_result_error(ctx, "Invalid symbol language", -1);
  } else {
    sqlite3_result_text(ctx, symbol_languages[idx], -1, nullptr);
  }
}

#define CHECK_ERR(e)                                                           \
  do {                                                                         \
    if ((rc = (e)) != SQLITE_OK)                                               \
      return rc;                                                               \
  } while (0)

EXPORT int sqlite3_clangql_init(sqlite3 *db, char **pzErrMsg,
                                const sqlite3_api_routines *pApi) {
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);

  CHECK_ERR(sqlite3_create_function(db, "symbol_kind", 1, SQLITE_UTF8, nullptr,
                                    symbol_kind, nullptr, nullptr));

  CHECK_ERR(sqlite3_create_function(db, "symbol_subkind", 1, SQLITE_UTF8,
                                    nullptr, symbol_subkind, nullptr, nullptr));

  CHECK_ERR(sqlite3_create_function(db, "symbol_language", 1, SQLITE_UTF8,
                                    nullptr, symbol_language, nullptr,
                                    nullptr));

  auto mod = new ClangQLModule();

  return mod->Register(db, "clangql");
}
