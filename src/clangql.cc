#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#include "ClangQLModule.hpp"

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif

EXPORT int sqlite3_clangql_init(sqlite3 *db, char **pzErrMsg,
                                const sqlite3_api_routines *pApi) {
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);

  auto mod = new ClangQLModule();

  return mod->Register(db, "clangql");
}
