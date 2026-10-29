#pragma once

#if defined(__cplusplus) && !defined(HAVE_SNDFILE)
struct SFWrapper {
  int doClose() { return 0; }
  void* doOpen(const char*, int, void*) { return nullptr; }
};
#endif

