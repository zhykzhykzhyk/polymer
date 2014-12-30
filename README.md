polymer
=======

A NUMA-aware Graph-structured Analytics Framework

1. ABOUT POLYMER

This code is part of the project "NUMA-aware Graph-structured Analytics",
which is adopted from the project "Ligra: A Lightweight Graph Processing
Framework for Shared Memory".

This code currently compiles with g++ version 4.8.0 or higher with
support for Cilk+. To compile with g++ using Cilk, define the 
environment variable CILK. To compile with g++ with no parallel 
support, make sure CILK is not defined.

Implementations---Currently, Polymer comes with 6 implementation files:
numa-BFS.C, numa-PageRank.C, numa-Components.C, numa-BellmanFord.C, 
numa-SPMV.C. To compile all of them, simply run "make" with the 
appropriate environment variables set as described above. Currently 
the results of the computation are not used, but the code can be 
easily modified to output the results to a file.

To develop a new implementation, simply include "polymer.h" in the
implementation files. When finished, one may add it to the ALL
variable in Makefile.

Functions:

Polymer mostly follows the interface from Ligra 
and thus most applications in Ligra can run with small modifications on Polymer.

The following is a detailed description of the interface.

edgeMap: takes as input 6 arguments: a graph GA, frontier data
structure V and next, struct F, threshold argument (optional, default threshold
is |E|/20), an option in {DENSE, DENSE_FORWARD}
(optional, default value is DENSE), and a data structure storing the 
topology info of the core the caller thread is on. 
It set the frontier next to be the new frontier for next iteration of computation

The F struct must contain three boolean functions: update,
updateAtomic and cond.  update and updateAtomic should take two
integer arguments (corresponding to source and destination vertex). In
addition, updateAtomic should be atomic with respect to the
destination vertex. cond takes one argument corresponding to a vertex.

vertexMap: takes as input 2 arguments: a vertices data structure V and
a function F which is applied to all vertices in V. It does not have a
return value.

vertexFilter: takes as input a vertices data structure V and a boolean
function F which is applied to all vertices in V. It returns a
vertices data structure containing all vertices v in V such that F(v)
returned true.

2. BUILD & RUN

This code currently compiles with g++ version 4.8.0 or higher with
support for Cilk+. To compile with g++ using Cilk, define the 
environment variable CILK. To compile with g++ with no parallel 
support, make sure CILK is not defined.

With correct version of g++ installed (Cilk+ recommended), use
command:
	make
to compile all the 6 implementations.

After that use the following command to run the algorithms:

      PageRank:	   	     ./numa-PageRank [graph file] [maximum iteration]
      SPMV:	   	     ./numa-SPMV [graph file] [maximum iteration]
      BP:	   	     ./numa-BP [graph file] [maximum iteration]
      BFS:		     ./numa-BFS [graph file] [start vertex number]
      BellmanFord:	     ./numa-BellmanFord [graph file] [start vertex number]
      ConnectedComponents:   ./numa-Components [graph file]
      
3. INPUT FORMAT
The input format of an unweighted graphs should be in following format.

 The adjacency graph format from the Problem Based Benchmark Suite
 (http://www.cs.cmu.edu/~pbbs/benchmarks/graphIO.html). The adjacency
 graph format starts with a sequence of offsets one for each vertex,
 followed by a sequence of directed edges ordered by their source
 vertex. The offset for a vertex i refers to the location of the start
 of a contiguous block of out edges for vertex i in the sequence of
 edges. The block continues until the offset of the next vertex, or
 the end if i is the last vertex. All vertices and offsets are 0 based
 and represented in decimal. The specific format is as follows:

AdjacencyGraph
<n>
<m>
<o0>
<o1>
...
<o(n-1)>
<e0>
<e1>
...
<e(m-1)>

This file is represented as plain text.