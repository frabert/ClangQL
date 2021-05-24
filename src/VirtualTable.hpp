#ifndef VIRTUALTABLE_HPP
#define VIRTUALTABLE_HPP
#include "sqlite3ext.h"
#include <functional>
#include <memory>
#include <string>

class VirtualTableCursor;

class VirtualTable {
public:
  virtual ~VirtualTable() = default;
  virtual int BestIndex(sqlite3_index_info *info) = 0;
  virtual std::unique_ptr<VirtualTableCursor> Open() = 0;
  virtual int FindFunction(int nArg, const std::string &name,
                           void (**pxFunc)(sqlite3_context *, int,
                                           sqlite3_value **),
                           void **ppArg) = 0;
  virtual int Disconnect() = 0;
  virtual int Destroy() = 0;
};

#endif