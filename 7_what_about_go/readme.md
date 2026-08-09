# What About Go

When we run through go, we are adopt similar approach to the optimisation of Python chunking up the file data processing it line by line into groups merging those groups together and calculating the averages, et cetera at the last possible moment.

Go allows a bit of a lower level approach it allows us to use specific types rather than more generic types in python and so we can limit the size of our raise so that we have them to more specific memory and we can therefore pre-allocate memory rather than continuously growing our memory, which is a costly process.

This version takes two seconds to complete the process, however this is obviously a lot more code much more complex to debug and harder to change.