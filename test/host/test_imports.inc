// What an add-on module may import (platform/AddonImports.h) against the allow-list the documentation names
// (docs/ADDONS.md, "Add-ons as modules"). Included by test_main.cpp after CHECK; called from main().
// The loader itself is board-only, but its export table is built from the same list, so a name that is
// documented and missing here (or the reverse) is a module that fails to load for a reason nobody can see.

static std::vector<std::string> exportedImportNames() {
  std::vector<std::string> names;
#define BBW_TEST_IMPORT_NAME(name, address) names.push_back(#name);
  BBW_ADDON_IMPORTS(BBW_TEST_IMPORT_NAME)
#undef BBW_TEST_IMPORT_NAME
  return names;
}

// The backticked names of the sentence that starts "A module may call only this allow-list".
static std::vector<std::string> documentedImportNames() {
  std::vector<std::string> names;
  FILE *f = fopen("docs/ADDONS.md", "rb");
  if (!f) return names;
  std::string text;
  char buffer[4096];
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) text.append(buffer, n);
  fclose(f);
  const size_t from = text.find("A module may call only this allow-list");
  const size_t to = text.find("Anything else makes the", from);
  if (from == std::string::npos || to == std::string::npos) return names;
  for (size_t i = from; i < to;) {
    const size_t open = text.find('`', i);
    if (open == std::string::npos || open >= to) break;
    const size_t close = text.find('`', open + 1);
    if (close == std::string::npos || close > to) break;
    names.push_back(text.substr(open + 1, close - open - 1));
    i = close + 1;
  }
  return names;
}

static bool hasName(const std::vector<std::string> &list, const std::string &name) {
  for (const std::string &s : list) {
    if (s == name) return true;
  }
  return false;
}

static void testModuleImportList() {
  printf("module imports: every documented name is exported, and the reverse\n");
  const std::vector<std::string> exported = exportedImportNames();
  const std::vector<std::string> documented = documentedImportNames();
  CHECK(!documented.empty());
  CHECK(!exported.empty());
  for (const std::string &name : documented) {
    const bool found = hasName(exported, name);
    if (!found) printf("  documented but not exported: %s\n", name.c_str());
    CHECK(found);
  }
  for (const std::string &name : exported) {
    const bool found = hasName(documented, name);
    if (!found) printf("  exported but not documented: %s\n", name.c_str());
    CHECK(found);
  }
  for (size_t i = 0; i < exported.size(); ++i) {  // no name twice: the first would hide the second
    for (size_t j = i + 1; j < exported.size(); ++j) CHECK(exported[i] != exported[j]);
  }
  // The names added after the first allow-list (a module that parses numbers and text fields needs them).
  const char *const added[] = {"atoi", "atol", "fminf", "memchr", "strncasecmp", "strtok"};
  for (const char *name : added) CHECK(hasName(exported, name));
}
