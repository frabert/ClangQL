#include "VirtualTable.hpp"

int VirtualTable::FindFunction(int nArg, const std::string &name,
                               void (**pxFunc)(sqlite3_context *, int,
                                               sqlite3_value **),
                               void **ppArg) {
  return 0;
}