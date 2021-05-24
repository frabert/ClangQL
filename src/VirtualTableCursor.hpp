#ifndef VIRTUALTABLEMONITOR_HPP
#define VIRTUALTABLEMONITOR_HPP
#include "sqlite3ext.h"

class VirtualTableCursor {
public:
  virtual ~VirtualTableCursor() = default;

  virtual int Close() = 0;
  virtual int Filter(int idxNum, const char *idxStr, int argc,
                     sqlite3_value **argv) = 0;
  virtual int Next() = 0;
  virtual int Eof() = 0;
  virtual int Column(sqlite3_context *ctx, int idxCol) = 0;
  virtual sqlite3_int64 RowId() = 0;
};

#endif