# Relaxing the Dependency Rule

If we were to face such a task in python of passing a very large CSV file performing some floating point calculations and returning the output data it is unlikely that we would adopt the pypy optimised Python solution. Normally when you encounter such a task you would reach for a library likely built in C and simply connect it to the Python code.

This variation is implemented using the polars library a library written using rust which is compiled highly performance source project.

As you can see the code is relatively straightforward to read quite succinct obviously extendable and its runtime is 10 seconds.