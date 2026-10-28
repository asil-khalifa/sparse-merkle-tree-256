#include <string>
#include <vector>

#include "smt/hash.hpp"
#include "test_framework.hpp"

using namespace smt;

TEST(sha256_known_vectors) {
  CHECK_EQ(to_hex(sha256("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK_EQ(to_hex(sha256("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK_EQ(to_hex(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  // Exactly one block of padding boundary cases.
  CHECK_EQ(to_hex(sha256(std::string(55, 'a'))), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
  CHECK_EQ(to_hex(sha256(std::string(56, 'a'))), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
  CHECK_EQ(to_hex(sha256(std::string(64, 'a'))), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST(sha256_one_million_a) {
  const std::string m(1000000, 'a');
  CHECK_EQ(to_hex(sha256(m)), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(sha256_streaming_matches_one_shot) {
  std::string m;
  for (int i = 0; i < 1000; ++i) m += char('a' + i % 26);
  const Hash256 one_shot = sha256(m);
  Sha256 ctx;
  std::size_t pos = 0;
  const std::size_t chunks[] = {1, 7, 63, 64, 65, 100, 3, 500, 1000};
  for (std::size_t c : chunks) {
    if (pos >= m.size()) break;
    const std::size_t n = std::min(c, m.size() - pos);
    ctx.update(reinterpret_cast<const uint8_t*>(m.data()) + pos, n);
    pos += n;
  }
  if (pos < m.size()) ctx.update(reinterpret_cast<const uint8_t*>(m.data()) + pos, m.size() - pos);
  Hash256 out;
  ctx.finish(out.data());
  CHECK(out == one_shot);
}

TEST(hex_roundtrip_and_bits) {
  const Hash256 h = sha256("hello");
  Hash256 back;
  CHECK(from_hex(to_hex(h), back));
  CHECK(back == h);
  CHECK(from_hex("0x" + to_hex(h), back));
  CHECK(!from_hex("zz", back));
  CHECK(!from_hex(to_hex(h).substr(1), back));

  Hash256 k{};
  k[0] = 0x80;  // bit 0 set
  k[31] = 0x01; // bit 255 set
  CHECK(get_bit(k, 0));
  CHECK(!get_bit(k, 1));
  CHECK(get_bit(k, 255));
  CHECK(!get_bit(k, 254));
  Hash256 z{};
  set_bit(z, 5);
  CHECK(get_bit(z, 5) && z[0] == 0x04);
}

TEST(common_prefix_len_cases) {
  Hash256 a{}, b{};
  CHECK_EQ(common_prefix_len(a, b), 256u);
  b[31] = 0x01;
  CHECK_EQ(common_prefix_len(a, b), 255u);
  b = Hash256{};
  b[0] = 0x80;
  CHECK_EQ(common_prefix_len(a, b), 0u);
  b[0] = 0x01;
  CHECK_EQ(common_prefix_len(a, b), 7u);
  b = Hash256{};
  b[3] = 0x10;
  CHECK_EQ(common_prefix_len(a, b), 27u);
}

TEST(domain_separation_leaf_vs_node) {
  const Hash256 x = sha256("x"), y = sha256("y");
  CHECK(hash_leaf(x, y) != hash_node(x, y));
  CHECK(hash_node(x, y) != hash_node(y, x));
  CHECK(!is_placeholder(hash_node(placeholder(), placeholder())));
}
