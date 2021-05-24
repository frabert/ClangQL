#ifndef CLANGQLMODULE_HPP
#define CLANGQLMODULE_HPP
#include "Module.hpp"

class ClangQLModule : public Module {
public:
  virtual std::unique_ptr<VirtualTable>
  Create(sqlite3 *db, int argc, const char *const *argv) override;
};

#endif