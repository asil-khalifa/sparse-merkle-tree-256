#include "smt/db_file.hpp"

#include <cstring>
#include <fstream>

namespace smt {

namespace {
constexpr char MAGIC[8] = {'S', 'M', 'T', 'D', 'B', 0, 0, 1};

void put_u8(std::string& o, uint8_t v) { o.push_back(char(v)); }
void put_u32(std::string& o, uint32_t v) {
  for (int i = 0; i < 4; ++i) o.push_back(char((v >> (8 * i)) & 0xff));
}
void put_u64(std::string& o, uint64_t v) {
  for (int i = 0; i < 8; ++i) o.push_back(char((v >> (8 * i)) & 0xff));
}
void put_hash(std::string& o, const Hash256& h) { o.append(reinterpret_cast<const char*>(h.data()), h.size()); }

struct Reader {
  const std::string& s;
  std::size_t pos = 0;
  bool ok = true;
  bool need(std::size_t n) {
    if (pos + n > s.size()) ok = false;
    return ok;
  }
  uint8_t u8() { return need(1) ? uint8_t(s[pos++]) : 0; }
  uint32_t u32() {
    if (!need(4)) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(uint8_t(s[pos++])) << (8 * i);
    return v;
  }
  uint64_t u64() {
    if (!need(8)) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(uint8_t(s[pos++])) << (8 * i);
    return v;
  }
  Hash256 hash() {
    Hash256 h{};
    if (need(HASH_SIZE)) {
      std::memcpy(h.data(), s.data() + pos, HASH_SIZE);
      pos += HASH_SIZE;
    }
    return h;
  }
};
}  // namespace

bool Database::save(const std::string& path, std::string* err) const {
  std::string o;
  o.append(MAGIC, 8);
  put_u8(o, keep_history ? 1 : 0);
  put_u64(o, roots.size());
  for (const Hash256& r : roots) put_hash(o, r);
  put_u64(o, store.size());
  store.for_each([&](const Hash256& h, const Node& n) {
    put_hash(o, h);
    put_u8(o, uint8_t(n.kind));
    put_u8(o, uint8_t((n.left_is_leaf ? 1 : 0) | (n.right_is_leaf ? 2 : 0)));
    put_hash(o, n.a);
    put_hash(o, n.b);
    if (n.kind == NodeKind::Leaf) {
      put_u32(o, uint32_t(n.value.size()));
      o.append(reinterpret_cast<const char*>(n.value.data()), n.value.size());
    }
  });
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    if (err) *err = "cannot open " + path + " for writing";
    return false;
  }
  f.write(o.data(), std::streamsize(o.size()));
  if (!f) {
    if (err) *err = "write error on " + path;
    return false;
  }
  return true;
}

bool Database::load(const std::string& path, Database& db, std::string* err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (err) *err = "cannot open " + path;
    return false;
  }
  std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (s.size() < 8 || std::memcmp(s.data(), MAGIC, 8) != 0) {
    if (err) *err = path + " is not an SMT database";
    return false;
  }
  Reader r{s, 8};
  db.keep_history = r.u8() != 0;
  db.roots.clear();
  const uint64_t n_roots = r.u64();
  for (uint64_t i = 0; i < n_roots && r.ok; ++i) db.roots.push_back(r.hash());
  db.store.clear();
  const uint64_t n_nodes = r.u64();
  db.store.reserve(std::size_t(n_nodes));
  for (uint64_t i = 0; i < n_nodes && r.ok; ++i) {
    const Hash256 h = r.hash();
    Node n;
    n.kind = NodeKind(r.u8());
    const uint8_t flags = r.u8();
    n.left_is_leaf = flags & 1;
    n.right_is_leaf = flags & 2;
    n.a = r.hash();
    n.b = r.hash();
    if (n.kind == NodeKind::Leaf) {
      const uint32_t len = r.u32();
      if (!r.need(len)) break;
      n.value.assign(s.begin() + std::ptrdiff_t(r.pos), s.begin() + std::ptrdiff_t(r.pos + len));
      r.pos += len;
    }
    if (!r.ok) break;
    db.store.put(h, std::move(n));
  }
  if (!r.ok || r.pos != s.size()) {
    if (err) *err = path + " is truncated or corrupt";
    return false;
  }
  return true;
}

}  // namespace smt
