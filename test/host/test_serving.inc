// Loop timing and the web serving helpers (core/LoopTiming.h, core/WebCache.h), 11 Nov 2026.
#include "core/LoopTiming.h"
#include "core/WebCache.h"

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
