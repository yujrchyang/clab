#ifndef COMMON_VALGRIND_H
#define COMMON_VALGRIND_H

#if defined(__has_include)
#  if __has_include(<valgrind/helgrind.h>)
#    include <valgrind/helgrind.h>
#  else
#    define ANNOTATE_HAPPENS_AFTER(x)             (void)(x)
#    define ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(x)  (void)(x)
#    define ANNOTATE_HAPPENS_BEFORE(x)            (void)(x)
#    define ANNOTATE_BENIGN_RACE_SIZED(address, size, description) (void)(address)
#  endif
#else
#  define ANNOTATE_HAPPENS_AFTER(x)             (void)(x)
#  define ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(x)  (void)(x)
#  define ANNOTATE_HAPPENS_BEFORE(x)            (void)(x)
#  define ANNOTATE_BENIGN_RACE_SIZED(address, size, description) (void)(address)
#endif

#endif  // COMMON_VALGRIND_H
