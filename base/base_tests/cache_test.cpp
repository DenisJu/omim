#include "testing/testing.hpp"
#include "base/cache.hpp"
#include "base/macros.hpp"
#include "base/stl_add.hpp"

#include "std/bind.hpp"

namespace
{
// This functor will be passed in Cache::ForEachValue by reference
class SimpleFunctor
{
public:
  SimpleFunctor() {}

  void operator() (char c)
  {
    m_v.push_back(c);
  }

  std::vector<char> m_v;

private:
  DISALLOW_COPY(SimpleFunctor);
};

// This functor will be passed in Cache::ForEachValue by move ctor
class SimpleMovableFunctor
{
public:
  SimpleMovableFunctor(std::vector<char> * v) : m_v(v) {}

  // movable
  SimpleMovableFunctor(SimpleMovableFunctor && other)
  {
      m_v = other.m_v;
      other.m_v = nullptr;
  }

  void operator() (char c)
  {
    m_v->push_back(c);
  }

private:
  std::vector<char> * m_v;

  DISALLOW_COPY(SimpleMovableFunctor);
};

double constexpr kEpsilon = 1e-6;
}  // namespace

UNIT_TEST(CacheSmoke)
{
  char const s     [] = "1123212434445";
  char const isNew [] = "1011???1??001";
  size_t const n = ARRAY_SIZE(s) - 1;
  for (int logCacheSize = 2; logCacheSize < 6; ++logCacheSize)
  {
    my::Cache<uint32_t, char> cache(logCacheSize);
    for (size_t i = 0; i < n; ++i)
    {
      bool found = false;
      char & c = cache.Find(s[i], found);
      if (isNew[i] != '?')
      {
        TEST_EQUAL(found, isNew[i] == '0', (i, s[i]));
      }
      if (found)
      {
        TEST_EQUAL(c, s[i], (i));
      }
      else
      {
        c = s[i];
      }
    }
  }
}

UNIT_TEST(CacheSmoke_0)
{
  my::Cache<uint32_t, char> cache(3); // it contains 2^3=8 elements
  bool found = true;
  cache.Find(0, found);
  TEST(!found, ());
  std::vector<char> v;
  cache.ForEachValue(MakeBackInsertFunctor(v));
  TEST_EQUAL(v, std::vector<char>(8, 0), ());
}

UNIT_TEST(CacheSmoke_1)
{
  my::Cache<uint32_t, char> cache(3); // it contains 2^3=8 elements
  SimpleFunctor f;
  cache.ForEachValue(f); // f passed by reference
  TEST_EQUAL(f.m_v, std::vector<char>(8, 0), ());
}

UNIT_TEST(CacheSmoke_2)
{
  my::Cache<uint32_t, char> cache(3); // it contains 2^3=8 elements
  SimpleFunctor f;
  cache.ForEachValue(ref(f)); // f passed by reference
  TEST_EQUAL(f.m_v, std::vector<char>(8, 0), ());
}

UNIT_TEST(CacheSmoke_3)
{
  my::CacheWithStat<uint32_t, char> cache(3); // it contains 2^3=8 elements

  // 0 access, cache miss is 0
  TEST(my::AlmostEqualAbs(0.0, cache.GetCacheMiss(), kEpsilon), ());

  bool found = true;
  cache.Find(1, found);
  TEST(!found, ());
  // 1 access, 1 miss, cache miss = 1/1 = 1
  TEST(my::AlmostEqualAbs(1.0, cache.GetCacheMiss(), kEpsilon), ());

  found = false;
  cache.Find(1, found);
  TEST(found, ());
  // 2 access, 1 miss, cache miss = 1/2 = 0.5
  TEST(my::AlmostEqualAbs(0.5, cache.GetCacheMiss(), kEpsilon), ());

  found = false;
  cache.Find(2, found);
  TEST(!found, ());
  // 3 access, 2 miss, cache miss = 2/3 = 0.6(6)
  TEST(my::AlmostEqualAbs(2.0/3.0, cache.GetCacheMiss(), kEpsilon), ());

  cache.Reset();

  // 0 access, cache miss is 0
  TEST(my::AlmostEqualAbs(0.0, cache.GetCacheMiss(), kEpsilon), ());
}

UNIT_TEST(CacheSmoke_4)
{
  my::CacheWithStat<uint32_t, char> cache(3); // it contains 2^3=8 elements
  SimpleFunctor f;
  cache.ForEachValue(f); // f passed by reference
  TEST_EQUAL(f.m_v, std::vector<char>(8, 0), ());
}

UNIT_TEST(CacheSmoke_5)
{
  my::CacheWithStat<uint32_t, char> cache(3); // it contains 2^3=8 elements
  SimpleFunctor f;
  cache.ForEachValue(ref(f)); // f passed by reference
  TEST_EQUAL(f.m_v, std::vector<char>(8, 0), ());
}

UNIT_TEST(CacheSmoke_6)
{
  my::CacheWithStat<uint32_t, char> cache(3); // it contains 2^3=8 elements
  std::vector<char> v;
  cache.ForEachValue(SimpleMovableFunctor(&v));
  TEST_EQUAL(v, std::vector<char>(8, 0), ());
}

UNIT_TEST(Cache_Init)
{
  my::Cache<uint32_t, char> cache;
  cache.Init(3 /* logCacheSize */);

  bool found = true;
  cache.Find(5, found) = 'a';
  TEST(!found, ());

  TEST_EQUAL(cache.Find(5, found), 'a', ());
  TEST(found, ());

  cache.Init(1 /* logCacheSize */);
  cache.Find(5, found) = 'b';
  TEST(!found, ());

  TEST_EQUAL(cache.Find(5, found), 'b', ());
  TEST(found, ());
}
