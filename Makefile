CXXFLAGS+=-std=c++1y -D_FILE_OFFSET_BITS=64
LDLIBS+=-lstdc++

ifdef CILK
CXXFLAGS+=-fcilkplus -DCILK
LDLIBS+=-lcilkrts
endif

BINS=test_parallel test_partition
SRCS=test_parallel.cc IO.cc test_partition.cc

.PHONY: all clean

all: $(BINS)

%.o: %.cc
	$(CXX) -c -MD -o $@ $(CPPFLAGS) $(CXXFLAGS) $<
	@cp $*.d $*.P; \
	  sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
	      -e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	  rm -f $*.d

test_partition: test_partition.o io.o

clean:
	rm -f $(BINS) *.o

-include $(SRCS:.cc=.P)
