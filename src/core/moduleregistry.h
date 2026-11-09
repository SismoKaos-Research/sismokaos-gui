// Ordered list of the registered modules; the main window builds one tab
// per entry in this order.
#pragma once

#include <memory>
#include <vector>

#include "core/module.h"

namespace skg {

class ModuleRegistry {
public:
  Module *add(std::unique_ptr<Module> m) {
    modules_.push_back(std::move(m));
    return modules_.back().get();
  }

  template <class T, class... Args> T *emplace(Args &&...args) {
    auto m = std::make_unique<T>(std::forward<Args>(args)...);
    T *raw = m.get();
    modules_.push_back(std::move(m));
    return raw;
  }

  const std::vector<std::unique_ptr<Module>> &modules() const {
    return modules_;
  }

  Module *find(const QString &id) const {
    for (const auto &m : modules_)
      if (m->id() == id)
        return m.get();
    return nullptr;
  }

private:
  std::vector<std::unique_ptr<Module>> modules_;
};

} // namespace skg
