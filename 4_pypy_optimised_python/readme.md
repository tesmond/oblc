# pypy Optimisation

What we can do is run the exact same code we have written but using the pypy just in time compilation engine. pypy only supports Python version 3.11, however as you've seen from the code we're not using any newer functionality in 3.11 is a reasonably up-to-date version of Python.

So what happens when we run using the JIT? 

We have a five second runtime! That is six times faster just from switching away from cPython and using a JIT instead.