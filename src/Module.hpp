#ifndef MODULE_HPP
#define MODULE_HPP
#include "sqlite3ext.h"
#include <memory>

class VirtualTable;

class Module {
public:
  virtual std::unique_ptr<VirtualTable> Create(sqlite3 *db, int argc,
                                               const char *const *argv) = 0;
  virtual ~Module() = default;

  int Register(sqlite3 *db, const char *name);
};

#endif