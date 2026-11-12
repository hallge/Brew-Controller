// Loop timing and the web serving helpers (core/LoopTiming.h, core/WebCache.h), 11 Nov 2026.
#include "core/LoopTiming.h"
#include "core/WebCache.h"
#include "core/ConnPool.h"
#include "core/LibraryFetch.h"

static void testLoopTiming() {
  printf("loop timing\n");
  LoopTiming t;
  // Pass 1: 10 ms, of which ui 4 ms and web 3 ms; the rest is "other".
  loopPassBegin(t, 1000, 0);
  loopSpanAdd(t, LOOP_UI, 4000);
  loopSpanAdd(t, LOOP_WEB, 3000);
  CHECK(loopPassEnd(t, 11000, 10) == 10000);
  CHECK(t.current.passes == 1);
  CHECK(t.current.maxPassUs == 10000);
  CHECK(t.current.maxPassJob == LOOP_UI);
  CHECK(t.current.jobMaxUs[LOOP_OTHER] == 3000);
  // Pass 2: 1.2 s, nearly all of it in the add-on job.
  loopPassBegin(t, 20000, 20);
  loopSpanAdd(t, LOOP_ADDON, 1150000);
  loopSpanAdd(t, LOOP_UI, 5000);
  CHECK(loopPassEnd(t, 1220000, 1220) == 1200000);
  CHECK(t.current.maxPassUs == 1200000);
  CHECK(t.current.maxPassJob == LOOP_ADDON);
  CHECK(t.current.maxPassJobUs == 1150000);
  CHECK(t.current.jobMaxUs[LOOP_UI] == 5000);   // the slowest ui of any pass
  CHECK(t.bootMaxJob == LOOP_ADDON);
  CHECK(t.bootMaxAtS == 1);
  // Before a minute has gone by the report shows the minute under way.
  CHECK(&loopReportWindow(t) == &t.current);
  // A short pass after the minute closes the window: the report keeps the minute that was.
  loopPassBegin(t, 61000000u, 61000);
  CHECK(loopPassEnd(t, 61001000u, 61001) == 1000);
  CHECK(t.last.passes == 3);
  CHECK(t.last.maxPassJob == LOOP_ADDON);
  CHECK(t.current.passes == 0);
  CHECK(&loopReportWindow(t) == &t.last);
  CHECK(t.bootMaxPassUs == 1200000);   // since boot stays
  // The microsecond clock wraps (every 71 minutes): the pass length still comes out right.
  loopPassBegin(t, 0xFFFFF000u, 62000);
  CHECK(loopPassEnd(t, 0x00001000u, 62000) == 0x2000u);
  // An unknown job is ignored, not written past the table.
  loopSpanAdd(t, static_cast<LoopJob>(LOOP_JOB_COUNT + 3), 5);
  CHECK(strcmp(LOOP_JOB_NAMES[LOOP_OTHER], "other") == 0);
}

static void testWebCache() {
  printf("web cache tags\n");
  char tag[40];
  CHECK(shotsEtag(0x1234abcdu, 7, 20, tag, sizeof(tag)));
  CHECK(strcmp(tag, "\"s-1234abcd-7-20\"") == 0);
  CHECK(!shotsEtag(1, 1, 1, tag, 4));   // too small: no tag rather than half of one
  // If-None-Match: the exact tag, a list holding it, a weak form, or *.
  CHECK(etagMatches("\"s-1234abcd-7-20\"", "\"s-1234abcd-7-20\""));
  CHECK(etagMatches("\"x\", \"s-1234abcd-7-20\"", "\"s-1234abcd-7-20\""));
  CHECK(etagMatches("W/\"s-1234abcd-7-20\"", "\"s-1234abcd-7-20\""));
  CHECK(etagMatches("*", "\"s-1234abcd-7-20\""));
  CHECK(!etagMatches("\"s-1234abcd-7-2\"", "\"s-1234abcd-7-20\""));
  CHECK(!etagMatches("\"s-1234abcd-7-200\"", "\"s-1234abcd-7-20\""));
  CHECK(!etagMatches("", "\"s-1234abcd-7-20\""));
  CHECK(!etagMatches(nullptr, "\"a\""));
  CHECK(!etagMatches("\"a\"", ""));
  // Accept-Encoding: gzip, unless it is refused with q=0.
  CHECK(acceptsGzip("gzip, deflate, br"));
  CHECK(acceptsGzip("br;q=1.0, gzip;q=0.8"));
  CHECK(acceptsGzip("GZIP"));
  CHECK(!acceptsGzip("deflate, br"));
  CHECK(!acceptsGzip("gzip;q=0"));
  CHECK(!acceptsGzip("gzip;q=0.0, br"));
  CHECK(!acceptsGzip(""));
  CHECK(!acceptsGzip(nullptr));
  CHECK(acceptsGzip("*"));
  // The chunk buffer: text gathers until a flush; one larger than the buffer goes straight out.
  struct Sink {
    std::string out;
    int writes = 0;
  } sink;
  char buffer[16];
  ChunkBuffer cb(buffer, sizeof(buffer),
                 [](const char *data, size_t n, void *c) {
                   Sink &s = *static_cast<Sink *>(c);
                   s.out.append(data, n);
                   ++s.writes;
                 },
                 &sink);
  cb.add("[");
  cb.add("{\"id\":1}");
  CHECK(sink.writes == 0);
  cb.add(",{\"id\":22222}");   // does not fit: what was gathered goes first
  CHECK(sink.writes == 1);
  cb.add("0123456789abcdefXYZ");   // larger than the whole buffer
  cb.add("]");
  cb.flush();
  CHECK(sink.out == "[{\"id\":1},{\"id\":22222}0123456789abcdefXYZ]");
  CHECK(sink.writes == 4);
  cb.flush();
  CHECK(sink.writes == 4);   // nothing waiting: nothing sent
}

// A fake client for the connection pool.
struct FakeConn {
  int id = 0;
  int bytes = 0;
  bool up = true;
  int available() const { return bytes; }
  bool connected() const { return up; }
};

static void testConnPool() {
  printf("connection pool\n");
  using Pool = bbw::ConnPool<FakeConn, 3>;
  FakeConn out;
  {  // An idle client does not block a ready one behind it.
    Pool p;
    p.add({1, 0, true}, 0);
    p.add({2, 10, true}, 100);
    CHECK(p.takeReady(200, 5000, out) && out.id == 2);
    CHECK(p.size() == 1);   // the idle one stays
    CHECK(!p.takeReady(300, 5000, out));
    CHECK(p.size() == 1);
  }
  {  // An idle client is dropped once it has waited longer than the limit, not before.
    Pool p;
    p.add({1, 0, true}, 1000);
    CHECK(!p.takeReady(6000, 5000, out) && p.size() == 1);   // exactly 5000: kept
    CHECK(!p.takeReady(6001, 5000, out) && p.size() == 0);
    // The clock wrapping does not drop a fresh one.
    p.add({2, 0, true}, 0xFFFFFF00u);
    CHECK(!p.takeReady(0x100u, 5000, out) && p.size() == 1);
  }
  {  // A disconnected client is dropped; one that sent its request and closed is still served.
    Pool p;
    p.add({1, 0, false}, 0);
    p.add({2, 5, false}, 0);
    p.add({3, 0, true}, 0);
    CHECK(p.takeReady(10, 5000, out) && out.id == 2);
    CHECK(p.size() == 1);   // 1 dropped, 3 kept
    CHECK(!p.takeReady(20, 5000, out) && p.size() == 1);
  }
  {  // A full pool refuses more; room comes back when one is served.
    Pool p;
    CHECK(p.add({1, 0, true}, 0));
    CHECK(p.add({2, 0, true}, 0));
    CHECK(p.add({3, 7, true}, 0));
    CHECK(p.full());
    CHECK(!p.add({4, 0, true}, 0));
    CHECK(p.size() == 3);
    CHECK(p.takeReady(1, 5000, out) && out.id == 3);
    CHECK(!p.full());
    CHECK(p.add({4, 0, true}, 1));
  }
  {  // Among ready ones, first come first served.
    Pool p;
    p.add({1, 0, true}, 0);
    p.add({2, 4, true}, 0);
    p.add({3, 9, true}, 0);
    CHECK(p.takeReady(1, 5000, out) && out.id == 2);
    CHECK(p.takeReady(1, 5000, out) && out.id == 3);
    CHECK(!p.takeReady(1, 5000, out));
    CHECK(p.size() == 1);
  }
}

// The loop's background library requests (core/LibraryFetch.h): one at a time, each job reads only its own answer.
static bbw::FetchState g_fakeLink = bbw::FetchState::IDLE;
static int g_fakeBegun = 0;
static bool g_fakeRefuse = false;
static std::string g_fakeKey;

static void testLibraryFetch() {
  printf("library requests in the background: one at a time, each job its own answer\n");
  using bbw::Fetch;
  using bbw::FetchState;
  bbw::LibraryFetcher f([]() {
    const FetchState s = g_fakeLink;
    if (s == FetchState::OK || s == FetchState::FAILED) g_fakeLink = FetchState::IDLE;   // read once
    return s;
  });
  auto begin = [](const char *key, void *) {
    if (g_fakeRefuse) return false;
    ++g_fakeBegun;
    g_fakeKey = key;
    g_fakeLink = FetchState::RUNNING;
    return true;
  };
  // Started, pending while it runs, then the answer once.
  CHECK(f.step(1, "a.json", begin, nullptr) == Fetch::PENDING && g_fakeBegun == 1 && f.busy(1) && f.busy());
  CHECK(f.step(1, "a.json", begin, nullptr) == Fetch::PENDING && g_fakeBegun == 1);
  // Another job meanwhile: pending, nothing started, the first job's answer untouched.
  CHECK(f.step(2, "b.json", begin, nullptr) == Fetch::PENDING && g_fakeBegun == 1 && !f.busy(2));
  g_fakeLink = FetchState::OK;
  CHECK(f.step(2, "b.json", begin, nullptr) == Fetch::PENDING && g_fakeLink == FetchState::OK);
  CHECK(f.step(1, "a.json", begin, nullptr) == Fetch::OK && !f.busy());
  // Now the second job's turn; a failure is answered as such.
  CHECK(f.step(2, "b.json", begin, nullptr) == Fetch::PENDING && g_fakeBegun == 2 && g_fakeKey == "b.json");
  g_fakeLink = FetchState::FAILED;
  CHECK(f.step(2, "b.json", begin, nullptr) == Fetch::FAIL && !f.busy());
  // Not started (the network is busy): pending, and started on a later turn.
  g_fakeRefuse = true;
  CHECK(f.step(1, "c.json", begin, nullptr) == Fetch::PENDING && !f.busy() && g_fakeBegun == 2);
  g_fakeRefuse = false;
  CHECK(f.step(1, "c.json", begin, nullptr) == Fetch::PENDING && f.busy(1) && g_fakeBegun == 3);
  // The job moved on to another file meanwhile: the old answer is dropped, the new one starts next turn.
  g_fakeLink = FetchState::OK;
  CHECK(f.step(1, "d.json", begin, nullptr) == Fetch::PENDING && !f.busy());
  CHECK(f.step(1, "d.json", begin, nullptr) == Fetch::PENDING && g_fakeKey == "d.json");
  g_fakeLink = FetchState::OK;
  CHECK(f.step(1, "d.json", begin, nullptr) == Fetch::OK);
  // An answer that never came as one (the link reset): taken as a failure, not waited on for ever.
  CHECK(f.step(3, "e.json", begin, nullptr) == Fetch::PENDING);
  g_fakeLink = FetchState::IDLE;
  CHECK(f.step(3, "e.json", begin, nullptr) == Fetch::FAIL && !f.busy());
}
