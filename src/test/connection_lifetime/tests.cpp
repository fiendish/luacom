int main() {
  test("empty and released wrappers have no legacy connection", [] {
    FakeDispatch source; tLuaCOM receiver(source);
    receiver.releaseConnection(); receiver.releaseConnection();
    assert(source.point.unadvises == 0);
    receiver.releaseComObject(); receiver.releaseConnection();
    assert(source.point.unadvises == 0 && source.refs == 0);
  });
  test("only the last connection is released and repeated calls are no-ops", [] {
    FakeDispatch source, sink; tLuaCOM receiver(source), server(sink);
    DWORD a = receiver.addConnection(&server), b = receiver.addConnection(&server);
    receiver.releaseConnection();
    assert(source.point.sinks.count(a) == 1 && source.point.sinks.count(b) == 0);
    receiver.releaseConnection(); receiver.releaseConnection();
    assert(source.point.unadvises == 1 && receiver.connections.size() == 1);
    receiver.releaseComObject();
    assert(source.point.sinks.empty() && source.refs == 0);
  });
  test("a new connection becomes the legacy target after a prior release", [] {
    FakeDispatch source, sink; tLuaCOM receiver(source), server(sink);
    DWORD a = receiver.addConnection(&server); receiver.releaseConnection();
    DWORD b = receiver.addConnection(&server); receiver.releaseConnection();
    assert(a != b && source.point.unadvises == 2 && source.point.sinks.empty());
  });
  test("failed legacy cleanup retains only the last record for retry", [] {
    FakeDispatch source, sink; tLuaCOM receiver(source), server(sink);
    DWORD a = receiver.addConnection(&server), b = receiver.addConnection(&server);
    source.point.failures = 1;
    must_throw([&] { receiver.releaseConnection(); });
    assert(source.point.sinks.size() == 2 && receiver.connections.size() == 2);
    receiver.releaseConnection(); receiver.releaseConnection();
    assert(source.point.sinks.count(a) == 1 && source.point.sinks.count(b) == 0);
    assert(source.point.unadvises == 2);
  });
  test("nested legacy cleanup does not release an older record", [] {
    FakeDispatch source, sink; tLuaCOM receiver(source), server(sink);
    DWORD a = receiver.addConnection(&server), b = receiver.addConnection(&server);
    source.point.during_unadvise = [&] { receiver.releaseConnection(); };
    receiver.releaseConnection();
    assert(source.point.unadvises == 1 && source.point.sinks.count(a) == 1);
    assert(source.point.sinks.count(b) == 0);
  });
  test("full cleanup during legacy cleanup retains the pending owner", [] {
    FakeDispatch source, sink; tLuaCOM receiver(source), server(sink);
    receiver.addConnection(&server); receiver.addConnection(&server);
    source.point.during_unadvise = [&] {
      receiver.releaseComObject();
      assert(receiver.released() && source.refs > 0);
    };
    receiver.releaseConnection();
    assert(source.point.sinks.empty() && receiver.connections.empty() && source.refs == 0);
    assert(source.point.unadvises == 2);
  });
  test("failed legacy cleanup can retry after a full release callback", [] {
    FakeDispatch source, sink; tLuaCOM receiver(source), server(sink);
    receiver.addConnection(&server);
    source.point.failures = 1;
    source.point.during_unadvise = [&] { receiver.releaseComObject(); };
    must_throw([&] { receiver.releaseConnection(); });
    assert(receiver.released() && source.point.sinks.size() == 1 && source.refs > 0);
    receiver.releaseConnection();
    assert(source.point.sinks.empty() && source.refs == 0);
  });
  test("a connection added by a callback remains the new legacy target", [] {
    FakeDispatch source, sink; tLuaCOM receiver(source), server(sink);
    DWORD a = receiver.addConnection(&server), b = receiver.addConnection(&server), c = 0;
    source.point.during_unadvise = [&] { c = receiver.addConnection(&server); };
    receiver.releaseConnection();
    assert(source.point.sinks.count(a) == 1 && source.point.sinks.count(b) == 0);
    assert(source.point.sinks.count(c) == 1);
    receiver.releaseConnection(); receiver.releaseConnection();
    assert(source.point.sinks.count(a) == 1 && source.point.sinks.count(c) == 0);
  });
  test("failed cleanup does not displace a new callback connection", [] {
    FakeDispatch source, sink; tLuaCOM receiver(source), server(sink);
    DWORD a = receiver.addConnection(&server), b = receiver.addConnection(&server), c = 0;
    source.point.failures = 1;
    source.point.during_unadvise = [&] { c = receiver.addConnection(&server); };
    must_throw([&] { receiver.releaseConnection(); });
    receiver.releaseConnection(); receiver.releaseConnection();
    assert(source.point.sinks.count(a) == 1 && source.point.sinks.count(b) == 1);
    assert(source.point.sinks.count(c) == 0 && source.point.unadvises == 2);
    receiver.releaseComObject();
    assert(source.point.sinks.empty() && source.refs == 0);
  });
  test("failed cleanup does not revive a target after a callback consumes a newer one", [] {
    FakeDispatch source, sink; tLuaCOM receiver(source), server(sink);
    DWORD a = receiver.addConnection(&server), b = receiver.addConnection(&server), c = 0;
    source.point.during_unadvise = [&] {
      c = receiver.addConnection(&server); receiver.releaseConnection();
      source.point.failures = 1;
    };
    must_throw([&] { receiver.releaseConnection(); });
    receiver.releaseConnection();
    assert(source.point.sinks.count(a) == 1 && source.point.sinks.count(b) == 1);
    assert(source.point.sinks.count(c) == 0 && source.point.unadvises == 2);
  });
  test("explicit release of the latest connection does not expose older targets", [] {
    FakeDispatch source, sink; tLuaCOM receiver(source), server(sink);
    DWORD a = receiver.addConnection(&server), b = receiver.addConnection(&server);
    receiver.releaseConnection(&server, b); receiver.releaseConnection();
    assert(source.point.sinks.count(a) == 1 && source.point.unadvises == 1);
  });
  test("explicit release of an older connection preserves the legacy target", [] {
    FakeDispatch source, sink; tLuaCOM receiver(source), server(sink);
    DWORD a = receiver.addConnection(&server); receiver.addConnection(&server);
    receiver.releaseConnection(&server, a); receiver.releaseConnection();
    assert(source.point.sinks.empty() && source.point.unadvises == 2);
  });
  std::cout << tests << " patched-source legacy connection scenarios passed\n";
}
