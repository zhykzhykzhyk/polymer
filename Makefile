CXXFLAGS=-std=c++14

BINS=
SRCS=

.PHONY: all clean

all: $(BINS)

%.o: %.cc
	$(CXX) -c -MD -o $@ $(CPPFLAGS) $(CXXFLAGS) $<
	@cp $*.d $*.P; \
	  sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
	      -e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	  rm -f $*.d

clean:
	rm -f $(BINS) *.o

-include $(SRCS:.cc=.P)
