#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT3

#include "Module.hpp"
#include "VirtualTable.hpp"
#include "VirtualTableCursor.hpp"

struct module_vtab {
  sqlite3_vtab base;
  std::unique_ptr<VirtualTable> tab;
};

struct module_vtab_cur {
  sqlite3_vtab_cursor base;
  std::unique_ptr<VirtualTableCursor> cur;
};

static int module_destroy(sqlite3_vtab *pVtab) {
  auto vtab = (module_vtab*)pVtab;
  vtab->tab->Destroy();
  delete vtab;
  return SQLITE_OK;
}

static int module_disconnect(sqlite3_vtab *pVtab) {
  auto vtab = (module_vtab*)pVtab;
  vtab->tab->Disconnect();
  delete vtab;
  return SQLITE_OK;
}

static int module_create(
  sqlite3 *db, void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVTab,
  char **pzErr
){
  auto mod = (Module*)pAux;
  auto vtab = new module_vtab();
  if(vtab == nullptr) {
    return SQLITE_NOMEM;
  }

  vtab->tab = mod->Create(db, argc, argv);

  *ppVTab = &(vtab->base);
  return SQLITE_OK;
}

static int module_connect(
  sqlite3 *db, void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVTab,
  char **pzErr
){
  auto mod = (Module*)pAux;
  auto vtab = new module_vtab();
  if(vtab == nullptr) {
    return SQLITE_NOMEM;
  }

  vtab->tab = mod->Connect(db, argc, argv);

  *ppVTab = &(vtab->base);
  return SQLITE_OK;
}

static int module_open(
  sqlite3_vtab *pVTab,
  sqlite3_vtab_cursor **pp_cursor
){
  auto table = (module_vtab*)pVTab;
  auto cur = new module_vtab_cur();
  if(cur == nullptr) {
    return SQLITE_NOMEM;
  } else {
    cur->cur = table->tab->Open();
    *pp_cursor = &(cur->base);
    return SQLITE_OK;
  }
}

static int module_close(
  sqlite3_vtab_cursor *pVCur
){
  auto cur = (module_vtab_cur*)pVCur;
  cur->cur->Close();
  delete cur;
  return SQLITE_OK;
}

static int module_eof(
  sqlite3_vtab_cursor *base
){
  auto cur = (module_vtab_cur*)base;
  return cur->cur->Eof();
}

static int module_next(
  sqlite3_vtab_cursor *base
){
  auto cur = (module_vtab_cur*)base;
  return cur->cur->Next();
}

static int module_column(
  sqlite3_vtab_cursor *base,
  sqlite3_context *ctx,
  int idxCol
){
  auto cur = (module_vtab_cur*)base;
  return cur->cur->Column(ctx, idxCol);
}

static int module_rowid(
  sqlite3_vtab_cursor *base,
  sqlite3_int64* rowid
){
  auto cur = (module_vtab_cur*)base;
  *rowid = cur->cur->RowId();
  return SQLITE_OK;
}

static int module_filter(
  sqlite3_vtab_cursor *base, 
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
) {
  auto cur = (module_vtab_cur*)base;
  return cur->cur->Filter(idxNum, idxStr, argc, argv);
}

static int module_best_index(
  sqlite3_vtab *base,
  sqlite3_index_info *pIdxInfo
){
  auto tab = (module_vtab*)base;
  return tab->tab->BestIndex(pIdxInfo);
}

static int module_find_function(
  sqlite3_vtab *base,
  int nArg,
  const char *zName,
  void (**pxFunc)(sqlite3_context*,int,sqlite3_value**),
  void **ppArg
) {
  auto tab = (module_vtab*)base;
  return tab->tab->FindFunction(nArg, zName, pxFunc, ppArg);
}

static sqlite3_module module_vtbl = {
    0,                    /* iVersion      */
    module_create,        /* xCreate       */
    module_create,        /* xConnect      */
    module_best_index,    /* xBestIndex    */
    module_disconnect,    /* xDisconnect   */
    module_destroy,       /* xDestroy      */
    module_open,          /* xOpen         */
    module_close,         /* xClose        */
    module_filter,        /* xFilter       */
    module_next,          /* xNext         */
    module_eof,           /* xEof          */
    module_column,        /* xColumn       */
    module_rowid,         /* xRowid        */
    nullptr,              /* xUpdate       */
    nullptr,              /* xBegin        */
    nullptr,              /* xSync         */
    nullptr,              /* xCommit       */
    nullptr,              /* xRollback     */
    module_find_function, /* xFindFunction */
    nullptr,              /* xRename       */
    nullptr,              /* xSavepoint    */
    nullptr,              /* xRelease      */
    nullptr               /* xRollbackto   */
};

#define CHECK_ERR(e) if((err = (e)) != SQLITE_OK) return err;

static void dummy_func(sqlite3_context* ctx,int,sqlite3_value**) {
  sqlite3_result_error(ctx, "Invalid call to function", -1);
}

int Module::Register(sqlite3* db, const char* name) {
  int err;
  CHECK_ERR(sqlite3_create_module(db, name, &module_vtbl, this));
  return SQLITE_OK;
}