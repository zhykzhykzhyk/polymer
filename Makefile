CXXFLAGS+=-std=gnu++14 `getconf LFS_CFLAGS` -Wall -g
LDLIBS+=-lstdc++ `getconf LFS_LIBS` -lnuma -lpthread
LDFLAGS+=`getconf LFS_LDFLAGS`
CXX=clang++

ifdef CILK
CXXFLAGS+=-fcilkplus -DCILK
LDLIBS+=-lcilkrts
endif

BINS=test_parallel test_partition test_vertex pagerank
SRCS=test_parallel.cc io.cc test_partition.cc test_vertex.cc pagerank.cc parallel.cc

.PHONY: all clean

all: $(BINS)

%.o: %.cc
	$(CXX) -c -MD -o $@ $(CPPFLAGS) $(CXXFLAGS) $<
	@cp $*.d $*.P; \
	  sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
	      -e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	  rm -f $*.d

test_partition: test_partition.o io.o parallel.o

test_vertex: test_vertex.o io.o parallel.o

pagerank: pagerank.o io.o parallel.o

clean:
	rm -f $(BINS) *.o

-include $(SRCS:.cc=.P)
